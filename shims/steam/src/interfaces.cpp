// Stub-mode Steam interfaces.
//
// Vtable order of every class below matches the slot indices used by the
// flat-API thunks (SteamAPI_ISteamX_Method) in the libsteam_api.dylib that
// shipped with the game, which is built from the same SDK headers. Steam
// interfaces have no virtual destructor, so declaration order == slot order.
// Slots the game is unlikely to need are Z(...) entries returning 0, which
// is ABI-safe here: no method in these interfaces returns via sret or in a
// floating-point register.

#include "shim.h"

#include <copyfile.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#define Z(name) virtual uint64_t name() { return 0; }

static std::recursive_mutex g_mutex;
static time_t g_start_time = time(nullptr);

static uint32_t account_id() { return (uint32_t)(shim_config().steam_id & 0xffffffffULL); }

static std::string app_data_dir() {
    const ShimConfig &c = shim_config();
    return c.data_dir + "/" + std::to_string(account_id()) + "/" + std::to_string(c.app_id);
}

static bool mkdirs(const std::string &path) {
    std::string cur;
    size_t pos = 0;
    while (pos != std::string::npos) {
        pos = path.find('/', pos + 1);
        cur = path.substr(0, pos);
        if (!cur.empty() && mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

static bool is_dir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// ---------------------------------------------------------------------------
// Steam Cloud (ISteamRemoteStorage) backed by <data_dir>/<account>/<app>/remote.
// On first use the directory is seeded with a copy of the real Steam cloud
// folder, so existing profiles carry over; the real folder is never written.
// The copy never keeps `crd`, the game's stored Uplay credentials (it would sign
// in to Ubisoft with them), unless RS_STEAM_KEEP_CREDENTIALS=1.

static void drop_credentials(const std::string &dir) {
    const char *keep = getenv("RS_STEAM_KEEP_CREDENTIALS");
    if (keep && *keep == '1') return;
    std::string crd = dir + "/crd";
    if (unlink(crd.c_str()) == 0) shim_log("removed stored Uplay credentials from %s", dir.c_str());
}

static std::string remote_dir() {
    static std::string dir;
    static std::once_flag once;
    std::call_once(once, [] {
        const ShimConfig &c = shim_config();
        dir = app_data_dir() + "/remote";
        if (is_dir(dir)) {
            drop_credentials(dir);
            return;
        }
        std::string src = c.steam_root + "/userdata/" + std::to_string(account_id()) + "/" +
                          std::to_string(c.app_id) + "/remote";
        mkdirs(app_data_dir());
        if (is_dir(src) && copyfile(src.c_str(), dir.c_str(), nullptr, COPYFILE_ALL | COPYFILE_RECURSIVE) == 0) {
            shim_log("seeded %s from %s", dir.c_str(), src.c_str());
            drop_credentials(dir);
        } else {
            mkdirs(dir);
        }
    });
    return dir;
}

// Cloud file names are relative; reject anything that could escape the directory.
static bool remote_path(const char *name, std::string *out) {
    if (!name || !*name || name[0] == '/') return false;
    std::string n = name;
    for (char &ch : n)
        if (ch == '\\') ch = '/';
    if (n == ".." || n.compare(0, 3, "../") == 0 || n.find("/../") != std::string::npos ||
        (n.size() >= 3 && n.compare(n.size() - 3, 3, "/..") == 0))
        return false;
    *out = remote_dir() + "/" + n;
    return true;
}

static bool write_atomic(const std::string &path, const void *data, size_t size) {
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) mkdirs(path.substr(0, slash));
    std::string tmp = path + ".shimtmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

static int32_t read_into(const std::string &path, void *data, int32_t cub, uint32_t offset = 0) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return 0;
    int32_t n = 0;
    if (cub > 0 && fseeko(f, offset, SEEK_SET) == 0) n = (int32_t)fread(data, 1, (size_t)cub, f);
    fclose(f);
    return n;
}

static void list_files(const std::string &root, const std::string &rel,
                       std::vector<std::pair<std::string, int32_t>> *out) {
    DIR *d = opendir((root + rel).c_str());
    if (!d) return;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || (n.size() > 8 && n.compare(n.size() - 8, 8, ".shimtmp") == 0)) continue;
        std::string r = rel.empty() ? n : rel.substr(1) + "/" + n;
        struct stat st;
        if (stat((root + "/" + r).c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode))
            list_files(root, "/" + r, out);
        else if (S_ISREG(st.st_mode))
            out->push_back(std::make_pair(r, (int32_t)st.st_size));
    }
    closedir(d);
}

