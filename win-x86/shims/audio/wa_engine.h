// CoreAudio side of the Learn & Play audio faces (WASAPI in wa_com.c, winmm in wa_winmm.c): the
// endpoints the game sees and one stream per opened endpoint. Host code only; nothing here
// knows the guest except the event HANDLE it signals (w32_event_signal).
//
// A stream is a HAL output unit (AUHAL) on one device, in or out, and a ring of float frames
// between it and the guest:
//   render   the guest writes (IAudioRenderClient), the device's I/O thread reads;
//   capture  the device's I/O thread writes, the guest reads (IAudioCaptureClient, waveIn).
// The ring holds what WASAPI calls the endpoint buffer: one period more than the guest's own
// buffer in event mode, as Windows' exclusive mode double-buffers. The device's I/O buffer
// follows the Mac build's policy (shims/coreaudio/rs_audio_host.c): RS_AUDIO_FRAMES, default 256
// frames, at most half the ring, so a callback never takes more than the guest has queued.
// Event mode signals the guest's event from the I/O thread whenever a period of space (render) or
// of frames (capture) is there, which is when Windows signals it.
#ifndef WA_ENGINE_H
#define WA_ENGINE_H

#include <CoreAudio/CoreAudio.h>
#include <stddef.h>
#include <stdint.h>

enum { WA_RENDER = 0, WA_CAPTURE = 1, WA_ALL = 2 };

// A sample format as WAVEFORMATEX(TENSIBLE) states it.
typedef struct {
  uint32_t rate;
  uint16_t channels;
  uint16_t bits;        // container bits per sample (8, 16, 24, 32)
  uint16_t valid_bits;  // significant bits (24 in a 32-bit container)
  uint16_t is_float;
  uint16_t block;       // bytes per frame
  uint32_t mask;        // channel mask (0: unspecified)
} wa_fmt;

// Parses a guest WAVEFORMATEX / WAVEFORMATEXTENSIBLE; 0 when the format is not linear PCM or
// IEEE float this layer converts (8/16/24/32-bit integer, 32-bit float).
int wa_fmt_parse(const void *wfx, wa_fmt *f);
// Writes f as a 40-byte WAVEFORMATEXTENSIBLE.
void wa_fmt_write(const wa_fmt *f, void *wfx);
// The format the device mixes in: 32-bit float, the stream rate the game asks for (48 kHz),
// the endpoint's channels.
void wa_fmt_mix(uint16_t channels, wa_fmt *f);
// Guest samples <-> interleaved float.
void wa_to_float(const wa_fmt *f, const void *src, float *dst, uint32_t frames, float gain);
void wa_from_float(const wa_fmt *f, const float *src, void *dst, uint32_t frames, float gain);

// An endpoint: a CoreAudio device seen in one direction (a device with inputs and outputs is two
// endpoints, as on Windows).
typedef struct {
  AudioObjectID dev;
  int flow;              // WA_RENDER or WA_CAPTURE
  uint16_t channels;     // in that direction
  uint16_t vid, pid;     // USB vendor / product (0 when not a USB device or not known)
  double rate;           // the device's nominal rate
  char name[128];        // UTF-8
  char uid[192];         // kAudioDevicePropertyDeviceUID
  int in_ch;             // a channel of the list of inputs (rs_audio_input.h), from 0; -1: the device
} wa_endpoint;

// The endpoint name the game is given, in place: the Real Tone cable (12BA:00FF) is "Rocksmith USB
// Guitar Adapter", any other device's "Rocksmith" becomes "RS" (the game's cable warning, wa_engine.c).
void wa_game_name(char *name, size_t n, uint16_t vid, uint16_t pid);

