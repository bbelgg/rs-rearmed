// libBink2Macx64.dylib replacement: the 16 Bink 2 entry points Rocksmith 2014
// imports, decoding with FFmpeg's bink demuxer and the (out-of-tree) bink2
// video / binkaudio decoders, statically linked.
//
// The whole .bk2 is loaded into memory at BinkOpen (through the game's BINKIO
// callbacks when it supplies them, like the original's BINKPRELOADALL path), and
// FFmpeg reads from that buffer. Frames are decoded strictly in order, as Bink 2
// inter frames need the previous one; skipping a frame (BinkNextFrame without
// BinkDoFrame) still decodes it, only the copy into the game's buffers is saved.
#include "bink2_abi.h"

#include <errno.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>

#define BINK_HEADER_SIZE 0x2c
#define BINK_MAX_TRACKS  32

// ---------------------------------------------------------------------------
// Globals set before BinkOpen (the original consumes and resets most of them).
static BINKIOOPEN g_io_open;
static uint64_t   g_io_size = UINT64_MAX;
static uint32_t   g_snd_track_count;
static uint32_t   g_snd_tracks[BINK_MAX_TRACKS];
static char       g_error[256];

static int g_debug = -1;
static void dbg(const char *fmt, ...) {
    if (g_debug < 0) g_debug = getenv("RS_BINK_DEBUG") != NULL;
    if (!g_debug) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("[bink-shim] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error, sizeof g_error, fmt, ap);
    va_end(ap);
    dbg("error: %s", g_error);
}

static uint64_t now_ns(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return mach_absolute_time() * tb.numer / tb.denom;
}

// ---------------------------------------------------------------------------
// Private state, allocated right after the public 0x6d0-byte BINK.

typedef struct TrackState {
    int       stream;        // FFmpeg stream index, -1 if none
    AVCodecContext *dec;
    int       open_count;    // BinkOpenTrack references
    uint8_t  *pcm;           // decoded s16 interleaved audio of the loaded frame
    size_t    pcm_len, pcm_cap, pcm_pos;
} TrackState;

typedef struct Shim {
    BINK            pub;     // must be first: HBINK points here
    pthread_mutex_t lock;

    uint8_t        *file;    // whole .bk2
    size_t          file_size;
    int64_t         avio_pos;

    AVFormatContext *fmt;
    AVIOContext     *avio;
    AVCodecContext  *vdec;
    int              vstream;
    AVPacket        *pkt;
    AVPacket        *vpkt;   // video packet of the loaded frame
    AVFrame         *frame;  // scratch (audio) frame
    AVFrame         *vframe; // decoded picture of the decoded frame

    uint32_t loaded_frame;   // frame whose packets are read (0 = none)
    uint32_t decoded_frame;  // frame whose picture is in vframe (0 = none)
    uint32_t copied_count;   // frames copied into the game's buffers

    TrackState tracks[BINK_MAX_TRACKS];
    uint32_t   track_max_sizes[BINK_MAX_TRACKS];
    uint32_t   track_types[BINK_MAX_TRACKS];
    uint32_t   track_ids[BINK_MAX_TRACKS];

    uint64_t open_ns;
    uint64_t clock_start_ns; // 0 = clock not running
    uint32_t clock_start_frame;
    uint32_t played_frames;
    uint32_t audio_bytes;       // BinkGetTrackData total, for the debug log
} Shim;

static Shim *shim_of(HBINK b) { return (Shim *)b; }

// ---------------------------------------------------------------------------
// Loading the file.

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t io_timer_ms(void) { return (uint32_t)(now_ns() / 1000000); }
static int64_t io_atomic_add(volatile int64_t *p, int64_t v) { return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }
static void io_copy(void *d, const void *s, uint64_t n) { memcpy(d, s, n); }

// Game-supplied BINKIO: open, read the header for the size, read everything
// in one go (as the original does for BINKPRELOADALL) and close.
static int load_via_bink_io(Shim *s, BINKIOOPEN open_fn, const char *name, uint32_t flags) {
    BINKIO *io = &s->pub.io;
    memset(io, 0, sizeof *io);
    io->timer_ms = io_timer_ms;
    io->atomic_add = io_atomic_add;
    io->copy = io_copy;
    if (!open_fn(io, name, flags)) {
        set_error("Error opening file.");
        return 0;
    }
    io->bink = &s->pub;
    uint8_t hdr[BINK_HEADER_SIZE];
    int ok = 0;
    if (io->ReadHeader && io->ReadHeader(io, 0, hdr, sizeof hdr) == sizeof hdr && !io->ReadError) {
        size_t size = (size_t)rd32(hdr + 4) + 8;
        if (io->SetInfo) io->SetInfo(io, NULL, 0, size, 0);
        s->file = malloc(size);
        if (s->file && io->ReadHeader(io, 0, s->file, size) == size && !io->ReadError) {
            s->file_size = size;
            ok = 1;
        }
    }
    if (!ok) set_error("Error reading Bink header.");
    if (io->Close) io->Close(io);
    return ok;
}

static int load_from_fd(Shim *s, int fd) {
    struct stat st;
    if (fstat(fd, &st) < 0) return 0;
    off_t start = lseek(fd, 0, SEEK_CUR);
    if (start < 0) start = 0;
    size_t size = (size_t)(st.st_size - start);
    s->file = malloc(size ? size : 1);
    if (!s->file) return 0;
    size_t got = 0;
    while (got < size) {
        ssize_t r = pread(fd, s->file + got, size - got, start + (off_t)got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    s->file_size = got;
    return got >= BINK_HEADER_SIZE;
}

static int load_file(Shim *s, const char *name, uint32_t flags, BINKIOOPEN io_open) {
    if ((flags & BINKIOPROCESSOR) && io_open)
        return load_via_bink_io(s, io_open, name, flags);
    if (flags & BINKFROMMEMORY) {
        size_t size = (size_t)rd32((const uint8_t *)name + 4) + 8;
        s->file = malloc(size);
        if (!s->file) return 0;
        memcpy(s->file, name, size);
        s->file_size = size;
        return 1;
    }
    if (flags & BINKFILEHANDLE)
        return load_from_fd(s, (int)(intptr_t)name);
    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        set_error("Error opening file.");
        return 0;
    }
    int ok = load_from_fd(s, fd);
    close(fd);
    return ok;
}

// ---------------------------------------------------------------------------
// FFmpeg plumbing.

static int mem_read(void *opaque, uint8_t *buf, int size) {
    Shim *s = opaque;
    int64_t left = (int64_t)s->file_size - s->avio_pos;
    if (left <= 0) return AVERROR_EOF;
    if (size > left) size = (int)left;
    memcpy(buf, s->file + s->avio_pos, (size_t)size);
    s->avio_pos += size;
    return size;
}

static int64_t mem_seek(void *opaque, int64_t off, int whence) {
    Shim *s = opaque;
    switch (whence & ~AVSEEK_FORCE) {
    case AVSEEK_SIZE: return (int64_t)s->file_size;
    case SEEK_SET: break;
    case SEEK_CUR: off += s->avio_pos; break;
    case SEEK_END: off += (int64_t)s->file_size; break;
    default: return AVERROR(EINVAL);
    }
    if (off < 0 || off > (int64_t)s->file_size) return AVERROR(EINVAL);
    s->avio_pos = off;
    return off;
}

static void close_demuxer(Shim *s) {
    avformat_close_input(&s->fmt);
    if (s->avio) {
        av_freep(&s->avio->buffer);
        avio_context_free(&s->avio);
    }
}

// (Re)starts demuxing from the first frame. Decoders are flushed, not recreated.
static int open_demuxer(Shim *s) {
    close_demuxer(s);
    s->avio_pos = 0;
    uint8_t *buf = av_malloc(64 * 1024);
    if (!buf) return 0;
    s->avio = avio_alloc_context(buf, 64 * 1024, 0, s, mem_read, NULL, mem_seek);
    s->fmt = avformat_alloc_context();
    if (!s->avio || !s->fmt) return 0;
    s->fmt->pb = s->avio;
    s->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&s->fmt, NULL, av_find_input_format("bink"), NULL) < 0) {
        set_error("Not a Bink file.");
        return 0;
    }
    s->loaded_frame = s->decoded_frame = 0;
    if (s->vdec) avcodec_flush_buffers(s->vdec);
    for (int i = 0; i < BINK_MAX_TRACKS; i++)
        if (s->tracks[i].dec) avcodec_flush_buffers(s->tracks[i].dec);
    return 1;
}