struct WriteStream {
    std::string path;
    std::vector<uint8_t> data;
};

class RemoteStorage {
    std::vector<std::pair<std::string, int32_t>> files_;
    std::map<uint64_t, std::vector<uint8_t>> async_reads_;
    std::map<uint64_t, WriteStream> streams_;
    uint64_t next_stream_ = 1;

public:
    virtual bool FileWrite(const char *name, const void *data, int32_t cub) {
        std::string p;
        if (!remote_path(name, &p) || cub < 0 || (!data && cub)) return false;
        bool ok = write_atomic(p, data, (size_t)cub);
        shim_log("FileWrite(%s, %d) -> %d", name, cub, ok);
        return ok;
    }
    virtual int32_t FileRead(const char *name, void *data, int32_t cub) {
        std::string p;
        if (!remote_path(name, &p) || !data) return 0;
        int32_t n = read_into(p, data, cub);
        shim_log("FileRead(%s, %d) -> %d", name, cub, n);
        return n;
    }
    virtual SteamAPICall_t FileWriteAsync(const char *name, const void *data, uint32_t cub) {
        RemoteStorageFileWriteAsyncComplete_t r = {FileWrite(name, data, (int32_t)cub) ? k_EResultOK : k_EResultFail};
        return shim_post_call_result(r.k_iCallback, &r, sizeof r);
    }
    virtual SteamAPICall_t FileReadAsync(const char *name, uint32_t offset, uint32_t cub) {
        std::string p;
        RemoteStorageFileReadAsyncComplete_t r = {0, k_EResultFileNotFound, offset, 0};
        std::vector<uint8_t> buf(cub);
        if (remote_path(name, &p) && access(p.c_str(), R_OK) == 0) {
            r.m_cubRead = (uint32_t)read_into(p, buf.data(), (int32_t)cub, offset);
            r.m_eResult = k_EResultOK;
        }
        buf.resize(r.m_cubRead);
        SteamAPICall_t h = shim_post_call_result(r.k_iCallback, &r, sizeof r, true);
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        async_reads_[h].swap(buf);
        return h;
    }
    virtual bool FileReadAsyncComplete(SteamAPICall_t h, void *out, uint32_t cub) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto it = async_reads_.find(h);
        if (it == async_reads_.end() || !out) return false;
        memcpy(out, it->second.data(), cub < it->second.size() ? cub : it->second.size());
        async_reads_.erase(it);
        return true;
    }
    virtual bool FileForget(const char *) { return true; }
    virtual bool FileDelete(const char *name) {
        std::string p;
        return remote_path(name, &p) && unlink(p.c_str()) == 0;
    }
    Z(FileShare)
    virtual bool SetSyncPlatforms(const char *, uint32_t) { return true; }
    virtual uint64_t FileWriteStreamOpen(const char *name) {
        std::string p;
        if (!remote_path(name, &p)) return 0;
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        uint64_t h = next_stream_++;
        streams_[h].path = p;
        return h;
    }
    virtual bool FileWriteStreamWriteChunk(uint64_t h, const void *data, int32_t cub) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto it = streams_.find(h);
        if (it == streams_.end() || cub < 0 || (!data && cub)) return false;
        it->second.data.insert(it->second.data.end(), (const uint8_t *)data, (const uint8_t *)data + cub);
        return true;
    }
    virtual bool FileWriteStreamClose(uint64_t h) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto it = streams_.find(h);
        if (it == streams_.end()) return false;
        bool ok = write_atomic(it->second.path, it->second.data.data(), it->second.data.size());
        streams_.erase(it);
        return ok;
    }
    virtual bool FileWriteStreamCancel(uint64_t h) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        return streams_.erase(h) != 0;
    }
    virtual bool FileExists(const char *name) {
        std::string p;
        struct stat st;
        return remote_path(name, &p) && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }
    virtual bool FilePersisted(const char *name) { return FileExists(name); }
    virtual int32_t GetFileSize(const char *name) {
        std::string p;
        struct stat st;
        return remote_path(name, &p) && stat(p.c_str(), &st) == 0 ? (int32_t)st.st_size : 0;
    }
    virtual int64_t GetFileTimestamp(const char *name) {
        std::string p;
        struct stat st;
        return remote_path(name, &p) && stat(p.c_str(), &st) == 0 ? (int64_t)st.st_mtime : 0;
    }
    virtual uint32_t GetSyncPlatforms(const char *) { return 0xffffffffu; }  // k_ERemoteStoragePlatformAll
    virtual int32_t GetFileCount() {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        files_.clear();
        list_files(remote_dir(), "", &files_);
        return (int32_t)files_.size();
    }
    virtual const char *GetFileNameAndSize(int32_t i, int32_t *size) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (files_.empty()) GetFileCount();
        if (i < 0 || i >= (int32_t)files_.size()) {
            if (size) *size = 0;
            return "";
        }
        if (size) *size = files_[i].second;
        return files_[i].first.c_str();
    }
    virtual bool GetQuota(uint64_t *total, uint64_t *available) {
        if (total) *total = 1ULL << 30;
        if (available) *available = 1ULL << 30;
        return true;
    }
    virtual bool IsCloudEnabledForAccount() { return true; }
    virtual bool IsCloudEnabledForApp() { return true; }
    virtual void SetCloudEnabledForApp(bool) {}
    Z(UGCDownload) Z(GetUGCDownloadProgress) Z(GetUGCDetails) Z(UGCRead) Z(GetCachedUGCCount)
    Z(GetCachedUGCHandle) Z(PublishWorkshopFile) Z(CreatePublishedFileUpdateRequest)
    Z(UpdatePublishedFileFile) Z(UpdatePublishedFilePreviewFile) Z(UpdatePublishedFileTitle)
    Z(UpdatePublishedFileDescription) Z(UpdatePublishedFileVisibility) Z(UpdatePublishedFileTags)
    Z(CommitPublishedFileUpdate) Z(GetPublishedFileDetails) Z(DeletePublishedFile)
    Z(EnumerateUserPublishedFiles) Z(SubscribePublishedFile) Z(EnumerateUserSubscribedFiles)
    Z(UnsubscribePublishedFile) Z(UpdatePublishedFileSetChangeDescription)
    Z(GetPublishedItemVoteDetails) Z(UpdateUserPublishedItemVote) Z(GetUserPublishedItemVoteDetails)
    Z(EnumerateUserSharedWorkshopFiles) Z(PublishVideo) Z(SetUserPublishedFileAction)
    Z(EnumeratePublishedFilesByUserAction) Z(EnumeratePublishedWorkshopFiles) Z(UGCDownloadToLocation)
};

