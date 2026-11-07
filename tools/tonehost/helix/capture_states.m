// Saves the state of each preset of an imported setlist to a file, with no window: MIDI selects a preset, and an Audio
// Unit's ClassInfo is what the host loads later. The setlist is needed only for this.
//   capture_states SETLIST_INDEX OUTDIR name0 name1 ...
#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#define N 256
static float in[N];
static OSStatus cb(void *r, AudioUnitRenderActionFlags *f, const AudioTimeStamp *t, UInt32 b, UInt32 n, AudioBufferList *io) {
  for (UInt32 c = 0; c < io->mNumberBuffers; c++) memcpy(io->mBuffers[c].mData, in, n * 4);
  return 0;
}
static AudioUnit u;
static double when;
static void run(double seconds) {
  static float L[N], R[N];
  for (int k = 0; k < seconds * 187.5; k++) {
    struct { UInt32 n; AudioBuffer b[2]; } bl = {2, {{1, sizeof L, L}, {1, sizeof R, R}}};
    AudioTimeStamp ts = {0};
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    ts.mSampleTime = when;
    when += N;
    AudioUnitRenderActionFlags fl = 0;
    AudioUnitRender(u, &fl, &ts, 0, N, (AudioBufferList *)&bl);
    usleep(5333);
  }
}
int main(int argc, char **argv) { @autoreleasepool {
  AudioComponentDescription d = {'aumf', '6HPA', 'LSIX', 0, 0};
  AudioComponent c = AudioComponentFindNext(NULL, &d);
  if (!c || AudioComponentInstanceNew(c, &u)) return 1;
  AudioStreamBasicDescription f = {48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
  AudioUnitSetProperty(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
  AudioUnitSetProperty(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  UInt32 max = N;
  AudioUnitSetProperty(u, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max, 4);
  AURenderCallbackStruct r = {cb, NULL};
  AudioUnitSetProperty(u, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &r, sizeof r);
  if (AudioUnitInitialize(u)) return 2;
  run(1);
  NSData *last = nil;
  int same = 0;
  for (int i = 3; i < argc; i++) {
    MusicDeviceMIDIEvent(u, 0xB0, 32, atoi(argv[1]), 0);
    MusicDeviceMIDIEvent(u, 0xC0, i - 3, 0, 0);
    run(1.0);
    CFPropertyListRef p = NULL;
    UInt32 sz = sizeof p;
    if (AudioUnitGetProperty(u, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &p, &sz)) return 3;
    NSData *x = [NSPropertyListSerialization dataWithPropertyList:(__bridge id)p format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
    [x writeToFile:[NSString stringWithFormat:@"%s/%s", argv[2], argv[i]] atomically:YES];
    if ([x isEqual:last]) same++;
    last = x;
    printf("%s %lu\n", argv[i], (unsigned long)x.length);
    fflush(stdout);
  }
  printf("identical to the one before: %d\n", same);
  _exit(0);
}}
