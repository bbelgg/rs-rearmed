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
//
// The list of inputs (rs_audio_input.h): each is a device of its own to the game, which opens one per slot and lists
// what it opened. A channel's id holds the real device and the channel, so nothing has to be kept; every call the game
// makes with one reaches the real device (rs_audio_real_dev, in the import thunks) and only its name and UID differ.
// An I/O function gets the real device from the system: audio_chan.c gives the game its own id there, as it compares
// them.
#include <CoreAudio/CoreAudio.h>
#include <math.h>
#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "rs_audio_input.h"

#define ENTRY_ID 0x40000000u
#define ENTRY_MAX_DEV 0x3fffffu  // what the id has room for
#define ENTRY_MAX_CHANNEL 253
#define MAX_LIST 128

uint32_t rs_audio_real_dev(uint32_t id) { return id & ENTRY_ID ? (id & ~ENTRY_ID) >> 8 : id; }
int rs_audio_entry_channel(uint32_t id) { return id & ENTRY_ID ? (int)(id & 0xff) - 1 : -1; }

// The game starts and stops its I/O by the function's address, not by the id it was given. That is the same id
// only for a function's first registration on a device, so a channel's own is kept here.
#define MAX_PROCS 32
static struct {
  uint32_t id;
  AudioDeviceIOProcID proc;
} g_procs[MAX_PROCS];
static os_unfair_lock g_procs_lock = OS_UNFAIR_LOCK_INIT;

int32_t rs_audio_io_create(uint32_t id, void *proc, void *client, void *out) {
  AudioDeviceIOProcID got = NULL;
  OSStatus st = AudioDeviceCreateIOProcID(rs_audio_real_dev(id), proc, client, &got);
  if (out) *(AudioDeviceIOProcID *)out = got;
  if (st != noErr || !(id & ENTRY_ID)) return st;
  os_unfair_lock_lock(&g_procs_lock);
  int free_at = -1, i = 0;
  while (i < MAX_PROCS && g_procs[i].id != id) {
    if (!g_procs[i].id && free_at < 0) free_at = i;
    i++;
  }
  if (i == MAX_PROCS) i = free_at;
  if (i >= 0) {
    g_procs[i].id = id;
    g_procs[i].proc = got;
  } else {
    rs_audio_log("no room for the I/O of input 0x%x", id);
  }
  os_unfair_lock_unlock(&g_procs_lock);
  return st;
}

int32_t rs_audio_io_destroy(uint32_t id, void *given) {
  void *proc = rs_audio_io_id(id, given);
  if (id & ENTRY_ID) {
    os_unfair_lock_lock(&g_procs_lock);
    for (int i = 0; i < MAX_PROCS; i++)
      if (g_procs[i].id == id) g_procs[i].id = 0;
    os_unfair_lock_unlock(&g_procs_lock);
  }
  return AudioDeviceDestroyIOProcID(rs_audio_real_dev(id), (AudioDeviceIOProcID)proc);
}

void *rs_audio_io_id(uint32_t id, void *given) {
  if (!(id & ENTRY_ID)) return given;
  void *r = given;
  os_unfair_lock_lock(&g_procs_lock);
  for (int i = 0; i < MAX_PROCS; i++)
    if (g_procs[i].id == id) r = (void *)g_procs[i].proc;
  os_unfair_lock_unlock(&g_procs_lock);
  return r;
}

// An input's level (rs_audio_input.h): any input but the cable reports a volume of ours, and the hardware is left
// alone. audio_chan.c applies it.
#define MAX_SOFT 32
static struct {
  _Atomic uint32_t id;
  _Atomic float value;
} g_soft[MAX_SOFT];

static int soft_volume(uint32_t id, const AudioObjectPropertyAddress *a) {
  return a && a->mSelector == kAudioDevicePropertyVolumeScalar && a->mScope != kAudioObjectPropertyScopeOutput &&
         rs_audio_level_on() && rs_audio_has_input(rs_audio_real_dev(id)) &&
         !rs_audio_is_cable(rs_audio_real_dev(id));
}

// A device plugged in again gets a new id, so the oldest entry makes room. Only the game's thread writes these.
static _Atomic float *soft_slot(uint32_t id) {
  static int next;
  int i = 0;
  while (i < MAX_SOFT && g_soft[i].id && g_soft[i].id != id) i++;
  if (i == MAX_SOFT) i = next++ % MAX_SOFT;
  if (g_soft[i].id != id) {
    g_soft[i].id = 0;
    g_soft[i].value = RS_AUDIO_LEVEL_UNITY;
    g_soft[i].id = id;
  }
  return &g_soft[i].value;
}

float rs_audio_soft_gain(uint32_t id) {
  for (int i = 0; i < MAX_SOFT; i++)
    if (g_soft[i].id == id) return rs_audio_level_applied(g_soft[i].value);
  return 1.0f;
}

int32_t rs_audio_has_property(uint32_t obj, const void *addr) {
  const AudioObjectPropertyAddress *a = addr;
  if (soft_volume(obj, a)) return 1;
  return AudioObjectHasProperty(rs_audio_real_dev(obj), addr);
}

