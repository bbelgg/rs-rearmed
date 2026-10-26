/*
 * dump_decrypted.c — dylib loaded into Rocksmith via LC_LOAD_DYLIB or
 * DYLD_INSERT_LIBRARIES. A constructor thread polls __text until the
 * Steam DRM has decrypted it, then writes the decrypted bytes to disk.
 *
 * Build:  clang -arch x86_64 -shared -o dump_decrypted.dylib dump_decrypted.c
 *         install_name_tool -id @executable_path/dump_decrypted.dylib dump_decrypted.dylib
 * Install: dump-decrypted.sh adds an LC_LOAD_WEAK_DYLIB to the game binary
 *          (same method as librsmacfixes) and copies the dylib into MacOS/.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <mach-o/getsect.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>

/* The encrypted __text starts with these bytes on disk. */
static const unsigned char ENCRYPTED_MAGIC[] = { 0x11, 0xe1, 0xcf, 0x98 };

/* The DRM stub header magic. */
static const unsigned char STUB_MAGIC[] = { 0x65, 0x6f, 0x6a, 0x78, 0x7b };

static const char *OUTPUT_PATH = "build/steamless/text_decrypted.bin";

static void *dump_thread(void *arg) {
    (void)arg;

    /* Find __text section in the main executable. */
    unsigned long size = 0;
    const struct mach_header_64 *hdr =
        (const struct mach_header_64 *)_dyld_get_image_header(0);
    const uint8_t *text = getsectiondata(hdr, "__TEXT", "__text", &size);
    if (!text || size == 0) {
        fprintf(stderr, "[dump_decrypted] __text section not found\n");
        return NULL;
    }

    fprintf(stderr, "[dump_decrypted] __text at %p, size %lu (0x%lx)\n",
            text, size, size);

    /* Poll until decrypted — check that the first 4 bytes are no longer
       the encrypted magic AND no longer the stub magic. */
    int waited = 0;
    while (memcmp(text, ENCRYPTED_MAGIC, 4) == 0 ||
           memcmp(text, STUB_MAGIC, 5) == 0) {
        usleep(100000);  /* 100ms */
        waited++;
        if (waited % 50 == 0) {
            fprintf(stderr, "[dump_decrypted] waiting for decryption... (%ds, first bytes: %02x %02x %02x %02x)\n",
                    waited / 10, text[0], text[1], text[2], text[3]);
        }
        if (waited > 600) {  /* 60 seconds */
            fprintf(stderr, "[dump_decrypted] timeout waiting for decryption\n");
            return NULL;
        }
    }

    fprintf(stderr, "[dump_decrypted] __text decrypted! first bytes: %02x %02x %02x %02x\n",
            text[0], text[1], text[2], text[3]);

    /* Small delay to ensure decryption is fully complete. */
    usleep(500000);

    FILE *f = fopen(OUTPUT_PATH, "wb");
    if (!f) {
        fprintf(stderr, "[dump_decrypted] cannot open %s: %m\n", OUTPUT_PATH);
        /* Try fallback path */
        const char *fallback = "/tmp/rs_text_decrypted.bin";
        f = fopen(fallback, "wb");
        if (!f) {
            fprintf(stderr, "[dump_decrypted] cannot open fallback %s either\n", fallback);
            return NULL;
        }
        fprintf(stderr, "[dump_decrypted] using fallback: %s\n", fallback);
    }

    size_t written = fwrite(text, 1, size, f);
    fclose(f);

    fprintf(stderr, "[dump_decrypted] wrote %zu bytes to disk\n", written);
    if (written == size) {
        fprintf(stderr, "[dump_decrypted] SUCCESS — dump complete\n");
    } else {
        fprintf(stderr, "[dump_decrypted] WARNING: wrote %zu of %lu bytes\n", written, size);
    }

    return NULL;
}

__attribute__((constructor))
static void dump_init(void) {
    fprintf(stderr, "[dump_decrypted] loaded, spawning dump thread\n");
    pthread_t thr;
    pthread_create(&thr, NULL, dump_thread, NULL);
    pthread_detach(thr);
}