// ---------------------------------------------------------------------------
// ISteamUser

class User {
public:
    virtual HSteamUser GetHSteamUser() { return 1; }
    virtual bool BLoggedOn() { return true; }
    virtual CSteamID GetSteamID() { return shim_config().steam_id; }
    Z(InitiateGameConnection)
    Z(TerminateGameConnection)
    Z(TrackAppUsageEvent)
    virtual bool GetUserDataFolder(char *buf, int cub) {
        std::string dir = app_data_dir() + "/local";
        mkdirs(dir);
        if (!buf || cub <= (int)dir.size()) return false;
        memcpy(buf, dir.c_str(), dir.size() + 1);
        return true;
    }
    Z(StartVoiceRecording)
    Z(StopVoiceRecording)
    virtual int GetAvailableVoice(uint32_t *compressed, uint32_t *uncompressed, uint32_t) {
        if (compressed) *compressed = 0;
        if (uncompressed) *uncompressed = 0;
        return 2;  // k_EVoiceResultNotRecording
    }
    virtual int GetVoice() { return 2; }
    virtual int DecompressVoice() { return 2; }
    Z(GetVoiceOptimalSampleRate)
    virtual uint32_t GetAuthSessionTicket(void *, int, uint32_t *cb) {
        if (cb) *cb = 0;
        return 0;  // k_HAuthTicketInvalid
    }
    virtual int BeginAuthSession() { return 1; }  // k_EBeginAuthSessionResultInvalidTicket
    Z(EndAuthSession)
    Z(CancelAuthTicket)
    virtual int UserHasLicenseForApp(CSteamID id, AppId_t app) {
        // k_EUserHasLicenseResultHasLicense / DoesNotHaveLicense
        return id == shim_config().steam_id && app == shim_config().app_id ? 0 : 1;
    }
    Z(BIsBehindNAT) Z(AdvertiseGame) Z(RequestEncryptedAppTicket) Z(GetEncryptedAppTicket)
    Z(GetGameBadgeLevel) Z(GetPlayerSteamLevel) Z(RequestStoreAuthURL) Z(BIsPhoneVerified)
    Z(BIsTwoFactorEnabled) Z(BIsPhoneIdentifying) Z(BIsPhoneRequiringVerification)
    Z(GetMarketEligibility) Z(GetDurationControl)
};

