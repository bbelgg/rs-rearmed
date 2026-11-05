// Override module `audio`, the guitar input's channel and gain (src/audio.h, shims/coreaudio/rs_audio_input.h).
//
// SoundInput's I/O function (0x10133c6d0, the hook point si_io) takes one I/O buffer of a device. It reads the
// input stream at +0x10 of its I/O state, and then:
//   * two channels: it moves the first channel, or the second with MonoToStereoChannel=1, to the front of the
//     buffer, in place, and writes one queue slot of that;
//   * any other count: it writes one queue slot of the buffer as it is. A slot is 128 mono floats, the size of
//     a mono buffer. Of a device of 8 channels that is the first 16 frames, all channels mixed up, and the rest
//     is lost. The sizes then don't match, so it skips the callback that follows a good write.
//
// This hook shows it a mono buffer instead: the channel the player chose, with the gain, in a buffer list of its
// own. It sets si_channels to 1 and the stream to 0 on each call, so the rest is the game's own mono path (queue
// write, flags, callback). Only that function reads si_channels. The device's stream starts inside si_open, so
// calls come before the settings are in place: those run as the original, a few milliseconds of a device that
// has just opened.
//
// A device of up to two channels with nothing set is left alone, MonoToStereoChannel included. One of more
// channels gets its first channel, as Learn & Play's input does.
#include <CoreAudio/CoreAudio.h>
#include <stdatomic.h>
#include <string.h>

#include "audio.h"
#include "rc_hook.h"
#include "rs_audio_input.h"

#define SLOTS 10           // SoundInput's inputs
#define MAX_FRAMES 4096u   // of an I/O buffer; the game asks for 128
#define REPORT_FRAMES (5u * 48000u)
#define SI_LISTENING 1     // si_state

typedef struct {
  _Atomic(uint8_t *) self;  // the SoundInput this slot belongs to
  _Atomic uint32_t dev;     // the device it applies to; 0: none, the input runs as the original
  uint32_t channel;
  float gain;
  rs_audio_input_stats stats;
  AudioBufferList list;
  char name[128];
  float mono[MAX_FRAMES];
} input;

static input g_in[SLOTS];
static _Thread_local uint8_t *t_opening;

static input *slot_of(uint8_t *self, int claim) {
  for (int i = 0; i < SLOTS; i++) {
    uint8_t *s = atomic_load_explicit(&g_in[i].self, memory_order_acquire);
    if (s == self) return &g_in[i];
    if (!s && claim) {
      atomic_store_explicit(&g_in[i].self, self, memory_order_release);
      return &g_in[i];
    }
  }
  return NULL;
}

static int before_open(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  t_opening = OV_PTR(a, uint8_t);
  return 0;
}

// What the opened device gets; called with the slot off (dev 0), so the I/O thread doesn't read it.
static void apply_settings(uint8_t *self, input *in) {
  uint32_t id = *(uint32_t *)(self + RC_OFF(si_dev) + 4), dev = rs_audio_real_dev(id);
  int channels = rs_audio_channels(dev, kAudioObjectPropertyScopeInput);
  rs_audio_input set;
  rs_audio_dev_name(dev, in->name, sizeof in->name);
  rs_audio_input_for(in->name, &set);
  int channel = rs_audio_input_channel(&set, (uint32_t)channels, in->name);
  if (rs_audio_entry_channel(id) >= 0 && rs_audio_entry_channel(id) < channels) channel = rs_audio_entry_channel(id);
  if (channel < 0) {
    if (channels <= 2 && set.gain == 1.0f && (!rs_audio_level_on() || rs_audio_is_cable(dev))) return;
    // The gain alone keeps the channel the game would listen to.
    channel = channels == 2 && *(uint32_t *)RC_GA(audio_mono_to_stereo) == 1 ? 1 : 0;
  }
  in->channel = (uint32_t)channel;
  in->gain = set.gain;
  in->list.mNumberBuffers = 1;
  in->list.mBuffers[0].mNumberChannels = 1;
  in->list.mBuffers[0].mData = in->mono;
  rs_audio_log("guitar input \"%s\": listening to channel %d of %d", in->name, channel + 1, channels);
  atomic_store_explicit(&in->dev, dev, memory_order_release);
}

static void after_open(rc_ctx *c, void *user) {
  (void)user;
  uint8_t *self = t_opening;
  t_opening = NULL;
  if (!self) return;
  int ok = (uint8_t)c->r[0] != 0;
  input *in = slot_of(self, ok);
  if (!in) return;
  atomic_store_explicit(&in->dev, 0, memory_order_release);
  if (ok) apply_settings(self, in);
}

static int before_io(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  uint32_t dev = (uint32_t)OV_U64(a);
  const AudioBufferList *list = OV_PTR(a, const AudioBufferList);
  // The game compares the device with its own id, which for a channel isn't the system's.
  uint32_t id = *(uint32_t *)(self + RC_OFF(si_dev) + 4);
  if (id != dev && rs_audio_real_dev(id) == dev) c->r[6] = id;  // rsi, the second argument
  input *in = slot_of(self, 0);
  if (!in || !list || atomic_load_explicit(&in->dev, memory_order_acquire) != dev) return 0;
  if (*(uint32_t *)(self + RC_OFF(si_state)) != SI_LISTENING) return 0;
  uint8_t *state = *(uint8_t **)(self + RC_OFF(si_io_state));
  if (!state) return 0;
  uint32_t frames = rs_audio_input_take(list, in->channel, in->gain * rs_audio_soft_gain(id), in->mono, MAX_FRAMES,
                                        &in->stats);
  if (!frames) return 0;
  in->list.mBuffers[0].mDataByteSize = frames * (uint32_t)sizeof(float);
  *(uint32_t *)(self + RC_OFF(si_channels)) = 1;
  *(uint32_t *)(state + RC_OFF(siio_stream)) = 0;
  c->r[2] = (uint64_t)(uintptr_t)&in->list;  // rdx, the third argument
  return 0;
}

void rs_audio_chan_report(void) {
  if (!rs_audio_log_on()) return;
  for (int i = 0; i < SLOTS; i++)
    if (atomic_load_explicit(&g_in[i].dev, memory_order_acquire))
      rs_audio_input_report(&g_in[i].stats, REPORT_FRAMES, g_in[i].name);
}

__attribute__((constructor)) static void chan_init(void) {
  if (!rs_audio_env_on("RS_AUDIO_INPUT", 1)) return;
  if (rc_hook_add(RC_ADDR(si_open), before_open, after_open, NULL) ||
      rc_hook_add(RC_ADDR(si_io), before_io, NULL, NULL))
    rs_audio_log("no SoundInput hook points: the guitar input's settings are off");
}
