// Drives the Bink shim the way a game does: open, register frame buffers, then
// DoFrame / GetTrackData / NextFrame for every frame.
//
//   bink_play [--io] [--realtime N] [--dump DIR] file.bk2
//
//   --io          open through BinkSetIO + BINKIOPROCESSOR with stdio callbacks
//                 (Rocksmith reads its videos out of video.psarc this way)
//   --realtime N  pace N frames with BinkWait and check the wall-clock time
//   --dump DIR    write frames at 10/50/90% as BMP (YUV -> RGB, BT.601) and the
//                 audio of track 0 as WAV
//
// Prints "OK" when every frame decoded and the pictures aren't blank.
#include "bink2_abi.h"

#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- stdio-backed BINKIO; the FILE* lives in the BINKIO's private area ---------
#define IO_FILE(io) (*(FILE **)((char *)(io) + 0x40))

static uint64_t io_read_header(BINKIO *io, int64_t off, void *dest, uint64_t size) {
    if (off >= 0) fseeko(IO_FILE(io), off, SEEK_SET);
    return fread(dest, 1, size, IO_FILE(io));
}
static uint64_t io_read_frame(BINKIO *io, uint32_t frame, int64_t off, void *dest, uint64_t size) {
    (void)frame;
    return io_read_header(io, off, dest, size);
}
static uint64_t io_get_buffer_size(BINKIO *io, uint64_t size) { (void)io; return size; }
static void io_set_info(BINKIO *io, void *buf, uint64_t bufsize, uint64_t filesize, uint32_t sim) {
    (void)io; (void)buf; (void)bufsize; (void)filesize; (void)sim;
}
static uint32_t io_idle(BINKIO *io) { (void)io; return 0; }
static int io_closes;
static void io_close(BINKIO *io) { fclose(IO_FILE(io)); io_closes++; }
static int32_t io_bg(BINKIO *io, uint32_t c) { (void)io; (void)c; return 0; }

static int32_t io_open(BINKIO *io, const char *name, uint32_t flags) {
    (void)flags;
    FILE *f = fopen(name, "rb");
    if (!f) return 0;
    IO_FILE(io) = f;
    io->ReadHeader = io_read_header;
    io->ReadFrame = io_read_frame;
    io->GetBufferSize = io_get_buffer_size;
    io->SetInfo = io_set_info;
    io->Idle = io_idle;
    io->Close = io_close;
    io->BGControl = io_bg;
    return 1;
}

// --- output helpers ---------------------------------------------------------------
static uint8_t clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

static void write_bmp(const char *path, const BINKFRAMEPLANESET *set, uint32_t w, uint32_t h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t row = (w * 3 + 3) & ~3u, size = 54 + row * h;
    uint8_t hdr[54] = {'B', 'M'};
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    uint8_t *line = calloc(1, row);
    for (int y = (int)h - 1; y >= 0; y--) {
        const uint8_t *Y = (const uint8_t *)set->YPlane.Buffer + y * set->YPlane.BufferPitch;
        const uint8_t *Cb = (const uint8_t *)set->cBPlane.Buffer + (y / 2) * set->cBPlane.BufferPitch;
        const uint8_t *Cr = (const uint8_t *)set->cRPlane.Buffer + (y / 2) * set->cRPlane.BufferPitch;
        for (uint32_t x = 0; x < w; x++) {
            int c = (Y[x] - 16) * 298, d = Cb[x / 2] - 128, e = Cr[x / 2] - 128;
            line[x * 3 + 0] = clamp((c + 516 * d + 128) >> 8);
            line[x * 3 + 1] = clamp((c - 100 * d - 208 * e + 128) >> 8);
            line[x * 3 + 2] = clamp((c + 409 * e + 128) >> 8);
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
}

static void write_wav(const char *path, const uint8_t *pcm, uint32_t len, uint32_t rate, uint32_t ch) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t v;
    uint16_t s;
    fwrite("RIFF", 1, 4, f); v = 36 + len; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f); s = (uint16_t)ch; fwrite(&s, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * ch * 2; fwrite(&v, 4, 1, f);
    s = (uint16_t)(ch * 2); fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&len, 4, 1, f);
    fwrite(pcm, 1, len, f);
    fclose(f);
}

static double seconds(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}

