// rs_audio_input: settings and the channel and gain on made-up buffers. No device, no game.
#include "rs_audio_input.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-4f)

static const char k_file[] =
    "# guitar-input.ini\n"
    "; another comment\n"
    "[HX Stomp]\n"
    "Channel=5\n"
    "GainDb=20\n"
    "\n"
    "[ Steinberg UR22mkII ]\r\n"
    "  channel = 2 \r\n"
    "[Loud]\n"
    "GainDb=-20\n"
    "[Too much]\n"
    "GainDb=100\n"
    "[Too little]\n"
    "GainDb=-100\n"
    "[Broken]\n"
    "Channel=0\n"
    "GainDb=loud\n"
    "no equals sign\n"
    "Colour=red\n"
    "[Not a number]\n"
    "GainDb=nan\n"
    "Channel=5x\n";

static void test_parse(void) {
  rs_audio_input in;
  rs_audio_input_parse(k_file, "HX Stomp", &in);
  CHECK(in.channel == 4 && NEAR(in.gain, 10.0f));
  // The UR22's name in the system ends in a space.
  rs_audio_input_parse(k_file, "Steinberg UR22mkII ", &in);
  CHECK(in.channel == 1 && in.gain == 1.0f);
  rs_audio_input_parse(k_file, "hx stomp", &in);
  CHECK(in.channel == 4);
  rs_audio_input_parse(k_file, "Loud", &in);
  CHECK(in.channel == -1 && NEAR(in.gain, 0.1f));
  rs_audio_input_parse(k_file, "Too much", &in);
  CHECK(NEAR(in.gain, powf(10.0f, RS_AUDIO_INPUT_MAX_DB / 20.0f)));
  rs_audio_input_parse(k_file, "Too little", &in);
  CHECK(NEAR(in.gain, powf(10.0f, RS_AUDIO_INPUT_MIN_DB / 20.0f)));
  rs_audio_input_parse(k_file, "Broken", &in);
  CHECK(in.channel == -1 && in.gain == 1.0f);
  rs_audio_input_parse(k_file, "Not a number", &in);
  CHECK(in.channel == -1 && in.gain == 1.0f);
  rs_audio_input_parse(k_file, "Another device", &in);
  CHECK(in.channel == -1 && in.gain == 1.0f);
  rs_audio_input_parse(NULL, "HX Stomp", &in);
  CHECK(in.channel == -1 && in.gain == 1.0f);
  rs_audio_input_parse("\xef\xbb\xbf[A]\nChannel=3", "A", &in);  // a byte-order mark, no last newline
  CHECK(in.channel == 2);
  rs_audio_input_parse("Channel=3\n[A]\n", "A", &in);  // before any section
  CHECK(in.channel == -1);
}

// The number format doesn't follow the locale ("18,5" in many).
static void test_locale(void) {
  rs_audio_input in;
  const char *was = setlocale(LC_NUMERIC, NULL);
  char keep[64];
  snprintf(keep, sizeof keep, "%s", was);
  if (!setlocale(LC_NUMERIC, "fi_FI.UTF-8")) setlocale(LC_NUMERIC, "de_DE.UTF-8");
  rs_audio_input_parse("[A]\nGainDb=6.5\n", "A", &in);
  CHECK(NEAR(in.gain, powf(10.0f, 6.5f / 20.0f)));
  setlocale(LC_NUMERIC, keep);
}

