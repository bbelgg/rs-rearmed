// The ENUMERATE menu entry, shared by both editions (interfaces.cpp is compiled into each).
//
// The entry is the main menu's SHOP under another label (tools/play-native/cache_overlay.py). Before the game
// opens the Shop it asks ISteamUtils::IsOverlayEnabled, and it asks nowhere else (the Mac's four callers of
// RSConnectService slot 126, Windows' of slot 125: two on the Shop stage, two that look like store pages).
// Nothing polls it: each of the four shows the notice when the overlay is off, as it always is here, so a poll
// would be a notice on every poll. The game stays in the menu and shows that notice, which the overlay archive
// rewords; the Shop itself, which needs the Steam overlay and the servers, never opens. So that question is the
// press: enumerate_press() brings new song archives into the copy and then has the game scan dlc/ again, on a
// thread of its own, as the copy can take a while. The game mounts what it finds, and the song list has it,
// without a restart.
//
// The game reads the app's own copy of the files, so a song dropped into Steam's dlc/ folder is not there yet.
// enumerate_sync() copies what the copy lacks from the folders it was made from (source.json, written by
// gamefiles.py): only new archives of the edition's kind, as clones when it can, under a new name, so no file the
// game has open is touched. Changed and removed archives wait for Refresh Game Files, and a song put straight into
// the copy's dlc/ needs no copying. What comes over keeps its size and time, so a refresh finds it the same and
// adds it to the manifest and steam.env, and .rearmed/enumerated.txt names it, so a refresh also removes it when
// the source no longer has it, as it does for the rest of the copy. A source folder that is gone, or too little room
// for a real copy, leaves the copy as it is and says so in the log; the game still scans what the copy has.
//
// The scan is the game's own: the done callback posts DlcInstalled_t, the Steam event of a finished DLC
// download, whose listener asks the DLC service to enumerate (RSSteamDownloadableContent::OnDlcInstalled ->
// RSDLCService slot 13; the same flag RSMods sets by hand). The listener only knows the DLC of the game's
// list, so the event names the free 248750, which is always in it.
#ifndef RS_STEAM_ENUMERATE_H
#define RS_STEAM_ENUMERATE_H

#include <copyfile.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <string>
#include <vector>

static const uint32_t kEnumerateAppId = 248750;  // always in GetDLCCount's list (shim.cpp)
static const int kDlcInstalledCallback = 1005;   // DlcInstalled_t { AppId_t }

// One JSON string at s[i] (the opening quote): its text, and i past the closing quote. gamefiles.py writes
// non-ASCII as \uXXXX.
static bool enumerate_json_string(const std::string &s, size_t &i, std::string *out) {
    auto hex4 = [&](size_t at, uint32_t *v) {
        if (at + 4 > s.size()) return false;
        *v = (uint32_t)strtoul(s.substr(at, 4).c_str(), nullptr, 16);
        return true;
    };
    out->clear();
    if (i >= s.size() || s[i] != '"') return false;
    for (i++; i < s.size(); i++) {
        char c = s[i];
        if (c == '"') {
            i++;
            return true;
        }
        if (c != '\\') {
            *out += c;
            continue;
        }
        if (++i >= s.size()) return false;
        uint32_t u = 0;
        switch (s[i]) {
        case 'b': *out += '\b'; break;
        case 'f': *out += '\f'; break;
        case 'n': *out += '\n'; break;
        case 'r': *out += '\r'; break;
        case 't': *out += '\t'; break;
        case 'u':
            if (!hex4(i + 1, &u)) return false;
            i += 4;
            if (u >= 0xd800 && u < 0xdc00 && i + 2 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                uint32_t lo = 0;
                if (!hex4(i + 3, &lo)) return false;
                u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
                i += 6;
            }
            if (u < 0x80) {
                *out += (char)u;
            } else if (u < 0x800) {
                *out += (char)(0xc0 | u >> 6);
                *out += (char)(0x80 | (u & 0x3f));
            } else if (u < 0x10000) {
                *out += (char)(0xe0 | u >> 12);
                *out += (char)(0x80 | (u >> 6 & 0x3f));
                *out += (char)(0x80 | (u & 0x3f));
            } else {
                *out += (char)(0xf0 | u >> 18);
                *out += (char)(0x80 | (u >> 12 & 0x3f));
                *out += (char)(0x80 | (u >> 6 & 0x3f));
                *out += (char)(0x80 | (u & 0x3f));
            }
            break;
        default: *out += s[i]; break;  // \" \\ \/
        }
    }
    return false;
}