int main(int argc, char **argv) {
    int use_io = 0, realtime = 0;
    const char *dump = NULL, *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--io")) use_io = 1;
        else if (!strcmp(argv[i], "--realtime") && i + 1 < argc) realtime = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "usage: %s [--io] [--realtime N] [--dump DIR] file.bk2\n", argv[0]);
        return 2;
    }

    uint32_t flags = 0;
    if (use_io) {
        BinkSetIO(io_open);
        flags |= BINKIOPROCESSOR;
    }
    HBINK b = BinkOpen(path, flags);
    if (!b) {
        printf("FAIL: BinkOpen: %s\n", BinkGetError());
        return 1;
    }
    printf("%ux%u, %u frames @ %u/%u fps, %u audio track(s), codec %.4s\n", b->Width, b->Height, b->Frames,
           b->FrameRate, b->FrameRateDiv, b->NumTracks, (const char *)&b->Codec);
    if (use_io && io_closes != 1) {
        printf("FAIL: expected the game I/O to be closed once after loading (got %d)\n", io_closes);
        return 1;
    }

    static BINKFRAMEBUFFERS fb;
    BinkGetFrameBuffersInfo(b, &fb);
    printf("frame buffers: %d sets, Y %ux%u, CrCb %ux%u\n", fb.TotalFrames, fb.YABufferWidth, fb.YABufferHeight,
           fb.cRcBBufferWidth, fb.cRcBBufferHeight);
    for (int i = 0; i < fb.TotalFrames; i++) {
        BINKPLANE *planes[] = {&fb.Frames[i].YPlane, &fb.Frames[i].cRPlane, &fb.Frames[i].cBPlane,
                               &fb.Frames[i].APlane, &fb.Frames[i].HPlane};
        for (int p = 0; p < 5; p++) {
            if (!planes[p]->Allocate) continue;
            int chroma = p == 1 || p == 2;
            uint32_t w = chroma ? fb.cRcBBufferWidth : fb.YABufferWidth;
            uint32_t h = chroma ? fb.cRcBBufferHeight : fb.YABufferHeight;
            planes[p]->BufferPitch = w;
            planes[p]->Buffer = calloc(w, h);
        }
    }
    BinkRegisterFrameBuffers(b, &fb);

    HBINKTRACK tr = b->NumTracks ? BinkOpenTrack(b, 0) : NULL;
    uint8_t *audio = NULL;
    size_t audio_len = 0;
    if (tr) {
        printf("track 0: id %u, %u Hz, %u ch, max %u bytes/frame\n", BinkGetTrackID(b, 0), tr->Frequency,
               tr->Channels, tr->MaxSize);
        audio = malloc((size_t)tr->MaxSize * (b->Frames + 1));
    }

    uint32_t frames = realtime ? realtime : b->Frames, decoded = 0, blank = 0;
    uint32_t snap[3] = {b->Frames / 10, b->Frames / 2, b->Frames * 9 / 10};
    double t0 = seconds();
    for (uint32_t n = 0; n < frames; n++) {
        if (realtime)
            while (BinkWait(b)) {}
        uint32_t cur = b->FrameNum;
        BinkDoFrame(b);
        const BINKFRAMEPLANESET *set = &fb.Frames[fb.FrameNum];
        // A frame counts as decoded if the luma plane isn't one flat value.
        const uint8_t *Y = set->YPlane.Buffer;
        uint8_t lo = 255, hi = 0;
        for (uint32_t y = 0; y < b->Height; y += 7)
            for (uint32_t x = 0; x < b->Width; x += 7) {
                uint8_t v = Y[y * set->YPlane.BufferPitch + x];
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }
        decoded++;
        if (hi - lo < 4) blank++;
        if (tr) audio_len += BinkGetTrackData(tr, audio + audio_len, tr->MaxSize);
        if (dump)
            for (int k = 0; k < 3; k++)
                if (cur == snap[k] + 1) {
                    char p[1024];
                    snprintf(p, sizeof p, "%s/frame%05u.bmp", dump, cur);
                    write_bmp(p, set, b->Width, b->Height);
                }
        if (!realtime && cur == b->Frames) {
            BinkNextFrame(b);
            if (b->FrameNum != 1) {
                printf("FAIL: no loop back to frame 1 after the last frame\n");
                return 1;
            }
            break;
        }
        BinkNextFrame(b);
        if (b->FrameNum != cur + 1) {
            printf("FAIL: FrameNum %u after %u\n", b->FrameNum, cur);
            return 1;
        }
    }
    double dt = seconds() - t0;

    BINKSUMMARY sum;
    BinkGetSummary(b, &sum);
    printf("decoded %u frames in %.2fs (%.0f fps), %u blank; summary says %u played; audio %zu bytes\n", decoded,
           dt, decoded / dt, blank, sum.TotalPlayedFrames, audio_len);

    int ok = decoded == frames && blank < decoded / 4 && sum.TotalPlayedFrames == decoded;
    if (realtime) {
        double expect = (double)(frames - 1) * b->FrameRateDiv / b->FrameRate;
        printf("realtime: %.3fs for %u frames, expected %.3fs\n", dt, frames, expect);
        ok = ok && dt > expect * 0.95 && dt < expect + 0.1;
    }
    if (tr) {
        double secs = (double)audio_len / (tr->Frequency * tr->Channels * 2);
        double vsecs = (double)decoded * b->FrameRateDiv / b->FrameRate;
        printf("audio: %.2fs vs video %.2fs\n", secs, vsecs);
        if (!realtime) ok = ok && secs > vsecs * 0.9 && secs < vsecs * 1.1;
        if (dump) {
            char p[1024];
            snprintf(p, sizeof p, "%s/track0.wav", dump);
            write_wav(p, audio, (uint32_t)audio_len, tr->Frequency, tr->Channels);
        }
        BinkCloseTrack(tr);
    }
    BinkClose(b);
    printf("%s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
