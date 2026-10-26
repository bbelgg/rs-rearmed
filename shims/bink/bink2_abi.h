// Bink 2 public ABI as seen by Rocksmith 2014 (libBink2Macx64.dylib, x86_64).
//
// ABI of the Bink 2 library the game ships, reconstructed from that library for
// interoperability; every offset below is one the library reads or writes
// (library code offset noted where useful). The game's own __text is encrypted
// on disk, so the library is the reference for what the game was compiled
// against.
#pragma once

#include <stddef.h>
#include <stdint.h>

// --- BinkOpen flags (subset honoured by the shim) -----------------------------
#define BINKSNDTRACK      0x00004000u  // use BinkSetSoundTrack's track list
#define BINKPRELOADALL    0x00002000u
#define BINKFRAMERATE     0x00001000u  // override frame rate (not exposed: no BinkSetFrameRate import)
#define BINKALPHA         0x00100000u  // allocate/decode the alpha plane
#define BINKFILEHANDLE    0x00800000u  // "name" is a file descriptor
#define BINKIOSIZE        0x01000000u
#define BINKIOPROCESSOR   0x02000000u  // use the BINKIOOPEN set with BinkSetIO
#define BINKFROMMEMORY    0x04000000u  // "name" points at the whole file in memory

#define BINK_MAX_FRAMEBUFFERS 3

// --- Custom I/O (BinkSetIO) -------------------------------------------------------
// Lives inside the BINK at +0x210 and is 0x190 bytes (the open routine at 0x49c0
// zeroes a 400-byte local at BINK+0x210, fills it through BINKIOOPEN, then copies the
// whole 0x6d0-byte BINK header into the heap object).
typedef struct BINKIO BINKIO;
typedef int32_t  (*BINKIOOPEN)(BINKIO *io, const char *name, uint32_t flags);
typedef uint64_t (*BINKIOREADHEADER)(BINKIO *io, int64_t offset, void *dest, uint64_t size);  // offset -1: continue
typedef uint64_t (*BINKIOREADFRAME)(BINKIO *io, uint32_t frame, int64_t offset, void *dest, uint64_t size);
typedef uint64_t (*BINKIOGETBUFFERSIZE)(BINKIO *io, uint64_t size);
typedef void     (*BINKIOSETINFO)(BINKIO *io, void *buf, uint64_t bufsize, uint64_t filesize, uint32_t simulate);
typedef uint32_t (*BINKIOIDLE)(BINKIO *io);
typedef void     (*BINKIOCLOSE)(BINKIO *io);
typedef int32_t  (*BINKIOBGCONTROL)(BINKIO *io, uint32_t control);

struct BINKIO {
    BINKIOREADHEADER    ReadHeader;     // 0x00  open: ReadHeader(io, 0, hdr, 0x2c)
    BINKIOREADFRAME     ReadFrame;      // 0x08  frame advance (0x7b90): io+0x08 (BINK+0x218)
    BINKIOGETBUFFERSIZE GetBufferSize;  // 0x10
    BINKIOSETINFO       SetInfo;        // 0x18
    BINKIOIDLE          Idle;           // 0x20  BinkWait: BINK+0x230
    BINKIOCLOSE         Close;          // 0x28  BinkClose: BINK+0x238
    BINKIOBGCONTROL     BGControl;      // 0x30  BinkWait: BINK+0x240
    void               *bink;           // 0x38  open: *(BINK+0x248) = BINK
    uint8_t             _pad40[0x28];
    uint32_t          (*timer_ms)(void);                       // 0x68  default at 0x9ca0 (RADTimerRead)
    uint8_t             _pad70[0x08];
    int64_t           (*atomic_add)(volatile int64_t *, int64_t);  // 0x78  default at 0x9cb0 (atomic fetch-add)
    void              (*copy)(void *, const void *, uint64_t);     // 0x80  default at 0x9cc0 (memcpy)
    uint8_t             _pad88[0x38];
    uint64_t            BufSize;        // 0xc0  (BINK+0x2d0)
    uint32_t            ReadError;      // 0xc8  (BINK+0x2d8) "Error reading Bink header." when set
    uint8_t             _padcc[0x190 - 0xcc];
};
_Static_assert(sizeof(BINKIO) == 0x190, "BINKIO size");
_Static_assert(offsetof(BINKIO, bink) == 0x38, "BINKIO.bink");
_Static_assert(offsetof(BINKIO, ReadError) == 0xc8, "BINKIO.ReadError");

