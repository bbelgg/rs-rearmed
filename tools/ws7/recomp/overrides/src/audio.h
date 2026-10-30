// Override module `audio`: lower guitar-to-sound latency (audio_out.c, audio_in.c, audio_devs.c, audio_ring.c,
// audio_trace.c).
//
// The guitar reaches the speakers through three buffers the game sets up (Wwise 2013-era integration):
//   * the cable's I/O buffer (128 frames) and a 5-slot queue of such buffers that the Wwise thread drains
//     (SoundInput, writer 0x10133b280, reader 0x10133b310);
//   * the Wwise sink's ring, LatencyBuffer x 256 frames (Rocksmith.ini [Audio] LatencyBuffer, Wwise
//     uNumRefillsInVoice at 0x101907df4), filled by the Wwise LEngine thread (entry 0x100b049b0);
//   * the output device's I/O buffer, which the game never sets (the system default, 512 frames).
// This module sets the output device's buffer (RS_AUDIO_FRAMES, default 256; never above half the ring,
// see audio_out.c), runs the LEngine thread as a real-time thread in the output device's
// audio workgroup (RS_AUDIO_RT), keeps the input queue from holding stale audio (RS_AUDIO_INPUT_TRIM), lists
// input devices first so an interface past the 10th device still shows up (RS_AUDIO_INPUTS_FIRST), and gives
// each device one UID string so the device scan doesn't skip an interface as failed (RS_AUDIO_STABLE_UIDS).
// LatencyBuffer is also the audio engine setting in the game's Audio settings (Get/SetAudioLatency,
// 0x100093040/0x100093560), which the game saves to Rocksmith.ini; Wwise reads it at start-up only. At 1 the
// engine runs with 2 and only the sink's ring with 1 (audio_ring.c).
// RS_AUDIO_LOG=1 logs what it does to stderr (logs/last-run.log), and why an input device didn't open
// (audio_trace.c).
#ifndef RS_AUDIO_H
#define RS_AUDIO_H

#include <stdatomic.h>
#include <stdint.h>

#include "rc_ov.h"

// Wwise's AkPlatformInitSettings copy (0x101907da0): +0x54 uNumRefillsInVoice (u16), the ring is
// refills x 256 frames per channel (sink Init 0x100aa7be0).
#define AK_PD_REFILLS 0x101907df4ull
#define AK_REFILL_FRAMES 256u

// The output device's I/O buffer size in frames as last applied or read back (0: not known yet). Written by
// audio_out.c when the Wwise sink picks its device, read by audio_in.c on the Wwise thread.
extern _Atomic uint32_t rs_audio_out_frames;

// Called from the AudioUnitSetProperty import thunk (thunks/gen_thunks.py) when the game sets an output
// unit's current device (kAudioOutputUnitProperty_CurrentDevice), before the call.
void rs_audio_output_device(uint32_t device);

// Called from the AudioObjectGetPropertyData import thunk in place of the call: the same call, with the system
// object's device list (kAudioHardwarePropertyDevices) input devices first (audio_devs.c).
int32_t rs_audio_get_property_data(uint32_t obj, const void *addr, uint32_t qsize, const void *qdata,
                                   uint32_t *size, void *data);
// The same for AudioObjectGetPropertyDataSize and AudioObjectSetPropertyData (audio_trace.c).
int32_t rs_audio_get_property_data_size(uint32_t obj, const void *addr, uint32_t qsize, const void *qdata,
                                        uint32_t *size);
int32_t rs_audio_set_property_data(uint32_t obj, const void *addr, uint32_t qsize, const void *qdata,
                                   uint32_t size, const void *data);
// Records a property read made while SoundInput opens an input device (audio_trace.c, RS_AUDIO_LOG=1).
void rs_audio_trace_get(uint32_t obj, const void *addr, int32_t st, uint32_t size, const void *data);

// The sink's ring in refills of 256 frames: LatencyBuffer, also when the engine runs with more (audio_ring.c).
uint32_t rs_audio_ring_refills(void);

int rs_audio_log_on(void);
void rs_audio_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// An environment switch: unset or empty = dflt; "0", "n...", "f..." = 0; anything else = 1.
int rs_audio_env_on(const char *name, int dflt);

#endif
