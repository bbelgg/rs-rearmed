// The Mac's audio hardware API on the iOS audio session (CoreAudio/CoreAudio.h beside this file says what and why).
#import <AVFAudio/AVFAudio.h>

#include <os/lock.h>
#include <stdatomic.h>
#include <string.h>

#include "CoreAudio/CoreAudio.h"

enum { DEV_IN = 0x100, DEV_OUT = 0x101, STREAM_IN = 0x200, STREAM_OUT = 0x201 };
#define RATE 48000.0
#define IO_FRAMES 256.0  // the Mac's default device buffer (rs_audio_host.h: RS_AUDIO_DEFAULT_FRAMES)

static void say(NSString *s) { fprintf(stderr, "[rs-audio] ios: %s\n", s.UTF8String); }

static NSString *ports(NSArray<AVAudioSessionPortDescription *> *a) {
  NSMutableArray *o = [NSMutableArray array];
  for (AVAudioSessionPortDescription *p in a)
    [o addObject:[NSString stringWithFormat:@"\"%@\" (%@, %lu ch, %@)", p.portName, p.portType,
                                            (unsigned long)p.channels.count, p.UID]];
  return o.count ? [o componentsJoinedByString:@", "] : @"none";
}

// An interface with more than one input gives all of its channels (the game's list has one entry per channel) only
// when asked, and again for each new route: a USB interface plugged in while the game runs.
static void all_channels(NSString *why) {
  AVAudioSession *s = [AVAudioSession sharedInstance];
  NSInteger most = s.maximumInputNumberOfChannels;
  NSError *e = nil;
  if (most > 1 && s.inputNumberOfChannels < most && ![s setPreferredInputNumberOfChannels:most error:&e])
    say([NSString stringWithFormat:@"%ld input channels: %@", (long)most, e]);
  say([NSString stringWithFormat:@"%@: %.0f Hz, I/O %.0f frames, latency in %.1f ms, out %.1f ms; input %@ with "
                                 @"%ld of %ld ch; output %@ with %ld ch",
                                 why, s.sampleRate, s.IOBufferDuration * s.sampleRate, s.inputLatency * 1000,
                                 s.outputLatency * 1000, ports(s.currentRoute.inputs), (long)s.inputNumberOfChannels,
                                 (long)most, ports(s.currentRoute.outputs), (long)s.outputNumberOfChannels]);
  say([NSString stringWithFormat:@"inputs offered: %@", ports(s.availableInputs)]);
}

// The default devices change with the route: a cable, an interface or headphones plugged in or out. Every change is
// logged; the game's listeners hear of those that change a device.
#define MAX_LISTENERS 8
static struct {
  AudioObjectPropertySelector sel;
  AudioObjectPropertyListenerProc fn;
  void *arg;
} g_l[MAX_LISTENERS];
static _Atomic int g_nl;

// The route's ports, to tell a change of device from the many changes that leave them (a category, a configuration).
static NSString *route_key(void) {
  AVAudioSessionRouteDescription *r = [AVAudioSession sharedInstance].currentRoute;
  return [NSString stringWithFormat:@"%@|%@", r.inputs.firstObject.UID, r.outputs.firstObject.UID];
}
static NSString *g_route;
static os_unfair_lock g_route_lock = OS_UNFAIR_LOCK_INIT;