int rs_audio_soft_volume_set(uint32_t obj, const void *addr, uint32_t size, const void *data) {
  if (!soft_volume(obj, addr) || size != sizeof(float)) return 0;
  rs_audio_level_set(obj, *(const float *)data);  // first: the I/O thread finds the level once it's there
  *soft_slot(obj) = *(const float *)data;
  rs_audio_log("input level: 0x%x to %.4f, gain %.1f dB", obj, *(const float *)data,
               20.0 * log10(rs_audio_level_applied(*(const float *)data)));
  return 1;
}

// The system's list with the inputs in entries' order, the devices without inputs after them.
static uint32_t entry_list(AudioObjectID *out, uint32_t max) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  AudioObjectID devs[MAX_LIST];
  UInt32 size = sizeof devs;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, devs) != noErr) return 0;
  int n = (int)(size / sizeof *devs);
  rs_audio_entry es[MAX_LIST];
  int m = rs_audio_input_entries(devs, n, rs_audio_entries_budget(), es, MAX_LIST);
  uint32_t k = 0;
  for (int i = 0; i < m && k < max; i++) {
    if (es[i].channel < 0)
      out[k++] = es[i].dev;
    else if (es[i].dev <= ENTRY_MAX_DEV && es[i].channel <= ENTRY_MAX_CHANNEL)
      out[k++] = ENTRY_ID | es[i].dev << 8 | (uint32_t)(es[i].channel + 1);
  }
  for (int i = 0; i < n && k < max; i++)
    if (!rs_audio_has_input(devs[i])) out[k++] = devs[i];
  return k;
}

uint32_t rs_audio_entry_list_size(void) {
  AudioObjectID list[MAX_LIST];
  return entry_list(list, MAX_LIST) * (uint32_t)sizeof(AudioObjectID);
}

// A channel's name and UID, in place of the device's.
static CFStringRef entry_string(CFStringRef s, const char *format, uint32_t id) {
  char text[256] = "";
  CFStringGetCString(s, text, sizeof text, kCFStringEncodingUTF8);
  size_t len = strlen(text);
  while (len && text[len - 1] == ' ') text[--len] = 0;
  char out[300];
  snprintf(out, sizeof out, format, text, rs_audio_entry_channel(id) + 1);
  CFRelease(s);
  return CFStringCreateWithCString(NULL, out, kCFStringEncodingUTF8);
}

int32_t rs_audio_get_property_data(uint32_t obj, const void *addr, uint32_t qsize, const void *qdata,
                                   uint32_t *size, void *data) {
  const AudioObjectPropertyAddress *a = addr;
  if (obj == kAudioObjectSystemObject && a && a->mSelector == kAudioHardwarePropertyDevices && size && data &&
      rs_audio_entries_budget() > 0) {
    *size = entry_list(data, *size / sizeof(AudioObjectID)) * (uint32_t)sizeof(AudioObjectID);
    return noErr;
  }
  if (soft_volume(obj, a) && size && *size == sizeof(float) && data) {
    *(float *)data = *soft_slot(obj);
    return noErr;
  }
  OSStatus st = AudioObjectGetPropertyData(rs_audio_real_dev(obj), addr, qsize, qdata, size, data);
  if (st == noErr && a && obj & ENTRY_ID && size && data) {
    if (a->mSelector == kAudioObjectPropertyName && *size == sizeof(CFStringRef) && *(CFStringRef *)data)
      *(CFStringRef *)data = entry_string(*(CFStringRef *)data, "%s channel %d", obj);
    else if (a->mSelector == kAudioDevicePropertyDeviceUID && *size == sizeof(CFStringRef) && *(CFStringRef *)data)
      *(CFStringRef *)data = entry_string(*(CFStringRef *)data, "%s#%d", obj);
  }
  if (st == noErr && a && a->mSelector == kAudioDevicePropertyDeviceUID && size && *size == sizeof(CFStringRef) &&
      data && *(CFStringRef *)data && rs_audio_env_on("RS_AUDIO_STABLE_UIDS", 1))
    *(CFStringRef *)data = rs_audio_stable_uid(*(CFStringRef *)data);
  rs_audio_trace_get(obj, addr, st, size ? *size : 0, data);
  if (st != noErr || obj != kAudioObjectSystemObject || a->mSelector != kAudioHardwarePropertyDevices ||
      !size || !data || !rs_audio_env_on("RS_AUDIO_INPUTS_FIRST", 1))
    return st;
  uint32_t n = *size / sizeof(AudioObjectID);
  int moved = rs_audio_inputs_first(data, n);
  // Logged once per distinct device count (the list is read on start-up and on every hot-plug check).
  static _Atomic uint32_t last_n;
  if (moved && atomic_exchange(&last_n, n) != n) {
    const AudioObjectID *devs = data;
    uint32_t ni = 0;
    while (ni < n && rs_audio_has_input(devs[ni])) ni++;
    rs_audio_log("device list: %u devices, the %u with inputs first", n, ni);
  }
  return st;
}
