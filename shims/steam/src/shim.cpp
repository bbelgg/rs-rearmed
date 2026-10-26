// libsteam_api.dylib replacement for Rocksmith 2014 (arm64 + x86_64).
//
// Two backends:
//   real - forward to Valve's universal libsteam_api.dylib (talks to the
//          Steam client). Modern Valve builds no longer export
//          SteamAPI_Init, so we map it onto SteamAPI_InitSafe.
//   stub - offline implementation: mock interfaces, local callback
//          dispatch, Steam Cloud backed by a local directory.
// Mode "auto" (default) uses real when Valve's library loads and
// SteamAPI_Init succeeds through it, else falls back to stub.

#include "shim.h"

#include <dlfcn.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <vector>

// ---------------------------------------------------------------------------
// Config

static ShimConfig g_cfg;
static pthread_once_t g_cfg_once = PTHREAD_ONCE_INIT;

static std::string dirname_of(const std::string &p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

static bool file_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

static std::string read_file(const std::string &p) {
    std::string out;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// Minimal VDF tokenizer: quoted strings and braces.
static std::vector<std::string> vdf_tokens(const std::string &text) {
    std::vector<std::string> t;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '{' || c == '}') {
            t.push_back(std::string(1, c));
        } else if (c == '"') {
            std::string s;
            for (i++; i < text.size() && text[i] != '"'; i++) {
                if (text[i] == '\\' && i + 1 < text.size()) i++;
                s += text[i];
            }
            t.push_back(s);
        }
    }
    return t;
}

// Picks the most recent account from Steam's loginusers.vdf.
static void load_login_user(const std::string &steam_root, CSteamID *id, std::string *persona) {
    std::vector<std::string> t = vdf_tokens(read_file(steam_root + "/config/loginusers.vdf"));
    CSteamID first = 0;
    std::string first_name;
    for (size_t i = 0; i + 1 < t.size(); i++) {
        if (t[i].size() != 17 || t[i].compare(0, 4, "7656") != 0 || t[i + 1] != "{") continue;
        CSteamID sid = strtoull(t[i].c_str(), nullptr, 10);
        std::string name;
        bool recent = false;
        size_t j = i + 2;
        for (; j + 1 < t.size() && t[j] != "}"; j += 2) {
            if (t[j] == "PersonaName") name = t[j + 1];
            if (t[j] == "MostRecent" && t[j + 1] == "1") recent = true;
        }
        if (!first) { first = sid; first_name = name; }
        if (recent) { *id = sid; *persona = name; return; }
        i = j;
    }
    if (first) { *id = first; *persona = first_name; }
}

static std::string appmanifest_language(const ShimConfig &c) {
    std::string manifest = dirname_of(c.game_dir);  // steamapps/common
    manifest = dirname_of(manifest) + "/appmanifest_" + std::to_string(c.app_id) + ".acf";
    std::vector<std::string> t = vdf_tokens(read_file(manifest));
    for (size_t i = 0; i + 1 < t.size(); i++)
        if (t[i] == "language" && !t[i + 1].empty() && t[i + 1] != "{") return t[i + 1];
    return std::string();
}