static void test_for(void) {
  char path[] = "/tmp/rs_audio_input_test.XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  CHECK(write(fd, k_file, sizeof k_file - 1) == (ssize_t)sizeof k_file - 1);
  close(fd);
  rs_audio_input in;
  setenv("RS_AUDIO_INPUT_FILE", path, 1);
  rs_audio_input_for("HX Stomp", &in);
  CHECK(in.channel == 4 && NEAR(in.gain, 10.0f));
  // The switches win, for every device.
  setenv("RS_AUDIO_INPUT_CHANNEL", "6", 1);
  setenv("RS_AUDIO_INPUT_GAIN_DB", "0", 1);
  rs_audio_input_for("HX Stomp", &in);
  CHECK(in.channel == 5 && in.gain == 1.0f);
  rs_audio_input_for("Another device", &in);
  CHECK(in.channel == 5);
  setenv("RS_AUDIO_INPUT", "0", 1);  // turned off, the switches and the file are not read
  rs_audio_input_for("HX Stomp", &in);
  CHECK(in.channel == -1 && in.gain == 1.0f);
  unsetenv("RS_AUDIO_INPUT");
  setenv("RS_AUDIO_INPUT_CHANNEL", "many", 1);
  unsetenv("RS_AUDIO_INPUT_GAIN_DB");
  rs_audio_input_for("HX Stomp", &in);
  CHECK(in.channel == 4 && NEAR(in.gain, 10.0f));
  unsetenv("RS_AUDIO_INPUT_CHANNEL");
  // No file: nothing set.
  unlink(path);
  rs_audio_input_for("HX Stomp", &in);
  CHECK(in.channel == -1 && in.gain == 1.0f);
  unsetenv("RS_AUDIO_INPUT_FILE");
}

// Without RS_AUDIO_INPUT_FILE the file is the one in the RS Re-ARMed folder.
static void test_default_file(void) {
  char home[] = "/tmp/rs_audio_input_home.XXXXXX", dir[256], file[300];
  CHECK(mkdtemp(home));
  const char *parts[] = {"Library", "Application Support", "RS Re-ARMed"};
  snprintf(dir, sizeof dir, "%s", home);
  for (int i = 0; i < 3; i++) {
    strlcat(dir, "/", sizeof dir);
    strlcat(dir, parts[i], sizeof dir);
    CHECK(mkdir(dir, 0700) == 0);
  }
  snprintf(file, sizeof file, "%s/guitar-input.ini", dir);
  FILE *f = fopen(file, "w");
  CHECK(f && fputs(k_file, f) >= 0 && fclose(f) == 0);
  char *keep = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
  setenv("HOME", home, 1);
  rs_audio_input in;
  rs_audio_input_for("HX Stomp", &in);
  CHECK(in.channel == 4 && NEAR(in.gain, 10.0f));
  unlink(file);
  rs_audio_input_for("HX Stomp", &in);
  CHECK(in.channel == -1 && in.gain == 1.0f);
  if (keep) setenv("HOME", keep, 1);
  free(keep);
  for (int i = 2; i >= 0; i--) {
    CHECK(rmdir(dir) == 0);
    *strrchr(dir, '/') = 0;
  }
  CHECK(rmdir(home) == 0);
}

static void test_channel(void) {
  rs_audio_input in = {4, 1.0f};
  CHECK(rs_audio_input_channel(&in, 8, "x") == 4);
  CHECK(rs_audio_input_channel(&in, 5, "x") == 4);
  CHECK(rs_audio_input_channel(&in, 4, "x") == 0);  // the device hasn't that many
  in.channel = -1;
  CHECK(rs_audio_input_channel(&in, 8, "x") == -1);
}

// An interleaved block whose channel c, frame i is (c + 1) / 100 + i / 100000.
#define VALUE(c, i) (((c) + 1) / 100.0f + (i) / 100000.0f)
static void fill(float *p, uint32_t channels, uint32_t first, uint32_t frames) {
  for (uint32_t i = 0; i < frames; i++)
    for (uint32_t c = 0; c < channels; c++) p[i * channels + c] = VALUE(first + c, i);
}