static void route_changed(NSNotification *n) {
  NSUInteger why = [n.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
  all_channels([NSString stringWithFormat:@"route changed (reason %lu)", (unsigned long)why]);
  NSString *key = route_key();
  os_unfair_lock_lock(&g_route_lock);
  BOOL same = [key isEqualToString:g_route];
  g_route = key;
  os_unfair_lock_unlock(&g_route_lock);
  if (same) return;
  int k = atomic_load(&g_nl);
  for (int i = 0; i < k; i++) {
    AudioObjectPropertyAddress pa = {g_l[i].sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    g_l[i].fn(kAudioObjectSystemObject, 1, &pa, g_l[i].arg);
  }
}

void rs_hal_ios_session(void) {
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    AVAudioSession *s = [AVAudioSession sharedInstance];
    NSError *e = nil;
    // Play and record: the guitar in and the game out at once. The speaker rather than the receiver when nothing is
    // plugged in; Bluetooth headphones for output (A2DP: their latency is theirs).
    AVAudioSessionCategoryOptions opt = AVAudioSessionCategoryOptionDefaultToSpeaker |
                                        AVAudioSessionCategoryOptionAllowBluetoothA2DP |
                                        AVAudioSessionCategoryOptionMixWithOthers;
    if (![s setCategory:AVAudioSessionCategoryPlayAndRecord mode:AVAudioSessionModeDefault options:opt error:&e])
      say([NSString stringWithFormat:@"category: %@", e]);
    if (![s setPreferredSampleRate:RATE error:&e]) say([NSString stringWithFormat:@"rate: %@", e]);
    if (![s setPreferredIOBufferDuration:IO_FRAMES / RATE error:&e]) say([NSString stringWithFormat:@"buffer: %@", e]);
    [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification
                                                      object:nil
                                                       queue:nil
                                                  usingBlock:^(NSNotification *n) { route_changed(n); }];
    if (![s setActive:YES error:&e]) say([NSString stringWithFormat:@"activate: %@", e]);
    all_channels(@"session");
    g_route = route_key();
  });
}

static AVAudioSessionPortDescription *port(AudioObjectID dev) {
  AVAudioSessionRouteDescription *r = [AVAudioSession sharedInstance].currentRoute;
  return dev == DEV_IN ? r.inputs.firstObject : dev == DEV_OUT ? r.outputs.firstObject : nil;
}

// A USB interface that is both the input and the output (an HX Stomp; not the Real Tone Cable, which has no output)
// is one device, as on the Mac: the game's list then takes it for an interface, with an entry per channel, and its
// I/O buffer is the guitar input's (rs_audio_set_device_buffer), as the session's buffer is one for both directions.
static int one_device(void) {
  AVAudioSessionRouteDescription *r = [AVAudioSession sharedInstance].currentRoute;
  AVAudioSessionPortDescription *i = r.inputs.firstObject, *o = r.outputs.firstObject;
  return i && o && [i.portType isEqualToString:AVAudioSessionPortUSBAudio] &&
         [o.portType isEqualToString:AVAudioSessionPortUSBAudio] && [i.portName isEqualToString:o.portName];
}

static int has_in(AudioObjectID dev) { return dev == DEV_IN; }
static int has_out(AudioObjectID dev) { return dev == DEV_OUT || (dev == DEV_IN && one_device()); }

// The channels in a scope (global: either).
static int channels(AudioObjectID dev, AudioObjectPropertyScope sc) {
  AVAudioSession *s = [AVAudioSession sharedInstance];
  int in = has_in(dev) && s.isInputAvailable && port(DEV_IN) ? (int)s.inputNumberOfChannels : 0;
  int out = has_out(dev) ? (int)s.outputNumberOfChannels : 0;
  return sc == kAudioObjectPropertyScopeInput ? in : sc == kAudioObjectPropertyScopeOutput ? out : in + out;
}

static int in_scope(AudioObjectID dev, AudioObjectPropertyScope sc) {
  if (sc == kAudioObjectPropertyScopeInput) return has_in(dev);
  if (sc == kAudioObjectPropertyScopeOutput) return has_out(dev);
  return 1;
}