static void load_config() {
    ShimConfig &c = g_cfg;
    const char *home = getenv("HOME");
    c.steam_root = std::string(home ? home : "") + "/Library/Application Support/Steam";

    char exe[PATH_MAX];
    uint32_t sz = sizeof exe;
    std::string exe_path = _NSGetExecutablePath(exe, &sz) == 0 ? exe : ".";
    size_t app = exe_path.rfind(".app/Contents/MacOS/");
    c.game_dir = app != std::string::npos ? dirname_of(exe_path.substr(0, app + 4)) : dirname_of(exe_path);
    std::string exe_dir = dirname_of(exe_path);

    Dl_info info;
    std::string self_dir = exe_dir;
    if (dladdr((void *)&load_config, &info) && info.dli_fname) self_dir = dirname_of(info.dli_fname);

    // steam_shim.ini next to this dylib: key=value lines, '#' comments.
    std::map<std::string, std::string> ini;
    std::string text = read_file(self_dir + "/steam_shim.ini");
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        line = trim(line);
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || line[0] == ';' || eq == std::string::npos) continue;
        ini[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    auto opt = [&](const char *env, const char *key) -> std::string {
        const char *e = getenv(env);
        if (e && *e) return e;
        auto it = ini.find(key);
        return it != ini.end() ? it->second : std::string();
    };

    std::string v;
    c.log = !(v = opt("RS_STEAM_LOG", "log")).empty() && v != "0";
    c.mode = opt("RS_STEAM_MODE", "mode");
    if (c.mode != "real" && c.mode != "stub") c.mode = "auto";

    if (!(v = opt("SteamAppId", "app_id")).empty()) {
        c.app_id = (AppId_t)strtoul(v.c_str(), nullptr, 10);
    } else {
        std::string a = trim(read_file(exe_dir + "/steam_appid.txt"));
        if (!a.empty()) c.app_id = (AppId_t)strtoul(a.c_str(), nullptr, 10);
    }

    load_login_user(c.steam_root, &c.steam_id, &c.persona);
    if (!(v = opt("RS_STEAM_ID", "steam_id")).empty()) c.steam_id = strtoull(v.c_str(), nullptr, 10);
    if (!c.steam_id) c.steam_id = 76561197960265729ULL;  // account 1, public individual
    if (!(v = opt("RS_STEAM_PERSONA", "persona")).empty()) c.persona = v;
    if (c.persona.empty()) c.persona = "Player";

    c.language = opt("RS_STEAM_LANGUAGE", "language");
    if (c.language.empty()) c.language = appmanifest_language(c);
    if (c.language.empty()) c.language = "english";

    c.data_dir = opt("RS_STEAM_DATA_DIR", "data_dir");
    if (c.data_dir.empty())
        c.data_dir = std::string(home ? home : "/tmp") + "/Library/Application Support/RocksmithSteamShim/userdata";

    c.real_lib = opt("RS_STEAM_REAL_LIB", "real_lib");
    if (c.real_lib.empty()) {
        const std::string candidates[] = {
            self_dir + "/libsteam_api_valve.dylib",
            c.steam_root + "/Steam.AppBundle/Steam/Contents/MacOS/Frameworks/Steam Helper.app/Contents/MacOS/libsteam_api.dylib",
        };
        for (const std::string &p : candidates)
            if (file_exists(p)) { c.real_lib = p; break; }
    }
}

const ShimConfig &shim_config() {
    pthread_once(&g_cfg_once, load_config);
    return g_cfg;
}