static void test_take(void) {
  enum { FRAMES = 128 };
  static float dev[FRAMES * 8], out[FRAMES];
  fill(dev, 8, 0, FRAMES);
  AudioBufferList one = {1, {{8, sizeof dev, dev}}};
  for (uint32_t c = 0; c < 8; c++) {
    CHECK(rs_audio_input_take(&one, c, 1.0f, out, FRAMES, NULL) == FRAMES);
    for (uint32_t i = 0; i < FRAMES; i++) CHECK(out[i] == VALUE(c, i));
  }
  CHECK(rs_audio_input_take(&one, 8, 1.0f, out, FRAMES, NULL) == 0);
  // The gain, and at most max_frames.
  out[16] = 42.0f;
  CHECK(rs_audio_input_take(&one, 4, 2.0f, out, 16, NULL) == 16);
  CHECK(out[15] == 2.0f * VALUE(4, 15) && out[16] == 42.0f);

  // Channels across streams, of different widths and an empty one.
  struct {
    UInt32 n;
    AudioBuffer b[3];
  } three;
  static float a[FRAMES * 2], z[FRAMES * 3];
  fill(a, 2, 0, FRAMES);
  fill(z, 3, 2, FRAMES);
  three.n = 3;
  three.b[0] = (AudioBuffer){2, sizeof a, a};
  three.b[1] = (AudioBuffer){0, 0, NULL};
  three.b[2] = (AudioBuffer){3, sizeof z, z};
  const AudioBufferList *l = (const AudioBufferList *)&three;
  for (uint32_t c = 0; c < 5; c++) {
    CHECK(rs_audio_input_take(l, c, 1.0f, out, FRAMES, NULL) == FRAMES);
    CHECK(out[7] == VALUE(c, 7));
  }
  CHECK(rs_audio_input_take(l, 5, 1.0f, out, FRAMES, NULL) == 0);

  AudioBufferList none = {1, {{2, 1024, NULL}}};
  CHECK(rs_audio_input_take(&none, 0, 1.0f, out, FRAMES, NULL) == 0);
}

static void test_clip(void) {
  static float dev[4] = {0.5f, -0.5f, 0.05f, -0.2f}, out[4];
  AudioBufferList one = {1, {{1, sizeof dev, dev}}};
  rs_audio_input_stats st = {0};
  CHECK(rs_audio_input_take(&one, 0, 4.0f, out, 4, &st) == 4);
  CHECK(out[0] == 1.0f && out[1] == -1.0f && NEAR(out[2], 0.2f) && NEAR(out[3], -0.8f));
  CHECK(atomic_load(&st.clips) == 2 && atomic_load(&st.frames) == 4);
  uint32_t bits = atomic_load(&st.peak);
  float peak;
  memcpy(&peak, &bits, sizeof peak);
  CHECK(peak == 2.0f);  // before the clip: how far over it went
  // A quieter block doesn't lower the peak, and the report starts over.
  rs_audio_input_note(&st, 0.25f, 0, 4);
  CHECK(atomic_load(&st.peak) == bits);
  CHECK(atomic_load(&st.frames) == 8);
  rs_audio_input_report(&st, 8, "x");  // RS_AUDIO_LOG=1 (main)
  CHECK(atomic_load(&st.peak) == 0 && atomic_load(&st.clips) == 0 && atomic_load(&st.frames) == 0);
  // Not enough frames yet: nothing happens.
  rs_audio_input_note(&st, 0.25f, 1, 4);
  rs_audio_input_report(&st, 8, "x");
  CHECK(atomic_load(&st.frames) == 4 && atomic_load(&st.clips) == 1);
}

int main(void) {
  setenv("RS_AUDIO_LOG", "1", 1);
  unsetenv("RS_AUDIO_INPUT");
  unsetenv("RS_AUDIO_INPUT_FILE");
  unsetenv("RS_AUDIO_INPUT_CHANNEL");
  unsetenv("RS_AUDIO_INPUT_GAIN_DB");
  test_parse();
  test_locale();
  test_for();
  test_default_file();
  test_channel();
  test_take();
  test_clip();
  puts("rs_audio_input: ok");
  return 0;
}