// Endpoints in flow (WA_ALL: both), system order, inputs before outputs; returns the count.
int wa_endpoints(int flow, wa_endpoint *out, int max);
// The system's default device for flow; 0 when there is none.
int wa_default_endpoint(int flow, wa_endpoint *out);
// fn is called, on a CoreAudio thread, when the system's default output changes. Once.
void wa_watch_default_output(void (*fn)(void));
// The endpoint with this id (wa_endpoint_id); 0 when it is gone.
int wa_endpoint_by_id(const char *id, wa_endpoint *out);
// Windows-style endpoint id: "{0.0.0.00000000}.{<uid>}" (render) / "{0.0.1.00000000}.{<uid>}".
void wa_endpoint_id(const wa_endpoint *ep, char *buf, size_t n);

typedef struct wa_stream wa_stream;

typedef struct {
  uint32_t buffer_frames;  // the WASAPI buffer (GetBufferSize)
  uint32_t period_frames;  // event period / capture packet
  uint32_t ring_frames;    // what the ring holds
  uint32_t io_frames;      // the device's I/O buffer as applied
  uint32_t latency_frames; // device + stream latency + safety offset + I/O buffer
  double device_rate;
} wa_stream_info;

// Opens the device for the stream (not started). buffer_frames: the WASAPI buffer; period_frames:
// the event period (event mode) or 0; event_mode: the guest waits on an event. 0 on failure.
wa_stream *wa_stream_open(const wa_endpoint *ep, const wa_fmt *fmt, uint32_t buffer_frames, uint32_t period_frames,
                          int event_mode);
void wa_stream_close(wa_stream *s);
void wa_stream_info_get(wa_stream *s, wa_stream_info *i);
// The guest event HANDLE to signal (0: none).
void wa_stream_set_event(wa_stream *s, uint32_t handle);
// A host callback in place of an event (waveIn): called on the I/O thread when a period is there.
void wa_stream_set_notify(wa_stream *s, void (*fn)(void *), void *arg);
int wa_stream_start(wa_stream *s);  // 0 on success
int wa_stream_stop(wa_stream *s);
void wa_stream_reset(wa_stream *s);  // drops what the ring holds (stopped streams)
int wa_stream_running(wa_stream *s);
// Software gain (the endpoint volume) applied to the guest's samples.
void wa_stream_set_gain(wa_stream *s, float gain);

// Render: frames queued and not yet played (GetCurrentPadding).
uint32_t wa_render_padding(wa_stream *s);
// Render: writes frames of guest-format samples (src 0: silence). When the ring lacks room the
// oldest queued frames are dropped (event mode; Windows' exclusive mode would play them late).
void wa_render_write(wa_stream *s, const void *src, uint32_t frames);
// Render: frames played so far (device position).
uint64_t wa_render_played(wa_stream *s);
uint32_t wa_render_underruns(wa_stream *s);

// Capture: frames waiting (after the stale-input trim when trim is set).
uint32_t wa_capture_available(wa_stream *s, int trim);
// Capture: copies up to frames into dst in the guest's format without consuming; returns the count.
uint32_t wa_capture_peek(wa_stream *s, void *dst, uint32_t frames);
void wa_capture_consume(wa_stream *s, uint32_t frames);
// Capture: frames read so far (device position of the next packet) and frames dropped.
uint64_t wa_capture_position(wa_stream *s);
uint32_t wa_capture_overruns(wa_stream *s);

// The device of the stream (for the workgroup of the thread that feeds it).
AudioObjectID wa_stream_device(wa_stream *s);

// Endpoint volume (wa_com.c): one software gain per endpoint, shared by IAudioEndpointVolume
// and the winmm mixer's volume and mute controls. 0..1; the gain is 0 while muted.
float wa_endpoint_volume(const wa_endpoint *ep);
void wa_endpoint_set_volume(const wa_endpoint *ep, float scalar);
int wa_endpoint_mute(const wa_endpoint *ep);
void wa_endpoint_set_mute(const wa_endpoint *ep, int mute);
float wa_endpoint_gain(const wa_endpoint *ep);

#endif