void shim_log(const char *fmt, ...) {
    if (!shim_config().log) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[steam-shim] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// Real backend (Valve's library)

struct RealApi {
    void *handle = nullptr;
    bool (*Init)() = nullptr;
    int (*InternalInit)(const char *, char *) = nullptr;  // SteamInternal_SteamAPI_Init, 0 == OK
    void (*Shutdown)() = nullptr;
    void (*RunCallbacks)() = nullptr;
    bool (*RestartAppIfNecessary)(uint32_t) = nullptr;
    HSteamUser (*GetHSteamUser)() = nullptr;
    void (*RegisterCallback)(void *, int) = nullptr;
    void (*UnregisterCallback)(void *) = nullptr;
    void (*RegisterCallResult)(void *, SteamAPICall_t) = nullptr;
    void (*UnregisterCallResult)(void *, SteamAPICall_t) = nullptr;
    void *(*ContextInit)(void *) = nullptr;
    void *(*CreateInterface)(const char *) = nullptr;
    void *(*FindOrCreateUserInterface)(HSteamUser, const char *) = nullptr;
};

static RealApi g_real;
static pthread_once_t g_real_once = PTHREAD_ONCE_INIT;

enum Backend { BACKEND_STUB, BACKEND_REAL };
static Backend g_backend = BACKEND_STUB;

static void load_real() {
    const ShimConfig &c = shim_config();
    if (c.mode == "stub" || c.real_lib.empty()) return;
    void *h = dlopen(c.real_lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        shim_log("real library %s failed to load: %s", c.real_lib.c_str(), dlerror());
        return;
    }
    RealApi r;
    r.handle = h;
#define SYM(field, name) r.field = (decltype(r.field))dlsym(h, name)
    SYM(Init, "SteamAPI_Init");
    if (!r.Init) SYM(Init, "SteamAPI_InitSafe");
    SYM(InternalInit, "SteamInternal_SteamAPI_Init");
    SYM(Shutdown, "SteamAPI_Shutdown");
    SYM(RunCallbacks, "SteamAPI_RunCallbacks");
    SYM(RestartAppIfNecessary, "SteamAPI_RestartAppIfNecessary");
    SYM(GetHSteamUser, "SteamAPI_GetHSteamUser");
    SYM(RegisterCallback, "SteamAPI_RegisterCallback");
    SYM(UnregisterCallback, "SteamAPI_UnregisterCallback");
    SYM(RegisterCallResult, "SteamAPI_RegisterCallResult");
    SYM(UnregisterCallResult, "SteamAPI_UnregisterCallResult");
    SYM(ContextInit, "SteamInternal_ContextInit");
    SYM(CreateInterface, "SteamInternal_CreateInterface");
    SYM(FindOrCreateUserInterface, "SteamInternal_FindOrCreateUserInterface");
#undef SYM
    if (!(r.Init || r.InternalInit) || !r.Shutdown || !r.RunCallbacks || !r.RestartAppIfNecessary ||
        !r.GetHSteamUser || !r.RegisterCallback || !r.UnregisterCallback || !r.RegisterCallResult ||
        !r.UnregisterCallResult || !r.ContextInit || !r.CreateInterface || !r.FindOrCreateUserInterface) {
        shim_log("real library %s is missing required exports", c.real_lib.c_str());
        dlclose(h);
        return;
    }
    g_real = r;
    g_backend = BACKEND_REAL;
    shim_log("loaded real library %s", c.real_lib.c_str());
}

static bool use_real() {
    pthread_once(&g_real_once, load_real);
    return g_backend == BACKEND_REAL;
}

// ---------------------------------------------------------------------------
// Stub backend: callback registry

// CCallbackBase layout from steam_api.h (Itanium ABI, no virtual dtor):
//   vtbl[0] Run(void *pvParam)
//   vtbl[1] Run(void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall)
//   vtbl[2] GetCallbackSizeBytes()
struct CCallbackBase {
    void **vtbl;
    uint8_t m_nCallbackFlags;
    int32_t m_iCallback;
};
enum { k_ECallbackFlagsRegistered = 0x01, k_ECallbackFlagsGameServer = 0x02 };
typedef void (*RunFn)(CCallbackBase *, void *);
typedef void (*RunResultFn)(CCallbackBase *, void *, bool, SteamAPICall_t);

struct Pending {
    int id;
    std::vector<uint8_t> data;
};

static std::recursive_mutex g_cb_mutex;
static std::vector<CCallbackBase *> g_callbacks;
static std::multimap<SteamAPICall_t, CCallbackBase *> g_call_handlers;
static std::vector<Pending> g_pending;
static std::map<SteamAPICall_t, Pending> g_results;
static SteamAPICall_t g_next_call = 1;
static uintptr_t g_init_counter = 0;

void shim_post_callback(int id, const void *data, size_t size) {
    std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
    const uint8_t *p = (const uint8_t *)data;
    g_pending.push_back(Pending{id, std::vector<uint8_t>(p, p + size)});
}

SteamAPICall_t shim_post_call_result(int id, const void *data, size_t size, bool embed_handle) {
    std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
    SteamAPICall_t h = g_next_call++;
    const uint8_t *p = (const uint8_t *)data;
    Pending &r = g_results[h] = Pending{id, std::vector<uint8_t>(p, p + size)};
    if (embed_handle) memcpy(r.data.data(), &h, sizeof h);
    return h;
}

bool shim_call_completed(SteamAPICall_t h, bool *failed) {
    std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
    bool done = g_results.count(h) != 0;
    if (failed) *failed = !done;
    return done;
}

bool shim_call_result(SteamAPICall_t h, void *out, int cub, int expected_id, bool *failed) {
    std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
    auto it = g_results.find(h);
    if (it == g_results.end() || it->second.id != expected_id || cub < (int)it->second.data.size()) {
        if (failed) *failed = true;
        return false;
    }
    memcpy(out, it->second.data.data(), it->second.data.size());
    g_results.erase(it);
    if (failed) *failed = false;
    return true;
}

static bool callback_registered(CCallbackBase *cb) {
    for (CCallbackBase *c : g_callbacks)
        if (c == cb) return true;
    return false;
}

static void stub_run_callbacks() {
    std::vector<Pending> callbacks;
    std::vector<std::pair<SteamAPICall_t, Pending>> results;
    {
        std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
        callbacks.swap(g_pending);
        for (auto it = g_results.begin(); it != g_results.end();) {
            if (g_call_handlers.count(it->first)) {
                results.push_back(*it);
                it = g_results.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (Pending &p : callbacks) {
        std::vector<CCallbackBase *> targets;
        {
            std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
            for (CCallbackBase *cb : g_callbacks)
                if (cb->m_iCallback == p.id && !(cb->m_nCallbackFlags & k_ECallbackFlagsGameServer))
                    targets.push_back(cb);
        }
        shim_log("dispatch callback %d to %zu listener(s)", p.id, targets.size());
        for (CCallbackBase *cb : targets) {
            {
                std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
                if (!callback_registered(cb)) continue;  // unregistered by an earlier listener
            }
            ((RunFn)cb->vtbl[0])(cb, p.data.data());
        }
    }
    for (auto &r : results) {
        std::vector<CCallbackBase *> targets;
        {
            std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
            auto range = g_call_handlers.equal_range(r.first);
            for (auto it = range.first; it != range.second; ++it) targets.push_back(it->second);
            g_call_handlers.erase(r.first);
        }
        shim_log("dispatch call result %llu (callback %d)", (unsigned long long)r.first, r.second.id);
        for (CCallbackBase *cb : targets) ((RunResultFn)cb->vtbl[1])(cb, r.second.data.data(), false, r.first);
    }
}

// ---------------------------------------------------------------------------
// Exports

S_EXPORT bool SteamAPI_RestartAppIfNecessary(uint32_t app_id) {
    if (use_real()) return g_real.RestartAppIfNecessary(app_id);
    shim_log("SteamAPI_RestartAppIfNecessary(%u) -> false", app_id);
    return false;
}

S_EXPORT bool SteamAPI_Init() {
    const ShimConfig &c = shim_config();
    if (use_real()) {
        bool ok;
        if (g_real.Init) {
            ok = g_real.Init();
        } else {
            char err[1024] = {0};
            ok = g_real.InternalInit(nullptr, err) == 0;
            if (!ok) shim_log("real SteamAPI_Init failed: %s", err);
        }
        shim_log("real SteamAPI_Init -> %d", ok);
        if (ok || c.mode == "real") return ok;
        shim_log("falling back to stub backend");
        g_backend = BACKEND_STUB;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
        g_init_counter++;
    }
    shim_stub_init();
    shim_log("stub SteamAPI_Init: app %u, user %llu (%s), language %s", c.app_id,
             (unsigned long long)c.steam_id, c.persona.c_str(), c.language.c_str());
    return true;
}

S_EXPORT bool SteamAPI_InitSafe() { return SteamAPI_Init(); }

S_EXPORT void SteamAPI_Shutdown() {
    if (g_backend == BACKEND_REAL) return g_real.Shutdown();
    shim_stub_shutdown();
}

S_EXPORT void SteamAPI_RunCallbacks() {
    if (g_backend == BACKEND_REAL) return g_real.RunCallbacks();
    stub_run_callbacks();
}

S_EXPORT HSteamUser SteamAPI_GetHSteamUser() {
    if (g_backend == BACKEND_REAL) return g_real.GetHSteamUser();
    return 1;
}

S_EXPORT HSteamPipe SteamAPI_GetHSteamPipe() { return 1; }
S_EXPORT bool SteamAPI_IsSteamRunning() { return true; }
S_EXPORT void SteamAPI_ReleaseCurrentThreadMemory() {}

S_EXPORT void SteamAPI_RegisterCallback(void *p, int id) {
    if (use_real()) return g_real.RegisterCallback(p, id);
    CCallbackBase *cb = (CCallbackBase *)p;
    std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
    cb->m_nCallbackFlags |= k_ECallbackFlagsRegistered;
    cb->m_iCallback = id;
    if (!callback_registered(cb)) g_callbacks.push_back(cb);
    shim_log("RegisterCallback(%p, %d)", p, id);
}

S_EXPORT void SteamAPI_UnregisterCallback(void *p) {
    if (use_real()) return g_real.UnregisterCallback(p);
    CCallbackBase *cb = (CCallbackBase *)p;
    std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
    cb->m_nCallbackFlags &= ~k_ECallbackFlagsRegistered;
    for (auto it = g_callbacks.begin(); it != g_callbacks.end(); ++it)
        if (*it == cb) { g_callbacks.erase(it); break; }
}

S_EXPORT void SteamAPI_RegisterCallResult(void *p, SteamAPICall_t h) {
    if (use_real()) return g_real.RegisterCallResult(p, h);
    std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
    g_call_handlers.insert(std::make_pair(h, (CCallbackBase *)p));
}

S_EXPORT void SteamAPI_UnregisterCallResult(void *p, SteamAPICall_t h) {
    if (use_real()) return g_real.UnregisterCallResult(p, h);
    std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
    auto range = g_call_handlers.equal_range(h);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second == (CCallbackBase *)p) { g_call_handlers.erase(it); break; }
}

// The SDK's inline accessors pass a static block laid out as
//   { void (*pFn)(void *ctx); uintptr_t counter; <context payload> }
// and expect a pointer to the payload back, (re)filled via pFn whenever
// SteamAPI_Init has run since the block was last initialized.
S_EXPORT void *SteamInternal_ContextInit(void *data) {
    if (use_real()) return g_real.ContextInit(data);
    struct InitData {
        void (*pFn)(void *ctx);
        uintptr_t counter;
        void *ctx[1];
    } *d = (InitData *)data;
    std::lock_guard<std::recursive_mutex> lock(g_cb_mutex);
    if (d->counter != g_init_counter) {
        d->pFn(d->ctx);
        d->counter = g_init_counter;
    }
    return d->ctx;
}

S_EXPORT void *SteamInternal_CreateInterface(const char *version) {
    if (use_real()) return g_real.CreateInterface(version);
    void *p = shim_find_interface(version);
    shim_log("CreateInterface(%s) -> %p", version ? version : "(null)", p);
    return p;
}

S_EXPORT void *SteamInternal_FindOrCreateUserInterface(HSteamUser user, const char *version) {
    if (use_real()) return g_real.FindOrCreateUserInterface(user, version);
    void *p = shim_find_interface(version);
    shim_log("FindOrCreateUserInterface(%d, %s) -> %p", user, version ? version : "(null)", p);
    return p;
}

S_EXPORT void *SteamInternal_FindOrCreateGameServerInterface(HSteamUser, const char *version) {
    return shim_find_interface(version);
}
