// Override module `audio`, input side (src/audio.h): the SoundInput queue reader, with a trim.
//
// The cable's IOProc (0x10133c6d0) pushes each I/O buffer (128 mono floats) into a 5-slot queue (writer
// 0x10133b280: copies a whole slot under the lock, marks it filled, advances its index and never waits; it
// overwrites the slot if the reader is behind). The Wwise thread drains it oldest first through
// 0x10133b310(q, dst, bytes) when the Audio Input source needs samples (0x10133c620). Queue layout:
//   +0x08 + 8i slot buffers (5)    +0x30 + 4i filled flags (1 = filled)    +0x44 u16 slot size (bytes)
//   +0x46 u16 read offset          +0x48 u16 write index (writer)          +0x4a u16 read index
//   +0x4c u8 writer overrun        +0x4d u8 reader found the slot empty    +0x50 pthread mutex
//
// The Wwise thread reads in bursts (after each output callback), so the queue's standing depth is set by
// the phase at start-up and the clock drift between the cable and the output device: 0-3 slots of stale
// audio (up to 8 ms) for the whole session. The trim (RS_AUDIO_INPUT_TRIM=0 turns it off): when a read
// starts a fresh slot and more slots are filled than one burst needs plus one, the oldest are dropped. A
// burst needs (output buffer / 256, at least 1) Wwise blocks of 256 frames; with the output buffer
// unknown or 512 frames and up the limit is 5 slots, and nothing is ever dropped (the original behaviour).
// When the queue is full and the writer has overwritten the read slot, the order is already mixed up in
// the original; the trim then drops from the read index as well.
#include <pthread.h>

#include "audio.h"

#define Q_SLOTS 5
#define Q_BUF(q, i) (*(uint8_t **)((q) + RC_OFF(siq_bufs) + 8 * (i)))
#define Q_FLAG(q, i) ((int32_t *)((q) + RC_OFF(siq_filled) + 4 * (i)))
#define Q_SIZE(q) (*(uint16_t *)((q) + RC_OFF(siq_slot_size)))
#define Q_OFF(q) (*(uint16_t *)((q) + RC_OFF(siq_read_off)))
#define Q_RD(q) (*(uint16_t *)((q) + RC_OFF(siq_read_idx)))
#define Q_EMPTY(q) (*(uint8_t *)((q) + RC_OFF(siq_empty)))
#define Q_LOCK(q) ((pthread_mutex_t *)((q) + RC_OFF(siq_lock)))

static atomic_ulong g_trims, g_dropped;

static inline int flag_ld(uint8_t *q, unsigned i) { return __atomic_load_n(Q_FLAG(q, i), __ATOMIC_ACQUIRE); }
static inline void flag_clear(uint8_t *q, unsigned i) { __atomic_store_n(Q_FLAG(q, i), 0, __ATOMIC_RELEASE); }
static inline uint16_t next_slot(uint16_t i) { return (uint16_t)((i + 1u) % Q_SLOTS); }

// The most filled slots a read may find at a fresh slot before the oldest are dropped; >= Q_SLOTS: never.
static unsigned trim_limit(unsigned slot_bytes, unsigned out_frames) {
  unsigned slot_frames = slot_bytes / 4;  // mono float, as the IOProc writes
  if (!slot_frames) return Q_SLOTS;
  if (!out_frames) out_frames = 512;
  unsigned blocks = (out_frames + AK_REFILL_FRAMES - 1) / AK_REFILL_FRAMES;
  if (!blocks) blocks = 1;
  unsigned per_block = (AK_REFILL_FRAMES + slot_frames - 1) / slot_frames;
  return per_block * blocks + 1;
}

// Drops all but the newest `limit` of the filled run that starts at the read index (offset 0).
static void trim(uint8_t *q, unsigned limit) {
  pthread_mutex_lock(Q_LOCK(q));
  uint16_t rd = Q_RD(q);
  unsigned n = 0;
  while (n < Q_SLOTS && flag_ld(q, (rd + n) % Q_SLOTS) == 1) n++;
  unsigned drop = n > limit ? n - limit : 0;
  for (unsigned k = 0; k < drop; k++) {
    flag_clear(q, rd);
    rd = next_slot(rd);
  }
  Q_RD(q) = rd;
  pthread_mutex_unlock(Q_LOCK(q));
  if (!drop) return;
  unsigned long t = atomic_fetch_add_explicit(&g_trims, 1, memory_order_relaxed) + 1;
  unsigned long d = atomic_fetch_add_explicit(&g_dropped, drop, memory_order_relaxed) + drop;
  if (t <= 10 || t % 100 == 0)
    rs_audio_log("input queue: dropped %u stale slot(s) of %u (limit %u); %lu trims, %lu slots so far", drop, n,
                 limit, t, d);
}

// 0x10133b310 with the trim: limit >= Q_SLOTS reads exactly as the original.
static uint16_t rs_queue_read(uint8_t *q, void *dst, uint16_t req, unsigned limit) {
  if (limit < Q_SLOTS && Q_OFF(q) == 0 && flag_ld(q, Q_RD(q)) == 1) trim(q, limit);
  uint16_t rd = Q_RD(q);
  int filled = flag_ld(q, rd) == 1;
  Q_EMPTY(q) = !filled;
  if (!filled) return 0;
  uint32_t avail = (uint32_t)Q_SIZE(q) - Q_OFF(q);
  if (avail == 0) {
    pthread_mutex_lock(Q_LOCK(q));
    flag_clear(q, Q_RD(q));
    pthread_mutex_unlock(Q_LOCK(q));
    Q_RD(q) = next_slot(Q_RD(q));
    Q_OFF(q) = 0;
    return 0;
  }
  uint16_t avail16 = (uint16_t)avail, n = avail16 >= req ? req : avail16;
  pthread_mutex_lock(Q_LOCK(q));
  memcpy(dst, Q_BUF(q, Q_RD(q)) + Q_OFF(q), n);
  pthread_mutex_unlock(Q_LOCK(q));
  Q_OFF(q) = (uint16_t)(Q_OFF(q) + n);
  if (n != avail16) return n;
  pthread_mutex_lock(Q_LOCK(q));
  flag_clear(q, Q_RD(q));
  pthread_mutex_unlock(Q_LOCK(q));
  Q_RD(q) = next_slot(Q_RD(q));
  Q_OFF(q) = 0;
  return (uint16_t)avail;
}

static int g_trim = -1;

void RC_FN(input_queue_read)(rc_ctx *c) {
  OV_ARGS_OF(input_queue_read, a, c);
  uint8_t *q = OV_PTR(a, uint8_t);
  void *dst = OV_PTR(a, void);
  uint16_t req = (uint16_t)OV_U64(a);
  if (g_trim < 0) g_trim = rs_audio_env_on("RS_AUDIO_INPUT_TRIM", 1);
  unsigned limit = g_trim ? trim_limit(Q_SIZE(q), atomic_load_explicit(&rs_audio_out_frames, memory_order_relaxed))
                          : Q_SLOTS;
  ov_ret_u32(c, rs_queue_read(q, dst, req, limit));
}
