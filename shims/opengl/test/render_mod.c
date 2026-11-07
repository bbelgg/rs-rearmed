// A mod for checking the render API in a game run (rs_render.h, docs/mac/TESTING.md): it replaces the two ramps the
// notes take their colors from, in either edition, with hues that turn once a second. Not part of an install.
//   clang -arch arm64 -dynamiclib shims/opengl/test/render_mod.c -o render_mod.dylib
// Put it in the mods folder, or a folder with RS_MODS_PATH.
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../include/rs_render.h"

// Learn & Play loads no library without it (rc_mods.c). This one has no game address in it.
__attribute__((used, visibility("default"))) const int rc_mod_abi = 1;

enum { W = 256, H = 128, ROWS = 16 };

static rs_tex_rule *(*match)(int, int, uint64_t);
static int (*replace)(rs_tex_rule *, int, int, const void *);
static rs_tex_rule *rules[2];

static void hue(float h, float v, uint8_t *rgba) {
    h = fmodf(h, 360.0f) / 60.0f;
    float x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);
    float c[6][3] = {{1, x, 0}, {x, 1, 0}, {0, 1, x}, {0, x, 1}, {x, 0, 1}, {1, 0, x}};
    const float *p = c[(int)h % 6];
    for (int i = 0; i < 3; i++) rgba[i] = (uint8_t)(p[i] * v * 255.0f);
    rgba[3] = 255;
}

static void *turn(void *arg) {
    static uint8_t px[W * H * 4];
    for (int t = 0;; t++) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) hue((float)(y / (H / ROWS) * 45 + t * 30), (float)x / (W - 1), px + (y * W + x) * 4);
        for (int i = 0; i < 2; i++) replace(rules[i], W, H, px);
        sleep(1);
    }
    return arg;
}

__attribute__((constructor)) static void init(void) {
    int (*version)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "rs_render_api_version");
    const char *(*target)(void) = (const char *(*)(void))dlsym(RTLD_DEFAULT, "rc_target");
    match = (rs_tex_rule * (*)(int, int, uint64_t)) dlsym(RTLD_DEFAULT, "rs_render_match");
    replace = (int (*)(rs_tex_rule *, int, int, const void *))dlsym(RTLD_DEFAULT, "rs_render_replace");
    if (!version || version() < 1 || !target || !match || !replace) {
        fprintf(stderr, "[render_mod] no render API\n");
        return;
    }
    int lp = !strcmp(target(), "win-x86");
    rules[0] = match(W, H, lp ? 0x1c62a63a0c96a903ull : 0x43f3a5e256055935ull);
    rules[1] = match(W, H, lp ? 0xf14add8066964ed2ull : 0xa849d63d48a49660ull);
    fprintf(stderr, "[render_mod] on %s, API %d\n", target(), version());
    pthread_t th;
    pthread_create(&th, NULL, turn, NULL);
    pthread_detach(th);
}
