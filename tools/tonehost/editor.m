// rs-tone-editor: opens an Audio Unit's own window, with no audio, and saves what is set there as a state for rs-tonehost.
//   rs-tone-editor TYPE SUBTYPE MANUFACTURER [STATE.plist]     e.g. aumf 6HPA LSIX clean.plist
// With a file, its state is loaded first if it exists, and it is written when the window closes.
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#define N 512
static AudioUnit u;
static NSString *file;
static double when;
static OSStatus silence(void *r, AudioUnitRenderActionFlags *f, const AudioTimeStamp *t, UInt32 b, UInt32 n, AudioBufferList *io) {
  for (UInt32 c = 0; c < io->mNumberBuffers; c++) memset(io->mBuffers[c].mData, 0, n * 4);
  return 0;
}
static void render(void) {
  static float L[N], R[N];
  struct { UInt32 n; AudioBuffer b[2]; } list = {2, {{1, sizeof L, L}, {1, sizeof R, R}}};
  AudioTimeStamp ts = {0};
  ts.mFlags = kAudioTimeStampSampleTimeValid;
  ts.mSampleTime = when;
  when += N;
  AudioUnitRenderActionFlags fl = 0;
  AudioUnitRender(u, &fl, &ts, 0, N, (AudioBufferList *)&list);
}
static void save(void) {
  if (!file) return;
  CFPropertyListRef p = NULL;
  UInt32 sz = sizeof p;
  if (AudioUnitGetProperty(u, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &p, &sz)) return;
  NSData *d = [NSPropertyListSerialization dataWithPropertyList:(__bridge id)p format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
  [d writeToFile:file atomically:YES];
  printf("saved %s\n", file.UTF8String);
}
@interface Closer : NSObject <NSWindowDelegate> @end
@implementation Closer
- (void)windowWillClose:(NSNotification *)n { save(); [NSApp terminate:nil]; }
@end
// Command-V works through an Edit menu, in the plug-in's fields and in the file dialogs it opens
static void edit_menu(void) {
  NSMenu *bar = [NSMenu new], *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
  [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
  [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
  [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
  [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
  [bar addItemWithTitle:@"" action:NULL keyEquivalent:@""].submenu = [NSMenu new];  // the first is the application's menu
  [bar addItemWithTitle:@"Edit" action:NULL keyEquivalent:@""].submenu = edit;
  NSApp.mainMenu = bar;
}
static uint32_t fourcc(const char *c) { return (uint32_t)c[0] << 24 | (uint32_t)c[1] << 16 | (uint32_t)c[2] << 8 | (uint32_t)c[3]; }
int main(int argc, char **argv) { @autoreleasepool {
  if (argc < 4) { fprintf(stderr, "usage: rs-tone-editor TYPE SUBTYPE MANUFACTURER [STATE.plist]\n"); return 2; }
  if (argc > 4) file = [NSString stringWithUTF8String:argv[4]];
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  edit_menu();
  AudioComponentDescription d = {fourcc(argv[1]), fourcc(argv[2]), fourcc(argv[3]), 0, 0};
  AudioComponent c = AudioComponentFindNext(NULL, &d);
  if (!c || AudioComponentInstanceNew(c, &u)) { fprintf(stderr, "no such Audio Unit\n"); return 1; }
  AudioStreamBasicDescription f = {48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
  AudioUnitSetProperty(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
  AudioUnitSetProperty(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  UInt32 mf = N;
  AudioUnitSetProperty(u, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mf, 4);
  AURenderCallbackStruct r = {silence, NULL};
  AudioUnitSetProperty(u, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &r, sizeof r);
  if (AudioUnitInitialize(u)) return 1;
  NSData *old = file ? [NSData dataWithContentsOfFile:file] : nil;
  if (old) {
    id pl = [NSPropertyListSerialization propertyListWithData:old options:0 format:NULL error:NULL];
    CFPropertyListRef p = (__bridge CFPropertyListRef)pl;
    AudioUnitSetProperty(u, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &p, sizeof p);
  }
  UInt32 sz = 0;
  Boolean w = 0;
  if (AudioUnitGetPropertyInfo(u, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &sz, &w)) { fprintf(stderr, "this Audio Unit has no window\n"); return 1; }
  AudioUnitCocoaViewInfo *vi = malloc(sz);
  AudioUnitGetProperty(u, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, vi, &sz);
  NSBundle *b = [NSBundle bundleWithURL:(__bridge NSURL *)vi->mCocoaAUViewBundleLocation];
  [b load];
  id<AUCocoaUIBase> factory = [[b classNamed:(__bridge NSString *)vi->mCocoaAUViewClass[0]] new];
  NSView *v = [factory uiViewForAudioUnit:u withSize:NSMakeSize(1100, 760)];
  NSWindow *win = [[NSWindow alloc] initWithContentRect:v.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
  Closer *closer = [Closer new];
  win.delegate = closer;
  win.title = @"RS Tone Host: plug-in";
  win.contentView = v;
  [win center];
  [win makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  [NSTimer scheduledTimerWithTimeInterval:0.02 repeats:YES block:^(NSTimer *t) { render(); render(); }];
  [NSApp run];
} return 0; }