static AVCodecContext *open_decoder(AVStream *st) {
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return NULL;
    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c) return NULL;
    if (avcodec_parameters_to_context(c, st->codecpar) < 0 || avcodec_open2(c, codec, NULL) < 0) {
        avcodec_free_context(&c);
        return NULL;
    }
    return c;
}

static void append_audio(TrackState *t, const AVFrame *f) {
    int ch = f->ch_layout.nb_channels;
    size_t bytes = (size_t)f->nb_samples * ch * 2;
    if (t->pcm_len + bytes > t->pcm_cap) {
        size_t cap = (t->pcm_len + bytes) * 2;
        uint8_t *p = realloc(t->pcm, cap);
        if (!p) return;
        t->pcm = p;
        t->pcm_cap = cap;
    }
    int16_t *out = (int16_t *)(t->pcm + t->pcm_len);
    for (int i = 0; i < f->nb_samples; i++) {
        for (int c = 0; c < ch; c++) {
            float v;
            if (f->format == AV_SAMPLE_FMT_FLTP) v = ((const float *)f->extended_data[c])[i];
            else if (f->format == AV_SAMPLE_FMT_FLT) v = ((const float *)f->extended_data[0])[i * ch + c];
            else v = 0;
            int x = (int)(v * 32768.0f);
            *out++ = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
    }
    t->pcm_len += bytes;
}

// Reads the packets of frame n (must be loaded_frame + 1, or 1 to restart).
static int load_frame(Shim *s, uint32_t n) {
    if (n == s->loaded_frame) return 1;
    if (n == 1 && s->loaded_frame != 0 && !open_demuxer(s)) return 0;
    if (n != s->loaded_frame + 1) return 0;
    for (int i = 0; i < BINK_MAX_TRACKS; i++)
        s->tracks[i].pcm_len = s->tracks[i].pcm_pos = 0;
    av_packet_unref(s->vpkt);
    for (;;) {
        int r = av_read_frame(s->fmt, s->pkt);
        if (r < 0) {
            s->pub.ReadError = 1;
            return 0;
        }
        int si = s->pkt->stream_index;
        if (si == s->vstream) {
            av_packet_move_ref(s->vpkt, s->pkt);
            break;
        }
        for (uint32_t i = 0; i < s->pub.NumTracks; i++) {
            TrackState *t = &s->tracks[i];
            if (t->stream != si || !t->open_count || !t->dec) continue;
            if (avcodec_send_packet(t->dec, s->pkt) >= 0)
                while (avcodec_receive_frame(t->dec, s->frame) >= 0) {
                    append_audio(t, s->frame);
                    av_frame_unref(s->frame);
                }
        }
        av_packet_unref(s->pkt);
    }
    s->loaded_frame = n;
    return 1;
}

static int decode_frame(Shim *s, uint32_t n) {
    if (s->decoded_frame == n) return 1;
    if (!load_frame(s, n)) return 0;
    if (avcodec_send_packet(s->vdec, s->vpkt) < 0) return 0;
    av_frame_unref(s->vframe);
    if (avcodec_receive_frame(s->vdec, s->vframe) < 0) return 0;
    s->decoded_frame = n;
    return 1;
}

static void copy_plane(const BINKPLANE *p, const uint8_t *src, int src_pitch, uint32_t w, uint32_t h) {
    if (!p->Buffer || !src) return;
    uint8_t *dst = p->Buffer;
    uint32_t n = w < p->BufferPitch ? w : p->BufferPitch;
    for (uint32_t y = 0; y < h; y++)
        memcpy(dst + (size_t)y * p->BufferPitch, src + (size_t)y * src_pitch, n);
}

static void copy_to_framebuffers(Shim *s) {
    BINKFRAMEBUFFERS *fb = s->pub.FrameBuffers;
    if (!fb || fb->TotalFrames < 1) return;
    int total = fb->TotalFrames > BINK_MAX_FRAMEBUFFERS ? BINK_MAX_FRAMEBUFFERS : fb->TotalFrames;
    uint32_t idx = s->copied_count++ % (uint32_t)total;
    const BINKFRAMEPLANESET *set = &fb->Frames[idx];
    const AVFrame *f = s->vframe;
    uint32_t w = s->pub.Width, h = s->pub.Height;
    copy_plane(&set->YPlane, f->data[0], f->linesize[0], w, h);
    copy_plane(&set->cRPlane, f->data[2], f->linesize[2], (w + 1) / 2, (h + 1) / 2);  // V = Cr
    copy_plane(&set->cBPlane, f->data[1], f->linesize[1], (w + 1) / 2, (h + 1) / 2);  // U = Cb
    if (f->format == AV_PIX_FMT_YUVA420P)
        copy_plane(&set->APlane, f->data[3], f->linesize[3], w, h);
    fb->FrameNum = idx;
}

// ---------------------------------------------------------------------------
// Timing: frame n is due at clock_start + (n - clock_start_frame) frame periods.

static uint64_t frame_ns(const BINK *b) {
    return b->FrameRate ? (uint64_t)b->FrameRateDiv * 1000000000ull / b->FrameRate : 0;
}

static int64_t lateness_ns(Shim *s) {
    BINK *b = &s->pub;
    uint64_t due = s->clock_start_ns + (uint64_t)(b->FrameNum - s->clock_start_frame) * frame_ns(b);
    return (int64_t)(now_ns() - due);
}

// ---------------------------------------------------------------------------
// API

BINK *BinkOpen(const char *name, uint32_t flags) {
    BINKIOOPEN io_open = g_io_open;
    uint64_t io_size = g_io_size;
    g_io_open = NULL;
    g_io_size = UINT64_MAX;
    (void)io_size;  // whole file is buffered; the I/O buffer size hint is irrelevant
    g_error[0] = 0;
    if (!name) {
        set_error("Error opening file.");
        return NULL;
    }

    Shim *s;
    if (posix_memalign((void **)&s, 256, sizeof *s)) return NULL;
    memset(s, 0, sizeof *s);
    pthread_mutex_init(&s->lock, NULL);
    s->vstream = -1;
    for (int i = 0; i < BINK_MAX_TRACKS; i++) s->tracks[i].stream = -1;
    BINK *b = &s->pub;
    s->open_ns = now_ns();

    if (!load_file(s, name, flags, io_open)) goto fail;
    if (s->file_size < BINK_HEADER_SIZE) {
        set_error("Not a Bink file.");
        goto fail;
    }
    const uint8_t *h = s->file;
    if (!((h[0] == 'B' && h[1] == 'I' && h[2] == 'K') || (h[0] == 'K' && h[1] == 'B' && h[2] == '2'))) {
        set_error("Not a Bink file.");
        goto fail;
    }
    uint32_t file_flags = rd32(h + 0x24);
    b->Codec = rd32(h);
    b->Size = rd32(h + 4);
    b->Frames = rd32(h + 8);
    b->LargestFrameSize = rd32(h + 12);
    b->Width = rd32(h + 0x14);
    b->Height = rd32(h + 0x18);
    b->FrameRate = rd32(h + 0x1c);
    b->FrameRateDiv = rd32(h + 0x20);
    b->FileFlags = file_flags;
    uint32_t num_tracks = rd32(h + 0x28);
    if (!b->Frames || !b->Width || !b->Height || !b->FrameRate || !b->FrameRateDiv || num_tracks > 256) {
        set_error("The file has a corrupt header.");
        goto fail;
    }
    // OpenFlags as the library's open (0x49c0) builds them: the file's 0x20013 bits, alpha only
    // if the file has it, bit 2 from the file.
    b->OpenFlags = (flags & ~(BINKALPHA | 4u)) | (file_flags & 0x20013u) |
                   ((file_flags & BINKALPHA) ? (flags & BINKALPHA) : 0) | ((file_flags & 4u) ? 4u : 0);

    // Audio track tables: max sizes, then (u16 rate, u16 flags), then IDs.
    if (num_tracks > BINK_MAX_TRACKS) num_tracks = BINK_MAX_TRACKS;
    size_t off = BINK_HEADER_SIZE;
    char rev = (char)h[3];
    if ((h[0] == 'K' && (rev == 'i' || rev == 'j' || rev == 'k' || rev == 'n')) || (h[0] == 'B' && rev == 'k'))
        off += 4;
    else if (h[0] == 'K' && rev == 'm')
        off += 16;
    uint32_t file_tracks = rd32(h + 0x28);
    if (off + (size_t)file_tracks * 12 <= s->file_size) {
        for (uint32_t i = 0; i < num_tracks; i++) {
            s->track_max_sizes[i] = rd32(h + off + 4 * i);
            s->track_types[i] = rd32(h + off + 4 * file_tracks + 4 * i);
            s->track_ids[i] = rd32(h + off + 8 * file_tracks + 4 * i);
        }
    } else {
        num_tracks = 0;
    }
    b->NumTracks = num_tracks;
    b->TrackMaxSizes = s->track_max_sizes;
    b->TrackTypes = s->track_types;
    b->TrackIDs = s->track_ids;
    (void)g_snd_track_count;  // no built-in sound output: the game pulls audio with BinkGetTrackData

    s->pkt = av_packet_alloc();
    s->vpkt = av_packet_alloc();
    s->frame = av_frame_alloc();
    s->vframe = av_frame_alloc();
    if (!s->pkt || !s->vpkt || !s->frame || !s->vframe || !open_demuxer(s)) goto fail;

    for (unsigned i = 0; i < s->fmt->nb_streams; i++) {
        AVStream *st = s->fmt->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && s->vstream < 0) {
            s->vstream = (int)i;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // The demuxer creates one audio stream per track, in order.
            for (uint32_t t = 0; t < num_tracks; t++)
                if (s->tracks[t].stream < 0) {
                    s->tracks[t].stream = (int)i;
                    break;
                }
        }
    }
    if (s->vstream < 0 || !(s->vdec = open_decoder(s->fmt->streams[s->vstream]))) {
        set_error("No decoder for %.4s video.", (const char *)h);
        goto fail;
    }
    s->vdec->thread_count = 1;

    b->FrameNum = 1;
    dbg("opened %.4s %ux%u %u frames @ %u/%u fps, %u tracks, flags 0x%x%s", (const char *)h, b->Width, b->Height,
        b->Frames, b->FrameRate, b->FrameRateDiv, b->NumTracks, flags, io_open && (flags & BINKIOPROCESSOR) ? " (game I/O)" : "");
    return b;

fail:
    BinkClose(b);
    return NULL;
}