// ---------------------------------------------------------------------------
// ISteamFriends (every const char * returner is implemented explicitly)

class Friends {
public:
    virtual const char *GetPersonaName() { return shim_config().persona.c_str(); }
    Z(SetPersonaName)
    virtual int GetPersonaState() { return 1; }  // k_EPersonaStateOnline
    Z(GetFriendCount) Z(GetFriendByIndex) Z(GetFriendRelationship) Z(GetFriendPersonaState)
    virtual const char *GetFriendPersonaName(CSteamID id) {
        return id == shim_config().steam_id ? shim_config().persona.c_str() : "";
    }
    Z(GetFriendGamePlayed)
    virtual const char *GetFriendPersonaNameHistory(CSteamID, int) { return ""; }
    Z(GetFriendSteamLevel)
    virtual const char *GetPlayerNickname(CSteamID) { return nullptr; }  // documented: NULL if none
    Z(GetFriendsGroupCount) Z(GetFriendsGroupIDByIndex)
    virtual const char *GetFriendsGroupName(int16_t) { return ""; }
    Z(GetFriendsGroupMembersCount) Z(GetFriendsGroupMembersList) Z(HasFriend) Z(GetClanCount)
    Z(GetClanByIndex)
    virtual const char *GetClanName(CSteamID) { return ""; }
    virtual const char *GetClanTag(CSteamID) { return ""; }
    Z(GetClanActivityCounts) Z(DownloadClanActivityCounts) Z(GetFriendCountFromSource)
    Z(GetFriendFromSourceByIndex) Z(IsUserInSource) Z(SetInGameVoiceSpeaking) Z(ActivateGameOverlay)
    Z(ActivateGameOverlayToUser) Z(ActivateGameOverlayToWebPage) Z(ActivateGameOverlayToStore)
    Z(SetPlayedWith) Z(ActivateGameOverlayInviteDialog) Z(GetSmallFriendAvatar)
    Z(GetMediumFriendAvatar) Z(GetLargeFriendAvatar) Z(RequestUserInformation)
    Z(RequestClanOfficerList) Z(GetClanOwner) Z(GetClanOfficerCount) Z(GetClanOfficerByIndex)
    Z(GetUserRestrictions)
    virtual bool SetRichPresence(const char *, const char *) { return true; }
    Z(ClearRichPresence)
    virtual const char *GetFriendRichPresence(CSteamID, const char *) { return ""; }
    Z(GetFriendRichPresenceKeyCount)
    virtual const char *GetFriendRichPresenceKeyByIndex(CSteamID, int) { return ""; }
    Z(RequestFriendRichPresence) Z(InviteUserToGame) Z(GetCoplayFriendCount) Z(GetCoplayFriend)
    Z(GetFriendCoplayTime) Z(GetFriendCoplayGame) Z(JoinClanChatRoom) Z(LeaveClanChatRoom)
    Z(GetClanChatMemberCount) Z(GetChatMemberByIndex) Z(SendClanChatMessage) Z(GetClanChatMessage)
    Z(IsClanChatAdmin) Z(IsClanChatWindowOpenInSteam) Z(OpenClanChatWindowInSteam)
    Z(CloseClanChatWindowInSteam) Z(SetListenForFriendsMessages) Z(ReplyToFriendMessage)
    Z(GetFriendMessage) Z(GetFollowerCount) Z(IsFollowing) Z(EnumerateFollowingList) Z(IsClanPublic)
    Z(IsClanOfficialGameGroup) Z(GetNumChatsWithUnreadPriorityMessages)
};

// ---------------------------------------------------------------------------
// ISteamUtils

