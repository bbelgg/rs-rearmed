// The game-independent half of the Mac build's audio module (tools/ws7/recomp/overrides/src/audio.h):
// environment switches, logging, the output device's I/O buffer, the real-time audio thread and
// the device's audio workgroup, stable device UIDs and the inputs-first device order.
//
// The Mac overrides (audio_out.c, audio_devs.c) and the Learn & Play WASAPI/winmm faces
// (win-x86/shims/audio) both use them, passing in their own numbers (the Mac build: the Wwise
// ring and the LEngine thread), so both builds apply one latency policy with the same RS_AUDIO_*
// switches. Nothing here knows about a game image.
//
// Switches read here (see the README's audio section):
//   RS_AUDIO_FRAMES        the output device's I/O buffer in frames (default 256; 0 keeps the device's)
//   RS_AUDIO_RT            the audio thread runs real-time in the device's workgroup (default on)
//   RS_AUDIO_STABLE_UIDS   one CFString per device UID (default on)
//   RS_AUDIO_INPUTS_FIRST  input devices first in the device list (default on)
//   RS_AUDIO_LOG           log to stderr with the "[rs-audio]" prefix (default off)
#ifndef RS_AUDIO_HOST_H
#define RS_AUDIO_HOST_H

#include <CoreAudio/CoreAudio.h>
#include <os/workgroup.h>
#include <stddef.h>
#include <stdint.h>

#define RS_AUDIO_DEFAULT_FRAMES 256u
#define RS_AUDIO_MIN_FRAMES 32u
#define RS_AUDIO_INPUT_FRAMES 128u  // what the game gives the guitar input

// An environment switch: unset or empty = dflt; "0", "n...", "f..." = 0; anything else = 1.
int rs_audio_env_on(const char *name, int dflt);
int rs_audio_log_on(void);
void rs_audio_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// A global-scope property of dev of exactly size bytes; 1 on success.
int rs_audio_dev_get(AudioObjectID dev, AudioObjectPropertySelector sel, void *out, UInt32 size);
// The device's name ("?" when it has none).
void rs_audio_dev_name(AudioObjectID dev, char *buf, size_t n);
// 1 when the device has input streams.
int rs_audio_has_input(AudioObjectID dev);
// 1 when the device has output streams.
int rs_audio_has_output(AudioObjectID dev);

// RS_AUDIO_FRAMES: unset or empty = dflt, 0 = leave the device alone.
uint32_t rs_audio_frames_wanted(uint32_t dflt);
// Sets the device's I/O buffer (kAudioDevicePropertyBufferFrameSize, a per-process setting) to
// RS_AUDIO_FRAMES (default min(256, cap)), kept within cap, the device's range and 32 frames, and
// returns the size the device then reports (0: unknown). cap is the most the caller's buffer
// behind the device can take per callback (the Mac build: half the Wwise ring), and at most 128 on
// a device that also has inputs: one size serves both directions, and the Mac game's guitar input
// keeps only 128 frames of each I/O buffer. who names the caller in the log.
uint32_t rs_audio_set_device_buffer(AudioObjectID dev, uint32_t cap, const char *who);

// Makes the calling thread a time-constraint thread for one period of frames at rate (up to 30%
// of it computing, done within the period).
void rs_audio_make_realtime(uint32_t frames, double rate, const char *who);

// The calling thread in a device's audio workgroup (kAudioDevicePropertyIOThreadOSWorkgroup).
typedef struct {
  os_workgroup_t wg;
  os_workgroup_join_token_s token;
  int joined;
} rs_audio_workgroup;
void rs_audio_wg_join(rs_audio_workgroup *s, AudioObjectID dev, const char *who);
void rs_audio_wg_leave(rs_audio_workgroup *s);

// The string kept for uid's text: the caller's reference is swapped for one to the kept string.
CFStringRef rs_audio_stable_uid(CFStringRef uid);
// Reorders a device list in place: the devices with input streams first, each group in the
// system's order. Returns 1 when the order changed.
int rs_audio_inputs_first(AudioObjectID *devs, uint32_t n);

#endif