// --- Frame buffers ----------------------------------------------------------------
// BinkGetFrameBuffersInfo fills it; the game allocates every plane whose
// Allocate is set, writes Buffer/BufferPitch, and hands it back with
// BinkRegisterFrameBuffers (stored at BINK+0xb0). BinkAllocateFrameBuffers shows
// the per-plane layout: Buffer +0, Allocate +8, BufferPitch +12.
typedef struct BINKPLANE {
    void    *Buffer;
    int32_t  Allocate;
    uint32_t BufferPitch;
} BINKPLANE;

typedef struct BINKFRAMEPLANESET {
    BINKPLANE YPlane;   // full res
    BINKPLANE cRPlane;  // half res
    BINKPLANE cBPlane;  // half res
    BINKPLANE APlane;   // full res, Allocate = OpenFlags bit 20 (BINKALPHA)
    BINKPLANE HPlane;   // full res, Allocate = OpenFlags bit 2
} BINKFRAMEPLANESET;

typedef struct BINKFRAMEBUFFERS {
    int32_t  TotalFrames;       // 1 for single-frame files, else 2 (3 with OpenFlags bit 3)
    uint32_t YABufferWidth;     // Bink 2: (Width + 31) & ~31
    uint32_t YABufferHeight;    // Bink 2: (Height + 15) & ~15
    uint32_t cRcBBufferWidth;   // YABufferWidth / 2
    uint32_t cRcBBufferHeight;  // YABufferHeight / 2
    uint32_t FrameNum;          // index of the set holding the current frame
    BINKFRAMEPLANESET Frames[BINK_MAX_FRAMEBUFFERS];
} BINKFRAMEBUFFERS;
_Static_assert(sizeof(BINKPLANE) == 16, "BINKPLANE size");
_Static_assert(sizeof(BINKFRAMEPLANESET) == 80, "BINKFRAMEPLANESET size");
_Static_assert(offsetof(BINKFRAMEBUFFERS, Frames) == 24, "BINKFRAMEBUFFERS.Frames");
_Static_assert(sizeof(BINKFRAMEBUFFERS) == 0x108, "BINKFRAMEBUFFERS size");

// --- Audio track (BinkOpenTrack) --------------------------------------------------------
typedef struct BINKTRACK {
    uint32_t Frequency;   // 0x00  low 16 bits of the track's type word
    uint32_t Channels;    // 0x04  1 or 2 (type bit 29)
    uint32_t MaxSize;     // 0x08  largest BinkGetTrackData result, bytes
    uint32_t _pad0c;
    void    *bink;        // 0x10
    void    *sndcomp;     // 0x18  decoder; the shim keeps its own state here
    int32_t  trackindex;  // 0x20
    uint32_t _pad24;
} BINKTRACK;
_Static_assert(sizeof(BINKTRACK) == 0x28, "BINKTRACK size");

// --- BinkGetSummary (0x90 bytes, zeroed then filled) -----------------------------------
typedef struct BINKSUMMARY {
    uint8_t  _pad00[0x28];
    uint32_t Width;               // 0x28
    uint32_t Height;              // 0x2c
    uint32_t TotalTime;           // 0x30  ms since open
    uint32_t FileFrameRate;       // 0x34
    uint32_t FileFrameRateDiv;    // 0x38
    uint32_t FrameRate;           // 0x3c
    uint32_t FrameRateDiv;        // 0x40
    uint32_t TotalOpenTime;       // 0x44
    uint32_t TotalFrames;         // 0x48
    uint32_t TotalPlayedFrames;   // 0x4c
    uint8_t  _pad50[0x40];
} BINKSUMMARY;
_Static_assert(sizeof(BINKSUMMARY) == 0x90, "BINKSUMMARY size");