void BinkClose(BINK *b) {
    if (!b) return;
    Shim *s = shim_of(b);
    dbg("close: %u of %u frames played, %u audio bytes handed out", s->played_frames, b->Frames,
        s->audio_bytes);
    close_demuxer(s);
    avcodec_free_context(&s->vdec);
    for (int i = 0; i < BINK_MAX_TRACKS; i++) {
        avcodec_free_context(&s->tracks[i].dec);
        free(s->tracks[i].pcm);
    }
    av_packet_free(&s->pkt);
    av_packet_free(&s->vpkt);
    av_frame_free(&s->frame);
    av_frame_free(&s->vframe);
    free(s->file);
    pthread_mutex_destroy(&s->lock);
    free(s);
}

void BinkSetSoundTrack(uint32_t total, const uint32_t *tracks) {
    g_snd_track_count = total < BINK_MAX_TRACKS ? total : BINK_MAX_TRACKS;
    for (uint32_t i = 0; i < g_snd_track_count; i++) g_snd_tracks[i] = tracks ? tracks[i] : 0;
}

void BinkSetIO(BINKIOOPEN open) { g_io_open = open; }
void BinkSetIOSize(uint64_t size) { g_io_size = size; }

// RS_BINK_DUMP_DIR=<dir>: save the luma plane of a few played frames as PGM,
// read back from the game's own frame buffers (what it will upload).
static void dump_luma(Shim *s) {
    static const char *dir;
    static int init;
    if (!init) { dir = getenv("RS_BINK_DUMP_DIR"); init = 1; }
    uint32_t n = s->played_frames;
    BINKFRAMEBUFFERS *fb = s->pub.FrameBuffers;
    if (!dir || !fb || (n != 1 && n != 120 && n != 360)) return;
    const BINKPLANE *y = &fb->Frames[fb->FrameNum].YPlane;
    if (!y->Buffer) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/bink_%p_%04u.pgm", dir, (void *)s, n);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P5\n%u %u\n255\n", s->pub.Width, s->pub.Height);
    for (uint32_t r = 0; r < s->pub.Height; r++)
        fwrite((const uint8_t *)y->Buffer + (size_t)r * y->BufferPitch, 1, s->pub.Width, f);
    fclose(f);
    dbg("dumped %s", path);
}