static UInt32 transport(AudioObjectID dev) {
  NSString *t = port(dev).portType;
  if ([t isEqualToString:AVAudioSessionPortUSBAudio]) return kAudioDeviceTransportTypeUSB;
  if ([t isEqualToString:AVAudioSessionPortBuiltInMic] || [t isEqualToString:AVAudioSessionPortBuiltInSpeaker] ||
      [t isEqualToString:AVAudioSessionPortBuiltInReceiver])
    return kAudioDeviceTransportTypeBuiltIn;
  if ([t isEqualToString:AVAudioSessionPortBluetoothA2DP] || [t isEqualToString:AVAudioSessionPortBluetoothHFP] ||
      [t isEqualToString:AVAudioSessionPortBluetoothLE])
    return kAudioDeviceTransportTypeBluetooth;
  if ([t isEqualToString:AVAudioSessionPortHDMI]) return kAudioDeviceTransportTypeHDMI;
  if ([t isEqualToString:AVAudioSessionPortAirPlay]) return kAudioDeviceTransportTypeAirPlay;
  return kAudioDeviceTransportTypeUnknown;
}

#define PUT(T, v)                     \
  do {                                \
    if (*size < sizeof(T)) return -1; \
    *(T *)out = (v);                  \
    *size = sizeof(T);                \
    return noErr;                     \
  } while (0)

static OSStatus get(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 *size, void *out) {
  rs_hal_ios_session();
  AVAudioSession *s = [AVAudioSession sharedInstance];
  AudioObjectPropertySelector sel = a->mSelector;
  if (obj == kAudioObjectSystemObject) {
    if (sel == kAudioHardwarePropertyDevices) {
      AudioObjectID devs[2];
      UInt32 n = 0;
      if (channels(DEV_IN, kAudioObjectPropertyScopeInput) > 0) devs[n++] = DEV_IN;
      if (!one_device()) devs[n++] = DEV_OUT;
      if (*size < n * sizeof *devs) n = *size / sizeof *devs;
      memcpy(out, devs, n * sizeof *devs);
      *size = n * sizeof *devs;
      return noErr;
    }
    if (sel == kAudioHardwarePropertyDefaultInputDevice)
      PUT(AudioObjectID, channels(DEV_IN, kAudioObjectPropertyScopeInput) > 0 ? DEV_IN : kAudioObjectUnknown);
    if (sel == kAudioHardwarePropertyDefaultOutputDevice) PUT(AudioObjectID, one_device() ? DEV_IN : DEV_OUT);
    return kAudioHardwareUnknownPropertyError;
  }
  if (obj == STREAM_IN || obj == STREAM_OUT) {
    if (sel == kAudioStreamPropertyLatency) PUT(UInt32, 0);
    return kAudioHardwareUnknownPropertyError;
  }
  if (obj != DEV_IN && obj != DEV_OUT) return kAudioHardwareBadObjectError;
  double rate = s.sampleRate > 0 ? s.sampleRate : RATE;
  switch (sel) {
    case kAudioObjectPropertyName: {
      NSString *n = port(obj).portName ?: (obj == DEV_IN ? @"Input" : @"Output");
      PUT(CFStringRef, CFBridgingRetain(n));
    }
    case kAudioDevicePropertyDeviceUID: {
      NSString *u = port(obj).UID ?: (obj == DEV_IN ? @"ios-input" : @"ios-output");
      PUT(CFStringRef, CFBridgingRetain([u stringByAppendingString:obj == DEV_IN ? @"-in" : @"-out"]));
    }
    case kAudioDevicePropertyModelUID: return kAudioHardwareUnknownPropertyError;  // the cable is known by its name
    case kAudioDevicePropertyTransportType: PUT(UInt32, transport(obj));
    case kAudioDevicePropertyNominalSampleRate: PUT(Float64, rate);
    case kAudioDevicePropertyBufferFrameSize: PUT(UInt32, (UInt32)lround(s.IOBufferDuration * rate));
    case kAudioDevicePropertyBufferFrameSizeRange: {
      AudioValueRange r = {16, 4096};
      PUT(AudioValueRange, r);
    }
    case kAudioDevicePropertySafetyOffset: PUT(UInt32, 0);
    case kAudioDevicePropertyLatency:
      if (!in_scope(obj, a->mScope) || a->mScope == kAudioObjectPropertyScopeGlobal) PUT(UInt32, 0);
      PUT(UInt32,
          (UInt32)lround((a->mScope == kAudioObjectPropertyScopeInput ? s.inputLatency : s.outputLatency) * rate));
    case kAudioDevicePropertyStreams:
      if (!in_scope(obj, a->mScope) || a->mScope == kAudioObjectPropertyScopeGlobal || !channels(obj, a->mScope)) {
        *size = 0;
        return noErr;
      }
      PUT(AudioStreamID, a->mScope == kAudioObjectPropertyScopeInput ? STREAM_IN : STREAM_OUT);
    case kAudioDevicePropertyStreamConfiguration: {
      int ch = in_scope(obj, a->mScope) && a->mScope != kAudioObjectPropertyScopeGlobal ? channels(obj, a->mScope) : 0;
      if (*size < sizeof(AudioBufferList)) return -1;
      AudioBufferList *l = out;
      l->mNumberBuffers = ch ? 1 : 0;
      l->mBuffers[0].mNumberChannels = (UInt32)ch;
      l->mBuffers[0].mDataByteSize = 0;
      l->mBuffers[0].mData = NULL;
      *size = sizeof(AudioBufferList);
      return noErr;
    }
  }
  return kAudioHardwareUnknownPropertyError;
}

