// Checks that saved states are the chain make_presets.py wrote: a switch of the last block on the second path must change
// the sound. An import of an older setlist shows here, as that switch does nothing there.
//   check_states STATE.plist SWITCH KNOB
#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <math.h>
#define N 256
static float in[N];
static double ph, when;
static AudioUnit u;
static OSStatus cb(void *r, AudioUnitRenderActionFlags *f, const AudioTimeStamp *t, UInt32 b, UInt32 n, AudioBufferList *io) {
  for (UInt32 c = 0; c < io->mNumberBuffers; c++) memcpy(io->mBuffers[c].mData, in, n * 4);
  return 0;
}
static double run(int blocks) {
  static float L[N], R[N];
  double sum = 0;
  for (int k = 0; k < blocks; k++) {
    for (int i = 0; i < N; i++) { in[i] = 0.1f * (float)sin(ph) * (fmod(when + i, 24000) < 2400); ph += 2 * M_PI * 220 / 48000; }
    struct { UInt32 n; AudioBuffer b[2]; } bl = {2, {{1, sizeof L, L}, {1, sizeof R, R}}};
    AudioTimeStamp ts = {0};
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    ts.mSampleTime = when;
    when += N;
    AudioUnitRenderActionFlags fl = 0;
    AudioUnitRender(u, &fl, &ts, 0, N, (AudioBufferList *)&bl);
    for (int i = 0; i < N; i++) sum += (double)L[i] * L[i];
  }
  return 10 * log10(sum / (blocks * N) + 1e-14);
}
int main(int argc, char **argv) { @autoreleasepool {
  AudioComponentDescription d = {'aumf', '6HPA', 'LSIX', 0, 0};
  AudioComponent c = AudioComponentFindNext(NULL, &d);
  if (!c || AudioComponentInstanceNew(c, &u)) return 2;
  AudioStreamBasicDescription f = {48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
  AudioUnitSetProperty(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
  AudioUnitSetProperty(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  UInt32 mf = N;
  AudioUnitSetProperty(u, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mf, 4);
  AURenderCallbackStruct r = {cb, NULL};
  AudioUnitSetProperty(u, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &r, sizeof r);
  if (AudioUnitInitialize(u)) return 2;
  NSData *dd = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]];
  id pl = [NSPropertyListSerialization propertyListWithData:dd options:0 format:NULL error:NULL];
  CFPropertyListRef p = (__bridge CFPropertyListRef)pl;
  if (AudioUnitSetProperty(u, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &p, sizeof p)) return 2;
  AudioUnitSetParameter(u, atoi(argv[3]), kAudioUnitScope_Global, 0, 0.7f, 0);
  run(400);
  double off = run(800);
  AudioUnitSetParameter(u, atoi(argv[2]), kAudioUnitScope_Global, 0, 1.f, 0);
  run(400);
  double on = run(800);
  printf("last block off %.1f dB, on %.1f dB\n", off, on);
  _exit(fabs(on - off) > 0.3 ? 0 : 1);
}}
