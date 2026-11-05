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
// 128-frame buffer (the ring's count, audio_ring.c: at 1 the engine itself runs with 2). So does an
// interface that also has inputs: the guitar input needs 128 there (rs_audio_set_device_buffer). The sink is
// rebuilt on a device change, which calls this again.
//
// Following the default output: the sink opens the default device once and the game never moves it, so a
// change in Sound settings used to leave the sound where it was. Learn & Play follows the change, because
// its game reopens the stream when told. Here the graph is stopped, its AUHAL moved to the new device
// (48 kHz, its buffer, the LEngine thread's workgroup: all the sink's setup does for a device) and started.
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
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>
#include <mach/semaphore.h>
#include <pthread.h>
#include <stdio.h>
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
// Following the default output
// ------------------------------------------------------------------------------------------------
static pthread_mutex_t g_follow_mu = PTHREAD_MUTEX_INITIALIZER;  // the game's AUGraphStop waits for a move
static AudioUnit g_out_unit;
static AUGraph g_out_graph;

static void follow_default(void *arg) {
  (void)arg;
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  AudioObjectID dev = kAudioObjectUnknown;
  UInt32 size = sizeof dev;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, &dev) != noErr ||
      dev == kAudioObjectUnknown)
    return;
  pthread_mutex_lock(&g_follow_mu);
  if (g_out_graph && g_out_unit && dev != atomic_load(&g_out_dev)) {
    char name[128];
    rs_audio_dev_name(dev, name, sizeof name);
    fprintf(stderr, "audio: the default output is now \"%s\"\n", name);
    AUGraphStop(g_out_graph);
    AudioObjectPropertyAddress r = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    Float64 rate = SAMPLE_RATE;
    AudioObjectSetPropertyData(dev, &r, 0, NULL, sizeof rate, &rate);
    OSStatus st = AudioUnitSetProperty(g_out_unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                       &dev, sizeof dev);
    if (st == noErr)
      rs_audio_output_device(dev);
    else
      rs_audio_log("output device %u: the sink could not move there (%d)", dev, (int)st);
    AUGraphStart(g_out_graph);
  }
  pthread_mutex_unlock(&g_follow_mu);
}

// On the HAL's notification thread, which stopping the graph must not hold up.
static OSStatus default_changed(AudioObjectID obj, UInt32 n, const AudioObjectPropertyAddress *a, void *arg) {
  (void)obj; (void)n; (void)a; (void)arg;
  static dispatch_queue_t q;
  static dispatch_once_t once;
  dispatch_once(&once, ^{ q = dispatch_queue_create("rs audio default output", DISPATCH_QUEUE_SERIAL); });
  dispatch_async_f(q, NULL, follow_default);
  return noErr;
}

void rs_audio_output_unit(void *unit) {
  pthread_mutex_lock(&g_follow_mu);
  g_out_unit = (AudioUnit)unit;
  pthread_mutex_unlock(&g_follow_mu);
}

void rs_audio_output_graph(void *graph) {
  static int watching;
  pthread_mutex_lock(&g_follow_mu);
  g_out_graph = (AUGraph)graph;
  if (graph && !watching) {
    watching = 1;
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &a, default_changed, NULL);
  }
  pthread_mutex_unlock(&g_follow_mu);
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