OSStatus AudioObjectGetPropertyData(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 qsize,
                                    const void *q, UInt32 *size, void *out) {
  (void)qsize;
  (void)q;
  @autoreleasepool {
    return get(obj, a, size, out);
  }
}

OSStatus AudioObjectGetPropertyDataSize(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 qsize,
                                        const void *q, UInt32 *size) {
  uint8_t buf[256];
  UInt32 n = sizeof buf;
  OSStatus st = AudioObjectGetPropertyData(obj, a, qsize, q, &n, buf);
  if (st == noErr && n == sizeof(CFStringRef) &&
      (a->mSelector == kAudioObjectPropertyName || a->mSelector == kAudioDevicePropertyDeviceUID))
    CFRelease(*(CFStringRef *)buf);
  *size = st == noErr ? n : 0;
  return st;
}

OSStatus AudioObjectSetPropertyData(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 qsize,
                                    const void *q, UInt32 size, const void *in) {
  (void)qsize;
  (void)q;
  rs_hal_ios_session();
  if (obj != DEV_IN && obj != DEV_OUT) return kAudioHardwareBadObjectError;
  @autoreleasepool {
    AVAudioSession *s = [AVAudioSession sharedInstance];
    NSError *e = nil;
    // the session's preferences: the system takes them as a wish, the property then reads what it gave
    if (a->mSelector == kAudioDevicePropertyNominalSampleRate && size == sizeof(Float64))
      return [s setPreferredSampleRate:*(const Float64 *)in error:&e] ? noErr : kAudioHardwareUnspecifiedError;
    if (a->mSelector == kAudioDevicePropertyBufferFrameSize && size == sizeof(UInt32)) {
      double rate = s.sampleRate > 0 ? s.sampleRate : RATE;
      return [s setPreferredIOBufferDuration:*(const UInt32 *)in / rate error:&e] ? noErr
                                                                                  : kAudioHardwareUnspecifiedError;
    }
  }
  return kAudioHardwareUnknownPropertyError;
}

OSStatus AudioObjectAddPropertyListener(AudioObjectID obj, const AudioObjectPropertyAddress *a,
                                        AudioObjectPropertyListenerProc fn, void *arg) {
  if (obj != kAudioObjectSystemObject) return kAudioHardwareUnknownPropertyError;
  rs_hal_ios_session();
  int k = atomic_load(&g_nl);
  if (k >= MAX_LISTENERS) return kAudioHardwareUnspecifiedError;
  g_l[k].sel = a->mSelector;
  g_l[k].fn = fn;
  g_l[k].arg = arg;
  atomic_store(&g_nl, k + 1);
  return noErr;
}
