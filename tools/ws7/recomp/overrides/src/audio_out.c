// Override module `audio`, output side (src/audio.h): the output device's I/O buffer and the Wwise
// LEngine thread.
//
// rs_audio_output_device: the Wwise sink (setup 0x100aa6a10) sets its AUHAL's current device
// (AudioUnitSetProperty 2000 at 0x100aa6c3f) but never the device's buffer size, so the HAL callback
// runs at the system default (512 frames, 10.7 ms). The AudioUnitSetProperty import thunk
// (thunks/gen_thunks.py) calls this first; it sets kAudioDevicePropertyBufferFrameSize (a per-process
// setting: other apps keep theirs) to RS_AUDIO_FRAMES (default 256; 0 leaves the device alone). The size
// is kept within the device's range and at most half the sink's ring (LatencyBuffer x 256 frames): the
// HAL callback can take no more than the ring holds, and with no block to spare (a 256-frame ring and a
// 256-frame buffer) the game lists no audio interface under Direct Connect. So LatencyBuffer=1 gets a
// 128-frame buffer (the ring's count, audio_ring.c: at 1 the engine itself runs with 2). The sink is rebuilt
// on a device change, which calls this again.
//
// F_100b049b0, the LEngine thread entry (the thread Wwise renders on, 256 frames per block, woken by the
// HAL callback through a mach semaphore): the original is
//   g_lengine_thread = pthread_self(); g_start_time = time(0);
//   do { render(g_engine); semaphore_wait(this->sem); } while (!this->stop); return 0;
// The native version does the same, and first makes the thread a time-constraint (real-time) thread and
// keeps it in the current output device's audio workgroup (kAudioDevicePropertyIOThreadOSWorkgroup), so
// the scheduler treats a render as part of the device's I/O cycle. With a small ring there is little slack:
// a block must be ready within one HAL period of the wake-up.
// RS_AUDIO_RT=0 runs the thread as the original does.
#include <CoreAudio/CoreAudio.h>
#include <mach/semaphore.h>
#include <pthread.h>
#include <time.h>

#include "audio.h"

#define SAMPLE_RATE 48000u             // the sink forces 48 kHz ('nsrt', 0x100aa6a10)

_Atomic uint32_t rs_audio_out_frames;

// The output device as last set by the sink; the LEngine thread joins its workgroup when gen changes.
static _Atomic uint32_t g_out_dev;
static _Atomic uint32_t g_out_gen;

// ------------------------------------------------------------------------------------------------
// Output device buffer
// ------------------------------------------------------------------------------------------------
void rs_audio_output_device(uint32_t dev) {
  if (dev == kAudioObjectUnknown) return;
  uint32_t refills = rs_audio_ring_refills();
  uint32_t ring = refills * AK_REFILL_FRAMES;
  uint32_t applied = rs_audio_set_device_buffer(dev, ring / 2, "output device");
  atomic_store(&rs_audio_out_frames, applied);
  rs_audio_log("output device %u: LatencyBuffer %u = ring %u frames", dev, refills, ring);
  if (applied > ring)
    rs_audio_log("warning: the device buffer (%u) is larger than the ring (%u): expect dropouts; raise LatencyBuffer",
                 applied, ring);
  atomic_store(&g_out_dev, dev);
  atomic_fetch_add(&g_out_gen, 1);
}

// ------------------------------------------------------------------------------------------------
// LEngine thread
// ------------------------------------------------------------------------------------------------
// Time-constraint policy: one block period (256 frames, or the device buffer if larger: a callback then
// asks for several blocks at once), up to 30% of it computing, done within the period.
static void make_realtime(void) {
  uint32_t frames = atomic_load(&rs_audio_out_frames);
  if (frames < AK_REFILL_FRAMES) frames = AK_REFILL_FRAMES;
  rs_audio_make_realtime(frames, SAMPLE_RATE, "Wwise thread");
}

void RC_FN(ak_lengine_thread)(rc_ctx *c) {
  OV_ARGS_OF(ak_lengine_thread, a, c);
  uint8_t *self = OV_PTR(a, uint8_t);  // +0 mach semaphore, +4 stop flag
  RC_ST(uint64_t, RC_GA(ak_lengine_thread_id), (uint64_t)(uintptr_t)pthread_self());
  RC_ST(uint32_t, RC_GA(ak_lengine_start_time), (uint32_t)time(NULL));
  int rt = rs_audio_env_on("RS_AUDIO_RT", 1);
  if (rt) make_realtime();
  rs_audio_workgroup wg = {0};
  uint32_t gen = 0;
  do {
    if (rt) {
      uint32_t g = atomic_load_explicit(&g_out_gen, memory_order_acquire);
      if (g != gen) {
        gen = g;
        make_realtime();  // the period and budget follow the new device's buffer
        rs_audio_wg_join(&wg, atomic_load(&g_out_dev), "Wwise thread");
      }
    }
    OV_GUEST(ak_render_loop, RC_LD(uint64_t, RC_GA(ak_render_engine)));
    semaphore_wait(RC_LD(uint32_t, self));
  } while (!RC_LD(uint8_t, self + RC_OFF(ak_lengine_stop)));
  rs_audio_wg_leave(&wg);
  ov_ret_u32(c, 0);
}