class Utils {
public:
    virtual uint32_t GetSecondsSinceAppActive() { return (uint32_t)(time(nullptr) - g_start_time); }
    virtual uint32_t GetSecondsSinceComputerActive() {
        struct timeval boot;
        size_t len = sizeof boot;
        int mib[2] = {CTL_KERN, KERN_BOOTTIME};
        if (sysctl(mib, 2, &boot, &len, nullptr, 0) != 0) return 0;
        return (uint32_t)(time(nullptr) - boot.tv_sec);
    }
    virtual int GetConnectedUniverse() { return 1; }  // k_EUniversePublic
    virtual uint32_t GetServerRealTime() { return (uint32_t)time(nullptr); }
    virtual const char *GetIPCountry() { return "US"; }
    Z(GetImageSize) Z(GetImageRGBA) Z(GetCSERIPPort)
    virtual uint8_t GetCurrentBatteryPower() { return 255; }  // on AC power
    virtual uint32_t GetAppID() { return shim_config().app_id; }
    Z(SetOverlayNotificationPosition)
    virtual bool IsAPICallCompleted(SteamAPICall_t h, bool *failed) { return shim_call_completed(h, failed); }
    virtual int GetAPICallFailureReason(SteamAPICall_t h) {
        return shim_call_completed(h, nullptr) ? -1 : 2;  // None / InvalidHandle
    }
    virtual bool GetAPICallResult(SteamAPICall_t h, void *out, int cub, int expected, bool *failed) {
        return shim_call_result(h, out, cub, expected, failed);
    }
    Z(RunFrame)  // deprecated slot, absent from the flat API
    Z(GetIPCCallCount) Z(SetWarningMessageHook) Z(IsOverlayEnabled) Z(BOverlayNeedsPresent)
    Z(CheckFileSignature) Z(ShowGamepadTextInput) Z(GetEnteredGamepadTextLength)
    Z(GetEnteredGamepadTextInput)
    virtual const char *GetSteamUILanguage() { return shim_config().language.c_str(); }
    Z(IsSteamRunningInVR) Z(SetOverlayNotificationInset) Z(IsSteamInBigPictureMode)
    Z(StartVRDashboard) Z(IsVRHeadsetStreamingEnabled) Z(SetVRHeadsetStreamingEnabled)
    Z(IsSteamChinaLauncher) Z(InitFilterText) Z(FilterText)
};

// ---------------------------------------------------------------------------
// ISteamApps. Only the base game is reported as owned; DLC ownership needs
// the real backend.

class Apps {
public:
    virtual bool BIsSubscribed() { return true; }
    Z(BIsLowViolence) Z(BIsCybercafe) Z(BIsVACBanned)
    virtual const char *GetCurrentGameLanguage() { return shim_config().language.c_str(); }
    virtual const char *GetAvailableGameLanguages() { return shim_config().language.c_str(); }
    virtual bool BIsSubscribedApp(AppId_t app) { return app == shim_config().app_id; }
    Z(BIsDlcInstalled) Z(GetEarliestPurchaseUnixTime) Z(BIsSubscribedFromFreeWeekend)
    Z(GetDLCCount) Z(BGetDLCDataByIndex) Z(InstallDLC) Z(UninstallDLC)
    Z(RequestAppProofOfPurchaseKey) Z(GetCurrentBetaName) Z(MarkContentCorrupt) Z(GetInstalledDepots)
    virtual uint32_t GetAppInstallDir(AppId_t app, char *buf, uint32_t cub) {
        const std::string &dir = shim_config().game_dir;
        if (app != shim_config().app_id || !buf || cub <= dir.size()) return 0;
        memcpy(buf, dir.c_str(), dir.size() + 1);
        return (uint32_t)dir.size();
    }
    virtual bool BIsAppInstalled(AppId_t app) { return app == shim_config().app_id; }
    virtual CSteamID GetAppOwner() { return shim_config().steam_id; }
    virtual const char *GetLaunchQueryParam(const char *) { return ""; }
    Z(GetDlcDownloadProgress) Z(GetAppBuildId) Z(RequestAllProofOfPurchaseKeys) Z(GetFileDetails)
    Z(GetLaunchCommandLine) Z(BIsSubscribedFromFamilySharing)
};

// ---------------------------------------------------------------------------
// ISteamUserStats: local stats/achievements persisted to <app data>/stats.txt.

