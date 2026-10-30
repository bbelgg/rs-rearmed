// Override module `audio`, device list (src/audio.h): input devices first in the system's device list.
//
// The device scan (0x10133ccb0) goes through the system object's kAudioHardwarePropertyDevices list, which
// holds output-only devices too, and opens each device with input channels in a free one of SoundInput's 10
// slots (0x10131aff0; 0x10133bee0 opens list entry i, 0x10133ab30). It gives up on the rest of the list at
// the first device whose output or input stream configuration, maker or name it can't read. The import thunk
// of AudioObjectGetPropertyData (thunks/gen_thunks.py) sends the game's device-list reads here; the list comes
// back with the devices that have input streams first, each group in the system's order, so an output-only
// device the game can't read doesn't hide an interface. RS_AUDIO_INPUTS_FIRST=0 turns it off.
//
// Device UIDs, one string per device: the device scan (0x10133ccb0) tells devices apart by comparing their
// kAudioDevicePropertyDeviceUID CFStrings by pointer, against the UIDs of the open slots (+0x50) and of the
// devices that failed to open (a list at +0x1778, 16 entries). CoreAudio returns a new string on every read,
// and a failed device's string is released as soon as the next device takes its slot (0x10133cb10), while the
// failed list keeps the pointer. A later device's UID can be allocated at that address and is then skipped
// as failed: with a webcam that can't run at 48 kHz, an interface went missing from Direct Connect whenever
// another USB interface was connected. Here every UID read returns the one string kept for that UID, so a
// pointer stays valid and equal pointers mean equal UIDs. RS_AUDIO_STABLE_UIDS=0 turns it off.
#include <CoreAudio/CoreAudio.h>
#include <os/lock.h>
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

#define MAX_UIDS 64

// The string kept for uid's text: the caller's reference is swapped for one to the kept string.
static CFStringRef stable_uid(CFStringRef uid) {
  static os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
  static CFStringRef kept[MAX_UIDS];
  static int n;
  CFStringRef r = uid;
  os_unfair_lock_lock(&lock);
  int i = 0;
  while (i < n && !CFEqual(kept[i], uid)) i++;
  if (i < n) {
    r = (CFStringRef)CFRetain(kept[i]);
    CFRelease(uid);
  } else if (n < MAX_UIDS) {
    kept[n++] = (CFStringRef)CFRetain(uid);
  }
  os_unfair_lock_unlock(&lock);
  return r;
}

int32_t rs_audio_get_property_data(uint32_t obj, const void *addr, uint32_t qsize, const void *qdata,
                                   uint32_t *size, void *data) {
  OSStatus st = AudioObjectGetPropertyData(obj, addr, qsize, qdata, size, data);
  const AudioObjectPropertyAddress *a = addr;
  if (st == noErr && a && a->mSelector == kAudioDevicePropertyDeviceUID && size && *size == sizeof(CFStringRef) &&
      data && *(CFStringRef *)data && rs_audio_env_on("RS_AUDIO_STABLE_UIDS", 1))
    *(CFStringRef *)data = stable_uid(*(CFStringRef *)data);
  rs_audio_trace_get(obj, addr, st, size ? *size : 0, data);
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
    rs_audio_log("device list: %u devices, the %u with inputs first", n, ni);
  return st;
}