int32_t BinkDoFrame(BINK *b) {
    if (!b) return 0;
    Shim *s = shim_of(b);
    pthread_mutex_lock(&s->lock);
    if (decode_frame(s, b->FrameNum)) {
        copy_to_framebuffers(s);
        s->played_frames++;
        if (s->played_frames % 96 == 1)
            dbg("frame %u/%u played (%u so far)", b->FrameNum, b->Frames, s->played_frames);
        dump_luma(s);
    }
    pthread_mutex_unlock(&s->lock);
    return 0;  // the original's DoFrame path always ends in "return 0" (flag 0x200)
}

void BinkNextFrame(BINK *b) {
    if (!b) return;
    Shim *s = shim_of(b);
    static unsigned calls;
    if (++calls <= 3 || calls % 96 == 0) dbg("next frame #%u (from %u)", calls, b->FrameNum);
    pthread_mutex_lock(&s->lock);
    decode_frame(s, b->FrameNum);  // keep the reference chain intact for skipped frames
    b->LastFrameNum = b->FrameNum;
    if (b->FrameNum < b->Frames) {
        b->FrameNum++;
    } else {
        b->FrameNum = 1;          // loops, like the original
        s->clock_start_ns = 0;
    }
    pthread_mutex_unlock(&s->lock);
}