// --- The BINK handle -------------------------------------------------------------------
// Only the fields with a known meaning are named; everything else stays zero.
// The original object is 0x6d0 bytes and 256-byte aligned.
typedef struct BINK {
    uint32_t Width;            // 0x000
    uint32_t Height;           // 0x004
    uint32_t Frames;           // 0x008
    uint32_t FrameNum;         // 0x00c  1-based current frame
    uint32_t LastFrameNum;     // 0x010  the frame advance (0x7b90) copies FrameNum here first
    uint32_t FrameRate;        // 0x014
    uint32_t FrameRateDiv;     // 0x018
    uint32_t ReadError;        // 0x01c
    uint32_t OpenFlags;        // 0x020
    uint32_t FileFlags;        // 0x024  header flags (+0x24 in the file)
    uint32_t LargestFrameSize; // 0x028
    uint8_t  _pad2c[0x0c];
    uint32_t NumTracks;        // 0x038  BinkOpenTrack bound
    uint8_t  _pad3c[0x74];
    BINKFRAMEBUFFERS *FrameBuffers;  // 0x0b0  BinkRegisterFrameBuffers
    uint8_t  _padb8[0x18];
    uint64_t Size;             // 0x0d0  file size - 8
    uint8_t  _padd8[0x40];
    uint32_t Codec;            // 0x118  'KB2g' etc.
    uint32_t Paused;           // 0x11c
    uint8_t  _pad120[0x0c];
    uint32_t PlayingTracks;    // 0x12c  tracks played by Bink's own sound system (always 0 here)
    uint8_t  _pad130[0xb0];
    uint32_t *TrackMaxSizes;   // 0x1e0
    uint32_t *TrackTypes;      // 0x1e8  rate | flags << 16
    uint32_t *TrackIDs;        // 0x1f0
    uint8_t  _pad1f8[0x18];
    BINKIO   io;               // 0x210
    uint8_t  _pad3a0[0x6d0 - 0x3a0];
} BINK;
_Static_assert(offsetof(BINK, FrameBuffers) == 0xb0, "BINK.FrameBuffers");
_Static_assert(offsetof(BINK, Size) == 0xd0, "BINK.Size");
_Static_assert(offsetof(BINK, Codec) == 0x118, "BINK.Codec");
_Static_assert(offsetof(BINK, Paused) == 0x11c, "BINK.Paused");
_Static_assert(offsetof(BINK, PlayingTracks) == 0x12c, "BINK.PlayingTracks");
_Static_assert(offsetof(BINK, TrackMaxSizes) == 0x1e0, "BINK.TrackMaxSizes");
_Static_assert(offsetof(BINK, TrackIDs) == 0x1f0, "BINK.TrackIDs");
_Static_assert(offsetof(BINK, io) == 0x210, "BINK.io");
_Static_assert(sizeof(BINK) == 0x6d0, "BINK size");

typedef BINK *HBINK;
typedef BINKTRACK *HBINKTRACK;

// --- The 16 functions the game imports -----------------------------------------------------
#define RADEXPORT __attribute__((visibility("default")))
RADEXPORT HBINK    BinkOpen(const char *name, uint32_t flags);
RADEXPORT void     BinkClose(HBINK bink);
RADEXPORT void     BinkSetSoundTrack(uint32_t total, const uint32_t *tracks);
RADEXPORT void     BinkSetIO(BINKIOOPEN open);
RADEXPORT void     BinkSetIOSize(uint64_t size);
RADEXPORT int32_t  BinkDoFrame(HBINK bink);
RADEXPORT void     BinkNextFrame(HBINK bink);
RADEXPORT int32_t  BinkWait(HBINK bink);
RADEXPORT int32_t  BinkShouldSkip(HBINK bink);
RADEXPORT void     BinkGetFrameBuffersInfo(HBINK bink, BINKFRAMEBUFFERS *fb);
RADEXPORT void     BinkRegisterFrameBuffers(HBINK bink, BINKFRAMEBUFFERS *fb);
RADEXPORT void     BinkGetSummary(HBINK bink, BINKSUMMARY *summary);
RADEXPORT HBINKTRACK BinkOpenTrack(HBINK bink, uint32_t trackindex);
RADEXPORT void     BinkCloseTrack(HBINKTRACK track);
RADEXPORT uint32_t BinkGetTrackData(HBINKTRACK track, void *dest, uint32_t destsize);
RADEXPORT uint32_t BinkGetTrackID(HBINK bink, uint32_t trackindex);
// Not imported by the game; handy for tests and diagnostics.
RADEXPORT const char *BinkGetError(void);
