// The test input, for both editions: a capture device that isn't there, for game runs with nobody to play.
//
// RS_AUDIO_FAKE_INPUT names its channels, comma-separated, each one of:
//   PATH                    a WAV file, looped: 44.1 or 48 kHz, mono or stereo (mixed to one channel), 16-bit,
//                           24-bit or 32-bit float
//   tone:MIDI[:SECONDS]     a plucked string's note at MIDI (a fraction is cents: 40.5 is E2 50 cents sharp),
//                           decaying, again every SECONDS (default 2)
//   calibrate[:MIDI]        what the game's calibration needs: RS_AUDIO_FAKE_CALIBRATE_PLAY seconds of loud plucks
//                           of MIDI (default 40, E2), then RS_AUDIO_FAKE_CALIBRATE_QUIET of silence ("mute the
//                           strings"), over and over
//   silence                 nothing
// e.g. RS_AUDIO_FAKE_INPUT=tone:35,silence. Unset, empty or 0: no test input, and nothing of this runs (the
// calls below go straight to the system's). RS_AUDIO_FAKE_GAIN_DB raises or lowers every channel (default 0;
// a tone peaks at -6 dBFS). An entry that can't be used is logged and is silence, so the channels stay where the
// switch put them.
//
// The device is "RS Re-ARMed Test Input" (rs_audio_host.h's calls list it first), an interface of as many input
// channels as the switch names and no outputs: the list of inputs gives it an entry per channel as any interface
// (rs_audio_input.h), and its input level is RS Re-ARMed's as on any input but the cable. It runs at 44.1 or
// 48 kHz, as the game sets it, with the I/O buffer the game sets, on a real-time thread of its own: its clock is the
// host's, kept at the default output device's measured rate while that runs, so the game's input queue neither
// fills nor runs dry. An I/O function registered on it is called as the HAL calls one, with its input in one
// interleaved buffer.
//
// The log says "[audio] fake input: N channels: ..." when it is on; RS_AUDIO_LOG=1 says what each channel holds
// and what the game does with the device.
#ifndef RS_AUDIO_FAKE_H
#define RS_AUDIO_FAKE_H

#include <CoreAudio/CoreAudio.h>
#include <stddef.h>
#include <stdint.h>

#define RS_AUDIO_FAKE_NAME "RS Re-ARMed Test Input"
#define RS_AUDIO_FAKE_UID "rs-rearmed-test-input"
// Ids no device of the system has (the HAL counts its objects up from 1), within what an entry id holds
// (RS_AUDIO_ENTRY_MAX_DEV).
#define RS_AUDIO_FAKE_DEV 0x3fff00u
#define RS_AUDIO_FAKE_STREAM 0x3fff01u
#define RS_AUDIO_FAKE_MAX_CHANNELS 8
#define RS_AUDIO_FAKE_MAX_FRAMES 4096u  // the largest I/O buffer it takes
#define RS_AUDIO_FAKE_PEAK 0.5f         // a tone's peak, -6 dBFS
#define RS_AUDIO_FAKE_RATES 2           // 44100 and 48000
#define RS_AUDIO_FAKE_CALIBRATE_PLAY 12.0f   // calibrate: seconds of plucks, one every half second
#define RS_AUDIO_FAKE_CALIBRATE_QUIET 15.0f  // then seconds of silence
#define RS_AUDIO_FAKE_CALIBRATE_PEAK 0.9f    // its plucks' peak, about -1 dBFS

enum { RS_AUDIO_FAKE_SILENCE, RS_AUDIO_FAKE_TONE, RS_AUDIO_FAKE_WAV, RS_AUDIO_FAKE_CALIBRATE };

typedef struct {
  int kind;
  float midi, seconds;  // a tone
  char path[1024];      // a WAV file
} rs_audio_fake_source;

// A channel's sound at one rate: n mono samples, played over and over. s NULL: silence.
typedef struct {
  float *s;
  uint32_t n;
} rs_audio_fake_loop;

// The channels the switch's text names (up to max), 0 for none. An entry not understood is logged and is silence.
int rs_audio_fake_parse(const char *text, rs_audio_fake_source *out, int max);
// One entry as the log names it.
void rs_audio_fake_describe(const rs_audio_fake_source *src, char *buf, size_t n);
// A tone's note at rate, seconds long; 0 when midi or seconds is out of range (0..127, 0.05..60 s).
int rs_audio_fake_tone(float midi, float seconds, double rate, rs_audio_fake_loop *out);
// calibrate's loop at rate: the plucks, then the silence.
int rs_audio_fake_calibrate(float midi, double rate, rs_audio_fake_loop *out);
// A WAV file's sound at rate, from its bytes or its path; 0 with why in err when it isn't one this reads.
int rs_audio_fake_wav(const void *data, size_t size, double rate, rs_audio_fake_loop *out, char *err, size_t nerr);
int rs_audio_fake_wav_file(const char *path, double rate, rs_audio_fake_loop *out, char *err, size_t nerr);
void rs_audio_fake_loop_free(rs_audio_fake_loop *l);

// The test input's channels, 0 when it is off. The switch is read once.
int rs_audio_fake_channels(void);
// 1 for the test input's device and stream.
static inline int rs_audio_fake_is(AudioObjectID obj) {
  return obj == RS_AUDIO_FAKE_DEV || obj == RS_AUDIO_FAKE_STREAM;
}

#endif