// What the copy was made from, source.json's text: the edition, and the song folders (source/dlc, dlc_from...).
static bool enumerate_source(const std::string &json, std::string *edition, std::vector<std::string> *folders) {
    size_t i = 0;
    while ((i = json.find('"', i)) != std::string::npos) {
        std::string key, v;
        if (!enumerate_json_string(json, i, &key)) return false;
        while (i < json.size() && isspace((unsigned char)json[i])) i++;
        if (i >= json.size() || json[i] != ':') continue;  // a value, not a key
        i++;
        while (i < json.size() && isspace((unsigned char)json[i])) i++;
        if (i < json.size() && json[i] == '"') {
            if (!enumerate_json_string(json, i, &v)) return false;
            if (key == "edition") *edition = v;
            if (key == "source") folders->insert(folders->begin(), v + "/dlc");
        } else if (i < json.size() && json[i] == '[') {
            while (++i < json.size() && json[i] != ']') {
                if (json[i] != '"') continue;
                if (!enumerate_json_string(json, i, &v)) return false;
                if (key == "dlc_from") folders->push_back(v);
                i--;
            }
        }
    }
    return !edition->empty();
}

static std::string enumerate_read(const std::string &path) {
    std::string text;
    if (FILE *f = fopen(path.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        fclose(f);
    }
    return text;
}

static void enumerate_mkdirs(const std::string &dir) {
    if (mkdir(dir.c_str(), 0755) == 0 || errno != ENOENT) return;
    size_t cut = dir.rfind('/');
    if (cut == std::string::npos || cut == 0) return;
    enumerate_mkdirs(dir.substr(0, cut));
    mkdir(dir.c_str(), 0755);
}

struct EnumerateCopy {
    std::string game_dir;
    const char *suffix;
    dev_t dev;  // of the copy: an archive on the same volume is a clone and needs no room
    int copied;
    bool stop;
};

static const unsigned long long kEnumerateMargin = 256ull << 20;  // as gamefiles.py's MARGIN

static void enumerate_say(const char *what, const std::string &name) {
    fprintf(stderr, "[steam-shim] enumerate: %s: %s\n", what, name.c_str());  // in the run's log, whatever the switches
}

static bool enumerate_room(const EnumerateCopy &e, const struct stat &st) {
    if (st.st_dev == e.dev) return true;
    struct statvfs v;
    if (statvfs(e.game_dir.c_str(), &v) != 0) return true;  // the copy itself will tell
    return (unsigned long long)v.f_bavail * v.f_frsize >= (unsigned long long)st.st_size + kEnumerateMargin;
}

// One archive, written next to its place first, so the game never sees half a file; REL is its path in the copy.
static void enumerate_one(EnumerateCopy &e, const std::string &from, const std::string &rel, const struct stat &st) {
    std::string to = e.game_dir + "/" + rel, part = to + ".part";
    if (!enumerate_room(e, st)) {
        enumerate_say("not enough room in the copy, stopping before", rel);
        e.stop = true;
        return;
    }
    enumerate_mkdirs(to.substr(0, to.rfind('/')));
    unlink(part.c_str());
    // A link to an archive is copied as the archive: COPYFILE_CLONE copies a link as a link, which a relative
    // one makes a dangling file here, copied again at every press.
    char real[PATH_MAX];
    const char *src = realpath(from.c_str(), real) ? real : from.c_str();
    if (copyfile(src, part.c_str(), nullptr, COPYFILE_CLONE) != 0 || rename(part.c_str(), to.c_str()) != 0) {
        e.stop = errno == ENOSPC;
        enumerate_say(strerror(errno), rel);
        unlink(part.c_str());
        return;
    }
    if (FILE *f = fopen((e.game_dir + "/.rearmed/enumerated.txt").c_str(), "a")) {
        fprintf(f, "%s\n", rel.c_str());
        fclose(f);
    }
    e.copied++;
}

// The archives named *SUFFIX under SRC that the copy lacks at REL, keeping their folders.
static void enumerate_walk(EnumerateCopy &e, const std::string &src, const std::string &rel, int depth) {
    DIR *d = depth < 16 ? opendir(src.c_str()) : nullptr;
    if (!d) {
        if (depth == 0) enumerate_say("song folder not there, nothing copied from it", src);
        return;
    }
    size_t slen = strlen(e.suffix);
    while (struct dirent *entry = readdir(d)) {
        if (e.stop) break;
        if (entry->d_name[0] == '.') continue;
        std::string from = src + "/" + entry->d_name, to = rel + "/" + entry->d_name;
        struct stat st;
        if (stat(from.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            enumerate_walk(e, from, to, depth + 1);
            continue;
        }
        size_t n = strlen(entry->d_name);
        if (!S_ISREG(st.st_mode) || n < slen || strcasecmp(entry->d_name + n - slen, e.suffix) != 0) continue;
        if (access((e.game_dir + "/" + to).c_str(), F_OK) != 0) enumerate_one(e, from, to, st);
    }
    closedir(d);
}

// Brings the song archives the copy GAME_DIR lacks into its dlc/. Returns how many; 0 too for a folder that is
// not such a copy (no source.json), whose dlc/ the game scans as it is.
static int enumerate_sync(const std::string &game_dir) {
    std::string edition;
    std::vector<std::string> folders;
    if (!enumerate_source(enumerate_read(game_dir + "/.rearmed/source.json"), &edition, &folders)) return 0;
    struct stat st;
    if (stat(game_dir.c_str(), &st) != 0) return 0;
    EnumerateCopy e = {game_dir, edition == "mac" ? "_m.psarc" : "_p.psarc", st.st_dev, 0, false};
    for (const std::string &f : folders) enumerate_walk(e, f, "dlc", 0);
    return e.copied;
}

static std::atomic<bool> g_enumerate_busy{false};
static std::atomic<time_t> g_enumerate_last{0};

struct EnumerateJob {
    std::string game_dir;
    void (*done)(void);
};

static void *enumerate_thread(void *arg) {
    EnumerateJob *job = (EnumerateJob *)arg;
    int copied = enumerate_sync(job->game_dir);
    shim_log("enumerate: %d new archive(s) in the copy, scanning dlc/", copied);
    job->done();
    g_enumerate_last = time(nullptr);
    g_enumerate_busy = false;
    delete job;
    return nullptr;
}

// The press. DONE runs on the job's thread when the copy is up to date. One press can reach here more than once
// (the game has a Shop check in the menu and in its base class), so a press during a job or within two seconds of
// one is the same press. RS_MODS_OVERLAY=0 is the game as it was, without the entry.
static void enumerate_press(const std::string &game_dir, void (*done)(void)) {
    const char *overlay = getenv("RS_MODS_OVERLAY");  // the switch as mods.c and win32_overrides.c read it
    if (overlay && *overlay && strchr("0nNfF", *overlay)) return;
    if (time(nullptr) - g_enumerate_last < 2 || g_enumerate_busy.exchange(true)) return;
    shim_log("enumerate: pressed");
    pthread_t t;
    EnumerateJob *job = new EnumerateJob{game_dir, done};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &attr, enumerate_thread, job) != 0) {
        delete job;
        g_enumerate_busy = false;
    }
    pthread_attr_destroy(&attr);
}

#endif