int32_t BinkWait(BINK *b) {
    if (!b) return 0;
    Shim *s = shim_of(b);
    if (b->Paused) {
        static unsigned paused;
        if (++paused <= 3 || paused % 600 == 0) dbg("wait: paused at frame %u (#%u)", b->FrameNum, paused);
        return 1;
    }
    if (b->ReadError) return 0;
    pthread_mutex_lock(&s->lock);
    if (!s->clock_start_ns) {
        s->clock_start_ns = now_ns();
        s->clock_start_frame = b->FrameNum;
    }
    int64_t late = lateness_ns(s);
    if (late > 1000000000ll) {  // stalled for over a second: resync instead of racing
        s->clock_start_ns = now_ns();
        s->clock_start_frame = b->FrameNum;
        late = 0;
    }
    pthread_mutex_unlock(&s->lock);
    static unsigned calls;
    if (++calls <= 5 || calls % 600 == 0)
        dbg("wait #%u: frame %u, %.1f ms %s -> %d", calls, b->FrameNum, late / 1e6,
            late < 0 ? "early" : "late", late < 0);
    return late < 0;
}

int32_t BinkShouldSkip(BINK *b) {
    if (!b || b->ReadError || b->Paused) return 0;
    Shim *s = shim_of(b);
    if (!s->clock_start_ns || b->FrameNum + 2 >= b->Frames) return 0;
    return lateness_ns(s) > (int64_t)(2 * frame_ns(b));
}

