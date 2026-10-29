// Override module `audio`, device list (src/audio.h): input devices first in the system's device list.
//
// SoundInput keeps 10 device slots (0x10131aff0), and slot i opens entry i of the system object's
// kAudioHardwarePropertyDevices list (0x10133bee0 -> 0x10133ab30). That list holds every device, output-only
// ones too (displays, docks, speakers), so on a Mac with more than 10 devices an interface past the 10th
// entry never shows up under Direct Connect. The import thunk of AudioObjectGetPropertyData
// (thunks/gen_thunks.py) sends the game's device-list reads here; the list comes back with the devices that
// have input streams first, each group in the system's order. RS_AUDIO_INPUTS_FIRST=0 turns it off.
#include <CoreAudio/CoreAudio.h>
#include <stdatomic.h>
#include <string.h>

#include "audio.h"

#define MAX_DEVS 128

static int has_input(AudioObjectID dev) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput,
                                  kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  return AudioObjectGetPropertyDataSize(dev, &a, 0, NULL, &size) == noErr && size > 0;
}

int32_t rs_audio_get_property_data(uint32_t obj, const void *addr, uint32_t qsize, const void *qdata,
                                   uint32_t *size, void *data) {
  OSStatus st = AudioObjectGetPropertyData(obj, addr, qsize, qdata, size, data);
  const AudioObjectPropertyAddress *a = addr;
  if (st != noErr || obj != kAudioObjectSystemObject || a->mSelector != kAudioHardwarePropertyDevices ||
      !size || !data || !rs_audio_env_on("RS_AUDIO_INPUTS_FIRST", 1))
    return st;
  uint32_t n = *size / sizeof(AudioObjectID);
  if (n < 2 || n > MAX_DEVS) return st;
  AudioObjectID *devs = data, in[MAX_DEVS], out[MAX_DEVS];
  uint32_t ni = 0, no = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (has_input(devs[i])) in[ni++] = devs[i];
    else out[no++] = devs[i];
  }
  int moved = memcmp(devs, in, ni * sizeof *in) != 0;
  memcpy(devs, in, ni * sizeof *in);
  memcpy(devs + ni, out, no * sizeof *out);
  // Logged once per distinct device count (the list is read on start-up and on every hot-plug check).
  static _Atomic uint32_t last_n;
  if (moved && atomic_exchange(&last_n, n) != n)
    rs_audio_log("device list: %u devices, the %u with inputs first (the game opens only the first 10)", n, ni);
  return st;
}
