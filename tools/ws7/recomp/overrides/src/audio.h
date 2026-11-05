// Override module `audio`: lower guitar-to-sound latency and the guitar input (audio_out.c, audio_in.c,
// audio_chan.c, audio_devs.c, audio_ring.c, audio_trace.c).
//
// The guitar reaches the speakers through three buffers the game sets up (Wwise 2013-era integration):
//   * the cable's I/O buffer (128 frames) and a 5-slot queue of such buffers that the Wwise thread drains
//     (SoundInput, writer 0x10133b280, reader 0x10133b310);
//   * the Wwise sink's ring, LatencyBuffer x 256 frames (Rocksmith.ini [Audio] LatencyBuffer, Wwise
//     uNumRefillsInVoice at 0x101907df4), filled by the Wwise LEngine thread (entry 0x100b049b0);
//   * the output device's I/O buffer, which the game never sets (the system default, 512 frames).
// This module sets the output device's buffer (RS_AUDIO_FRAMES, default 256; never above half the ring,
// see audio_out.c), runs the LEngine thread as a real-time thread in the output device's
// audio workgroup (RS_AUDIO_RT), keeps the input queue from holding stale audio (RS_AUDIO_INPUT_TRIM), gives
// each device one UID string so the device scan doesn't skip an interface as failed (RS_AUDIO_STABLE_UIDS), and
// lists input devices first so an unreadable output-only device can't end that scan (RS_AUDIO_INPUTS_FIRST).
// LatencyBuffer is also the audio engine setting in the game's Audio settings (Get/SetAudioLatency,
// 0x100093040/0x100093560), which the game saves to Rocksmith.ini; Wwise reads it at start-up only. At 1 the
// engine runs with 2 (double-buffered voices) and only the sink's ring with 1 (audio_ring.c).
// RS_AUDIO_LOG=1 logs what it does to stderr (logs/last-run.log), and why an input device didn't open
// (audio_trace.c).
#ifndef RS_AUDIO_H
#define RS_AUDIO_H

#include <stdatomic.h>
#include <stdint.h>

#include "rc_ov.h"
#include "rs_audio_host.h"  // switches, log, device buffer, real-time thread, device list (shared with Learn & Play)

// Wwise's AkPlatformInitSettings copy (0x101907da0): +0x54 uNumRefillsInVoice (u16), the ring is
// refills x 256 frames per channel (sink Init 0x100aa7be0).
#define AK_PD_REFILLS (RC_ADDR(ak_platform_settings) + RC_OFF(ak_ps_refills))
#define AK_REFILL_FRAMES 256u

// The output device's I/O buffer size in frames as last applied or read back (0: not known yet). Written by
// audio_out.c when the Wwise sink picks its device, read by audio_in.c on the Wwise thread.
extern _Atomic uint32_t rs_audio_out_frames;

// Called from the AudioUnitSetProperty import thunk (thunks/gen_thunks.py) when the game sets an output
// unit's current device (kAudioOutputUnitProperty_CurrentDevice), before the call.
void rs_audio_output_device(uint32_t device);
// The same thunk, after that: the unit itself. And the AUGraphStart and AUGraphStop thunks: the sink's graph
// while it runs, NULL once the game stops it. With both known the sink follows the default output.
void rs_audio_output_unit(void *unit);
void rs_audio_output_graph(void *graph);

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

// With RS_AUDIO_LOG=1 the inputs' peak and clips every few seconds (audio_chan.c). Called by the input's reader.
void rs_audio_chan_report(void);

// The sink's ring in refills of 256 frames: LatencyBuffer, also when the engine runs with more (audio_ring.c).
uint32_t rs_audio_ring_refills(void);

#endif