void BinkGetFrameBuffersInfo(BINK *b, BINKFRAMEBUFFERS *fb) {
    if (!b || !fb) return;
    memset(fb, 0, sizeof *fb);
    // Bink 2 layout (the library's Bink 2 check at 0x252e0 true): luma 32x16 aligned, chroma exactly half.
    fb->YABufferWidth = (b->Width + 31) & ~31u;
    fb->YABufferHeight = (b->Height + 15) & ~15u;
    fb->cRcBBufferWidth = fb->YABufferWidth >> 1;
    fb->cRcBBufferHeight = fb->YABufferHeight >> 1;
    fb->TotalFrames = b->Frames == 1 ? 1 : (int32_t)(((b->OpenFlags >> 3) & 1) | 2);
    for (int i = 0; i < fb->TotalFrames; i++) {
        fb->Frames[i].YPlane.Allocate = 1;
        fb->Frames[i].cRPlane.Allocate = 1;
        fb->Frames[i].cBPlane.Allocate = 1;
        fb->Frames[i].APlane.Allocate = (b->OpenFlags >> 20) & 1;
        fb->Frames[i].HPlane.Allocate = (b->OpenFlags >> 2) & 1;
    }
}

void BinkRegisterFrameBuffers(BINK *b, BINKFRAMEBUFFERS *fb) {
    if (!b || !fb) return;
    b->FrameBuffers = fb;
}

