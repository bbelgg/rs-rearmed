// enumerate.h: source.json read, new song archives brought into a copy, and the press with its one callback.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include <atomic>
#include <string>

static void shim_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

#include "../src/enumerate.h"

#define EXPECT(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failed++; } } while (0)
static int failed;

static void write_file(const std::string &path, const std::string &text) {
    size_t cut = path.rfind('/');
    enumerate_mkdirs(path.substr(0, cut));
    FILE *f = fopen(path.c_str(), "wb");
    assert(f);
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

static bool exists(const std::string &path) { return access(path.c_str(), F_OK) == 0; }

static std::atomic<int> dones;
static void done() { dones++; }

int main() {
    char tmpl[] = "/tmp/enumerate-test-XXXXXX";
    std::string root = mkdtemp(tmpl);

    // source.json as gamefiles.py writes it: non-ASCII as \uXXXX, the keys in any order
    std::string edition;
    std::vector<std::string> folders;
    EXPECT(enumerate_source("{\n \"dlc_from\": [\n  \"/a/M\\u00e4ki\",\n  \"/b\\\\c\"\n ],\n \"edition\": \"learn-play\",\n"
                            " \"format\": 1,\n \"source\": \"/s\\ud83c\\udfb8t\"\n}\n", &edition, &folders));
    EXPECT(edition == "learn-play");
    EXPECT(folders.size() == 3 && folders[0] == "/s\xf0\x9f\x8e\xb8t/dlc" && folders[1] == "/a/M\xc3\xa4ki" &&
           folders[2] == "/b\\c");
    edition.clear();
    folders.clear();
    EXPECT(!enumerate_source("", &edition, &folders));
    EXPECT(!enumerate_source("{\"source\": \"/s\"", &edition, &folders));

    // a Mac copy: new _m archives come over, in their folders; what it has stays; the PC ones and others don't
    std::string game = root + "/game", steam = root + "/steam", extra = root + "/extra";
    write_file(game + "/.rearmed/source.json",
               "{\"dlc_from\": [\"" + extra + "\", \"" + root + "/gone\"], \"edition\": \"mac\", \"source\": \"" + steam + "\"}");
    write_file(game + "/dlc/old_m.psarc", "mine");
    write_file(steam + "/dlc/old_m.psarc", "theirs");
    write_file(steam + "/dlc/new_m.psarc", "new");
    write_file(steam + "/dlc/new_p.psarc", "pc");
    write_file(steam + "/dlc/sub/dir/Deep_M.PSARC", "deep");
    write_file(steam + "/dlc/.hidden_m.psarc", "no");
    write_file(steam + "/dlc/notes.txt", "no");
    write_file(extra + "/more_m.psarc", "more");
    EXPECT(enumerate_sync(game) == 3);
    EXPECT(enumerate_read(game + "/dlc/old_m.psarc") == "mine");
    EXPECT(enumerate_read(game + "/dlc/new_m.psarc") == "new");
    EXPECT(enumerate_read(game + "/dlc/sub/dir/Deep_M.PSARC") == "deep");
    EXPECT(enumerate_read(game + "/dlc/more_m.psarc") == "more");
    EXPECT(!exists(game + "/dlc/new_p.psarc") && !exists(game + "/dlc/notes.txt") && !exists(game + "/dlc/.hidden_m.psarc"));
    EXPECT(!exists(game + "/dlc/new_m.psarc.part"));
    EXPECT(enumerate_sync(game) == 0);  // nothing new the second time
    {   // for a refresh: the same time as the source, and each one named
        struct stat a, b;
        EXPECT(stat((steam + "/dlc/new_m.psarc").c_str(), &a) == 0 && stat((game + "/dlc/new_m.psarc").c_str(), &b) == 0);
        EXPECT(a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec);
        std::string names = enumerate_read(game + "/.rearmed/enumerated.txt");
        EXPECT(names.find("dlc/new_m.psarc\n") != std::string::npos);
        EXPECT(names.find("dlc/sub/dir/Deep_M.PSARC\n") != std::string::npos);
        EXPECT(names.find("dlc/more_m.psarc\n") != std::string::npos);
        EXPECT(names.find("old_m") == std::string::npos);
    }

    // Learn & Play takes the PC archives
    std::string lp = root + "/lp";
    write_file(lp + "/.rearmed/source.json", "{\"edition\": \"learn-play\", \"source\": \"" + steam + "\", \"dlc_from\": []}");
    EXPECT(enumerate_sync(lp) == 1 && exists(lp + "/dlc/new_p.psarc"));

    // not a copy: nothing to bring over
    EXPECT(enumerate_sync(root + "/nowhere") == 0);

    // the press: one job for a press that reaches here twice, DONE on its thread
    write_file(steam + "/dlc/later_m.psarc", "later");
    enumerate_press(game, done);
    enumerate_press(game, done);
    for (int i = 0; i < 500 && !dones; i++) usleep(10000);
    usleep(20000);
    EXPECT(dones == 1 && exists(game + "/dlc/later_m.psarc"));
    enumerate_press(game, done);  // still the same press
    usleep(50000);
    EXPECT(dones == 1);
    g_enumerate_last = 0;  // a later press
    setenv("RS_MODS_OVERLAY", "0", 1);  // the game as it was
    enumerate_press(game, done);
    usleep(50000);
    EXPECT(dones == 1);
    unsetenv("RS_MODS_OVERLAY");
    enumerate_press(game, done);
    for (int i = 0; i < 500 && dones < 2; i++) usleep(10000);
    EXPECT(dones == 2);

    system(("rm -rf '" + root + "'").c_str());
    if (!failed) puts("enumerate: ok");
    return failed != 0;
}