class UserStats {
    std::map<std::string, uint32_t> stats_;  // raw 32-bit value (int32 or float bits)
    std::map<std::string, uint32_t> achievements_;  // name -> unlock time (0 = locked)
    bool loaded_ = false;

    std::string path() { return app_data_dir() + "/stats.txt"; }

public:
    void Load() {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (loaded_) return;
        loaded_ = true;
        FILE *f = fopen(path().c_str(), "r");
        if (!f) return;
        char kind, name[256];
        unsigned v;
        while (fscanf(f, " %c %255s %u", &kind, name, &v) == 3) {
            if (kind == 'S') stats_[name] = v;
            if (kind == 'A') achievements_[name] = v;
        }
        fclose(f);
    }
    bool Save() {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        std::string out;
        for (auto &s : stats_) out += "S " + s.first + " " + std::to_string(s.second) + "\n";
        for (auto &a : achievements_) out += "A " + a.first + " " + std::to_string(a.second) + "\n";
        mkdirs(app_data_dir());
        return write_atomic(path(), out.data(), out.size());
    }

    virtual bool RequestCurrentStats() {
        Load();
        UserStatsReceived_t r = {shim_config().app_id, k_EResultOK, shim_config().steam_id};
        shim_post_callback(r.k_iCallback, &r, sizeof r);
        return true;
    }
    virtual bool GetStat(const char *name, int32_t *v) { return GetRaw(name, v); }
    virtual bool GetStat0(const char *name, float *v) { return GetRaw(name, v); }
    virtual bool SetStat(const char *name, int32_t v) { return SetRaw(name, &v); }
    virtual bool SetStat0(const char *name, float v) { return SetRaw(name, &v); }
    virtual bool UpdateAvgRateStat(const char *, float, double) { return true; }
    virtual bool GetAchievement(const char *name, bool *achieved) { return GetAchievementAndUnlockTime(name, achieved, nullptr); }
    virtual bool SetAchievement(const char *name) {
        if (!name || strlen(name) >= 256 || strchr(name, ' ')) return false;
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        uint32_t &t = achievements_[name];
        if (!t) t = (uint32_t)time(nullptr);
        return true;
    }
    virtual bool ClearAchievement(const char *name) {
        if (!name) return false;
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        achievements_[name] = 0;
        return true;
    }
    virtual bool GetAchievementAndUnlockTime(const char *name, bool *achieved, uint32_t *when) {
        if (!name) return false;
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto it = achievements_.find(name);
        uint32_t t = it == achievements_.end() ? 0 : it->second;
        if (achieved) *achieved = t != 0;
        if (when) *when = t;
        return true;
    }
    virtual bool StoreStats() {
        UserStatsStored_t r = {shim_config().app_id, Save() ? k_EResultOK : k_EResultFail};
        shim_post_callback(r.k_iCallback, &r, sizeof r);
        return true;
    }
    Z(GetAchievementIcon)
    virtual const char *GetAchievementDisplayAttribute(const char *, const char *) { return ""; }
    virtual bool IndicateAchievementProgress(const char *, uint32_t, uint32_t) { return true; }
    Z(GetNumAchievements)
    virtual const char *GetAchievementName(uint32_t) { return ""; }
    Z(RequestUserStats) Z(GetUserStat) Z(GetUserStat0) Z(GetUserAchievement)
    Z(GetUserAchievementAndUnlockTime)
    virtual bool ResetAllStats(bool achievements_too) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        stats_.clear();
        if (achievements_too) achievements_.clear();
        return true;
    }
    Z(FindOrCreateLeaderboard) Z(FindLeaderboard)
    virtual const char *GetLeaderboardName(uint64_t) { return ""; }
    Z(GetLeaderboardEntryCount) Z(GetLeaderboardSortMethod) Z(GetLeaderboardDisplayType)
    Z(DownloadLeaderboardEntries) Z(DownloadLeaderboardEntriesForUsers) Z(GetDownloadedLeaderboardEntry)
    Z(UploadLeaderboardScore) Z(AttachLeaderboardUGC) Z(GetNumberOfCurrentPlayers)
    Z(RequestGlobalAchievementPercentages)
    virtual int GetMostAchievedAchievementInfo(char *, uint32_t, float *, bool *) { return -1; }
    virtual int GetNextMostAchievedAchievementInfo(int, char *, uint32_t, float *, bool *) { return -1; }
    Z(GetAchievementAchievedPercent) Z(RequestGlobalStats) Z(GetGlobalStat) Z(GetGlobalStat0)
    Z(GetGlobalStatHistory) Z(GetGlobalStatHistory0)