void BinkGetSummary(BINK *b, BINKSUMMARY *sum) {
    if (!b || !sum) return;
    Shim *s = shim_of(b);
    memset(sum, 0, sizeof *sum);
    sum->Width = b->Width;
    sum->Height = b->Height;
    sum->TotalTime = (uint32_t)((now_ns() - s->open_ns) / 1000000);
    sum->TotalOpenTime = sum->TotalTime;
    sum->FileFrameRate = sum->FrameRate = b->FrameRate;
    sum->FileFrameRateDiv = sum->FrameRateDiv = b->FrameRateDiv;
    sum->TotalFrames = b->Frames;
    sum->TotalPlayedFrames = s->played_frames;
}

BINKTRACK *BinkOpenTrack(BINK *b, uint32_t index) {
    if (!b || index >= b->NumTracks) return NULL;
    Shim *s = shim_of(b);
    uint32_t type = s->track_types[index];
    if (!(type & 0x90000000u)) return NULL;  // same acceptance test as the original
    TrackState *t = &s->tracks[index];
    pthread_mutex_lock(&s->lock);
    if (!t->dec && t->stream >= 0) t->dec = open_decoder(s->fmt->streams[t->stream]);
    pthread_mutex_unlock(&s->lock);
    if (!t->dec) return NULL;
    BINKTRACK *tr = calloc(1, sizeof *tr);
    if (!tr) return NULL;
    tr->Frequency = type & 0xffff;
    tr->Channels = ((type >> 29) & 1) + 1;
    // One video frame of audio plus two decoder blocks of slack, 16-bit samples.
    uint64_t per_frame = (uint64_t)tr->Frequency * b->FrameRateDiv / b->FrameRate;
    tr->MaxSize = (uint32_t)(((per_frame + 2 * 4096) * tr->Channels * 2 + 3) & ~3ull);
    tr->bink = b;
    tr->sndcomp = t;
    tr->trackindex = (int32_t)index;
    t->open_count++;
    return tr;
}

void BinkCloseTrack(BINKTRACK *tr) {
    if (!tr) return;
    TrackState *t = tr->sndcomp;
    if (t && t->open_count) t->open_count--;
    free(tr);
}

uint32_t BinkGetTrackData(BINKTRACK *tr, void *dest, uint32_t destsize) {
    if (!tr || !dest || !tr->bink) return 0;
    Shim *s = shim_of(tr->bink);
    TrackState *t = tr->sndcomp;
    pthread_mutex_lock(&s->lock);
    load_frame(s, s->pub.FrameNum);
    size_t avail = t->pcm_len - t->pcm_pos;
    size_t cap = destsize ? destsize : tr->MaxSize;
    size_t n = avail < cap ? avail : cap;
    n &= ~(size_t)(tr->Channels * 2 - 1);
    memcpy(dest, t->pcm + t->pcm_pos, n);
    t->pcm_pos += n;
    s->audio_bytes += (uint32_t)n;
    pthread_mutex_unlock(&s->lock);
    return (uint32_t)n;
}

uint32_t BinkGetTrackID(BINK *b, uint32_t index) {
    if (!b || index >= BINK_MAX_TRACKS) return 0;
    return shim_of(b)->track_ids[index];
}

const char *BinkGetError(void) { return g_error; }
