// The guitar input's channel and gain, for both editions: which channel of an audio interface the game
// listens to, and how much it is boosted before the game sees it.
//
// The game listens to the first channel of a device (the Mac game to the second with MonoToStereoChannel,
// on a device of two channels), and it can only change a device's level through the device's own input
// volume, which most interfaces don't have.
//
// The settings are per device, as more than one interface can be connected. Both editions read them from
// ~/Library/Application Support/RS Re-ARMed/guitar-input.ini (RS_AUDIO_INPUT_FILE names another file), one
// section per device, by its name in the system (upper or lower case, and spaces around it, don't matter):
//
//   [HX Stomp]
//   Channel=5
//   GainDb=0
//
// Channels count from 1, as interfaces number them. Switches, which apply to every device and win over
// the file:
//   RS_AUDIO_INPUT_CHANNEL   the channel (from 1)
//   RS_AUDIO_INPUT_GAIN_DB   the gain in dB
//   RS_AUDIO_INPUT=0         none of this: the game's input as it is
#ifndef RS_AUDIO_INPUT_H
#define RS_AUDIO_INPUT_H

#include <CoreAudio/CoreAudio.h>
#include <stdatomic.h>
#include <stdint.h>

#define RS_AUDIO_INPUT_MIN_DB (-24.0f)
#define RS_AUDIO_INPUT_MAX_DB 36.0f
#define RS_AUDIO_INPUT_MAX_CHANNEL 256

typedef struct {
  int channel;  // from 0; -1: not set
  float gain;   // linear; 1 when not set
} rs_audio_input;

// What an input has done since the last report: written on the I/O thread, read by rs_audio_input_report.
typedef struct {
  _Atomic uint32_t peak;    // the largest sample before the clip, as float bits
  _Atomic uint32_t clips;   // samples clipped
  _Atomic uint32_t frames;  // frames seen
} rs_audio_input_stats;

// The settings of the device called name, from the text of a settings file (NULL: none). A line that
// isn't understood is logged and left out; a gain is kept within the limits above.
void rs_audio_input_parse(const char *text, const char *name, rs_audio_input *out);
// The settings of the device called name, from the settings file and the switches. Not for the I/O thread.
void rs_audio_input_for(const char *name, rs_audio_input *out);
// The channel to read of a device of `channels` channels: the one set, the first when the device hasn't
// that many (logged), -1 when none is set. Not for the I/O thread.
int rs_audio_input_channel(const rs_audio_input *in, uint32_t channels, const char *name);

// The inputs the game lists. It stops at 10 and its list doesn't scroll, so the capture devices go in order of what
// a player is likely to plug into, and an interface gets an entry per channel, unless the settings name its channel.
// Each keeps at least one. RS_AUDIO_CHANNEL_ENTRIES=N lists N (default 10; 0: the devices as they are).
#define RS_AUDIO_LIST_DEFAULT 10
enum { RS_AUDIO_RANK_CABLE, RS_AUDIO_RANK_INTERFACE, RS_AUDIO_RANK_BUILT_IN, RS_AUDIO_RANK_OTHER, RS_AUDIO_RANK_VIRTUAL,
       RS_AUDIO_RANKS };

typedef struct {
  AudioObjectID dev;
  int channel;  // from 0; -1: the device as it is
} rs_audio_entry;

// N, 0 when off (RS_AUDIO_INPUT=0 too).
int rs_audio_entries_budget(void);
// The entries of the capture devices among devs, in that order; returns the count.
int rs_audio_input_entries(const AudioObjectID *devs, int n, int budget, rs_audio_entry *out, int max);
// How many entries each of m devices gets, of `want`, within budget: an interface one more in turn, so a big one
// can't take the room of a small one, the others one.
void rs_audio_entry_counts(const int *rank, const int *want, int m, int budget, int *got);
// 1 for the Real Tone cable.
int rs_audio_is_cable(AudioObjectID dev);

// An input's level. The game's calibration is a loop: it raises the level, 0..1, in small steps until its meter
// reaches the top. It reaches a device through its input volume, which most interfaces don't have and which on the
// others is the player's own setting. So any input but the cable has a level of ours, as a gain: 0.375, where the
// game starts a device, is unity, and 1 is RS_AUDIO_LEVEL_MAX_DB. RS_AUDIO_INPUT_LEVEL=0: the game's own way.
#define RS_AUDIO_LEVEL_UNITY 0.375f
#define RS_AUDIO_LEVEL_MAX_DB 24.0f
int rs_audio_level_on(void);
float rs_audio_level_gain(float level);

// Levels saved before this meant nothing, and the game sets them at every start. So an input stays at unity until
// calibration has run once: a run of level changes on one input, which a start doesn't make, as it sets them all.
// Then the file input-level-calibrated in the edition's folder keeps that.
#define RS_AUDIO_LEVEL_RUN 4
typedef struct {
  uintptr_t who;
  float level;
  int run;
} rs_audio_level_watch;

// 1 when this write, of input `who`, shows calibration.
int rs_audio_level_watch_note(rs_audio_level_watch *w, uintptr_t who, float level);
// A write of the game's, and the gain of a level now. Not for the I/O thread, the first; the second is.
void rs_audio_level_set(uintptr_t who, float level);
float rs_audio_level_applied(float level);

// One sample with the gain, kept within -1..1 as an interface's converter keeps it. peak and clips add up
// a block for rs_audio_input_note.
static inline float rs_audio_input_apply(float v, float gain, float *peak, uint32_t *clips) {
  v *= gain;
  float a = v < 0 ? -v : v;
  if (a > *peak) *peak = a;
  if (a <= 1.0f) return v;
  (*clips)++;
  return v < 0 ? -1.0f : 1.0f;
}

// Adds a block to st. For the I/O thread.
void rs_audio_input_note(rs_audio_input_stats *st, float peak, uint32_t clips, uint32_t frames);
// Copies channel (counted across the list's buffers, each interleaved) to out with the gain, at most
// max_frames frames, and notes the block in st (may be NULL). Returns the frames copied, 0 when the list
// has no such channel. For the I/O thread.
uint32_t rs_audio_input_take(const AudioBufferList *list, uint32_t channel, float gain, float *out,
                             uint32_t max_frames, rs_audio_input_stats *st);
// With RS_AUDIO_LOG=1 and at least `every` frames seen: logs the peak in dBFS and the clips, and starts
// over. The log is not for the I/O thread, so this is called from the thread that reads the audio.
void rs_audio_input_report(rs_audio_input_stats *st, uint32_t every, const char *name);

#endif