private:
    bool GetRaw(const char *name, void *out) {
        if (!name || !out) return false;
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        auto it = stats_.find(name);
        uint32_t v = it == stats_.end() ? 0 : it->second;
        memcpy(out, &v, sizeof v);
        return true;
    }
    bool SetRaw(const char *name, const void *in) {
        if (!name || strlen(name) >= 256 || strchr(name, ' ')) return false;
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        memcpy(&stats_[name], in, sizeof(uint32_t));
        return true;
    }
};

// ---------------------------------------------------------------------------
// Null interface: 160 slots that all return 0. Served for ISteamHTTP (whose
// handles are all integers, so "invalid handle"/false is a clean failure) and
// for any interface we do not model, so a stray call cannot hit NULL.

typedef uint64_t (*ZeroFn)();
static uint64_t zero_fn() { return 0; }
struct NullInterface {
    void **vtbl;
};
static void *g_null_vtbl[160];
static NullInterface g_null = {g_null_vtbl};
__attribute__((constructor)) static void init_null_vtbl() {
    for (void *&slot : g_null_vtbl) slot = (void *)(ZeroFn)zero_fn;
}

// ---------------------------------------------------------------------------
// ISteamClient (SteamClient019)

class Client;
static void *lookup(const char *version);

class Client {
public:
    virtual HSteamPipe CreateSteamPipe() { return 1; }
    virtual bool BReleaseSteamPipe(HSteamPipe) { return true; }
    virtual HSteamUser ConnectToGlobalUser(HSteamPipe) { return 1; }
    virtual HSteamUser CreateLocalUser(HSteamPipe *pipe, int) {
        if (pipe) *pipe = 1;
        return 1;
    }
    virtual void ReleaseUser(HSteamPipe, HSteamUser) {}
    virtual void *GetISteamUser(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamGameServer(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void SetLocalIPBinding() {}
    virtual void *GetISteamFriends(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamUtils(HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamMatchmaking(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamMatchmakingServers(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamGenericInterface(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamUserStats(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamGameServerStats(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamApps(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamNetworking(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamRemoteStorage(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamScreenshots(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamGameSearch(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    Z(RunFrame)
    Z(GetIPCCallCount)
    Z(SetWarningMessageHook)
    virtual bool BShutdownIfAllPipesClosed() { return true; }
    virtual void *GetISteamHTTP(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *DEPRECATED_GetISteamUnifiedMessages(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamController(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamUGC(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamAppList(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamMusic(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamMusicRemote(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamHTMLSurface(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    Z(DEPRECATED_Set_SteamAPI_CPostAPIResultInProcess)
    Z(DEPRECATED_Remove_SteamAPI_CPostAPIResultInProcess)
    Z(Set_SteamAPI_CCheckCallbackRegisteredInProcess)
    virtual void *GetISteamInventory(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamVideo(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamParentalSettings(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamInput(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamParties(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
    virtual void *GetISteamRemotePlay(HSteamUser, HSteamPipe, const char *v) { return lookup(v); }
};

// ---------------------------------------------------------------------------

static Client g_client;
static User g_user;
static Friends g_friends;
static Utils g_utils;
static Apps g_apps;
static UserStats g_stats;
static RemoteStorage g_remote;

static bool starts_with(const char *s, const char *prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

static void *lookup(const char *v) {
    if (!v) return &g_null;
    struct Entry {
        const char *prefix, *expected;
        void *obj;
    } table[] = {
        {"SteamClient", "SteamClient019", &g_client},
        {"SteamUser0", "SteamUser020", &g_user},  // not SteamUserStats
        {"SteamFriends", "SteamFriends017", &g_friends},
        {"SteamUtils", "SteamUtils009", &g_utils},
        {"STEAMAPPS_INTERFACE_VERSION", "STEAMAPPS_INTERFACE_VERSION008", &g_apps},
        {"STEAMUSERSTATS_INTERFACE_VERSION", "STEAMUSERSTATS_INTERFACE_VERSION011", &g_stats},
        {"STEAMREMOTESTORAGE_INTERFACE_VERSION", "STEAMREMOTESTORAGE_INTERFACE_VERSION014", &g_remote},
    };
    for (const Entry &e : table) {
        if (!starts_with(v, e.prefix)) continue;
        if (strcmp(v, e.expected) != 0)
            shim_log("WARNING: %s requested, mock implements %s; vtable layout may differ", v, e.expected);
        return e.obj;
    }
    return &g_null;
}

void *shim_find_interface(const char *version) { return lookup(version); }

void shim_stub_init() {
    g_stats.Load();
    remote_dir();
}

void shim_stub_shutdown() { g_stats.Save(); }

// ---------------------------------------------------------------------------
// Self-check of vtable slots against the indices extracted from the original
// libsteam_api.dylib (used by tests/test_shim.cpp; not exported).

template <typename F>
static long vslot(F f) {
    // Itanium ABI: a pointer to a virtual member holds 1 + byte offset in the
    // vtable; the ARM variant stores the plain offset and flags adj instead.
    uintptr_t raw;
    memcpy(&raw, &f, sizeof raw);
#if defined(__arm64__) || defined(__aarch64__)
    return (long)(raw / sizeof(void *));
#else
    return (long)((raw - 1) / sizeof(void *));
#endif
}

int shim_check_vtables(void (*report)(const char *, long, long)) {
    int bad = 0;
#define CHECK(cls, m, idx)                                  \
    if (vslot(&cls::m) != idx) {                            \
        bad++;                                              \
        if (report) report(#cls "::" #m, vslot(&cls::m), idx); \
    }
    CHECK(User, GetSteamID, 2) CHECK(User, GetUserDataFolder, 6) CHECK(User, GetAuthSessionTicket, 13)
    CHECK(User, UserHasLicenseForApp, 17) CHECK(User, GetDurationControl, 30)
    CHECK(Friends, GetPersonaName, 0) CHECK(Friends, GetFriendPersonaName, 7)
    CHECK(Friends, GetPlayerNickname, 11) CHECK(Friends, GetClanName, 20) CHECK(Friends, SetRichPresence, 43)
    CHECK(Friends, GetFriendRichPresence, 45) CHECK(Friends, GetFriendRichPresenceKeyByIndex, 47)
    CHECK(Friends, GetNumChatsWithUnreadPriorityMessages, 72)
    CHECK(Utils, GetIPCountry, 4) CHECK(Utils, GetAppID, 9) CHECK(Utils, GetAPICallResult, 13)
    CHECK(Utils, GetIPCCallCount, 15) CHECK(Utils, GetSteamUILanguage, 23) CHECK(Utils, FilterText, 32)
    CHECK(Apps, GetCurrentGameLanguage, 4) CHECK(Apps, BIsSubscribedApp, 6) CHECK(Apps, GetAppInstallDir, 18)
    CHECK(Apps, GetAppOwner, 20) CHECK(Apps, GetLaunchQueryParam, 21) CHECK(Apps, BIsSubscribedFromFamilySharing, 27)
    CHECK(UserStats, SetStat0, 4) CHECK(UserStats, SetAchievement, 7) CHECK(UserStats, StoreStats, 10)
    CHECK(UserStats, GetAchievementName, 15) CHECK(UserStats, ResetAllStats, 21)
    CHECK(UserStats, GetLeaderboardName, 24) CHECK(UserStats, GetMostAchievedAchievementInfo, 35)
    CHECK(UserStats, GetGlobalStatHistory0, 42)
    CHECK(RemoteStorage, FileRead, 1) CHECK(RemoteStorage, FileWriteStreamOpen, 9)
    CHECK(RemoteStorage, GetFileNameAndSize, 19) CHECK(RemoteStorage, SetCloudEnabledForApp, 23)
    CHECK(RemoteStorage, UGCDownloadToLocation, 54)
    CHECK(Client, GetISteamUser, 5) CHECK(Client, GetISteamUtils, 9) CHECK(Client, GetISteamGenericInterface, 12)
    CHECK(Client, GetISteamHTTP, 24) CHECK(Client, GetISteamInventory, 35) CHECK(Client, GetISteamRemotePlay, 40)
#undef CHECK
    return bad;
}
