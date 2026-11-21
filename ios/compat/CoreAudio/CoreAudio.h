// iOS: the part of the Mac's audio hardware API (CoreAudio/AudioHardware.h, macOS only) the audio code of both
// editions uses (shims/coreaudio, win-x86/shims/audio), on the iOS audio session (rs_hal_ios.m). Found before the
// SDK's own CoreAudio/CoreAudio.h by the iOS build's include path (ios/build_lp.sh), so that code stays one source.
//
// iOS has one input and one output at a time, the session's route: here two devices, the input (a Real Tone Cable,
// an interface or the built-in microphone) and the output (the speaker, headphones, an interface), with the Mac's
// properties answered from the route: name, UID, transport, channels, sample rate, I/O buffer and latency. The
// selectors keep the Mac's values. The default devices change with the route (AVAudioSessionRouteChangeNotification).
#ifndef RS_IOS_COREAUDIO_H
#define RS_IOS_COREAUDIO_H

#include <CoreAudio/CoreAudioTypes.h>
#include <CoreAudio/AudioHardwareBase.h>
#include <CoreFoundation/CoreFoundation.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef AudioObjectID AudioStreamID;

typedef OSStatus (*AudioObjectPropertyListenerProc)(AudioObjectID inObjectID, UInt32 inNumberAddresses,
                                                    const AudioObjectPropertyAddress *inAddresses, void *inClientData);

enum { kAudioObjectSystemObject = 1 };

enum {
  kAudioHardwarePropertyDevices = 'dev#',
  kAudioHardwarePropertyDefaultInputDevice = 'dIn ',
  kAudioHardwarePropertyDefaultOutputDevice = 'dOut',
  kAudioDevicePropertyBufferFrameSize = 'fsiz',
  kAudioDevicePropertyBufferFrameSizeRange = 'fsz#',
  kAudioDevicePropertyStreamConfiguration = 'slay',
  kAudioDevicePropertyIOThreadOSWorkgroup = 'oswg',
};

OSStatus AudioObjectGetPropertyDataSize(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 qsize,
                                        const void *q, UInt32 *size);
OSStatus AudioObjectGetPropertyData(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 qsize,
                                    const void *q, UInt32 *size, void *out);
OSStatus AudioObjectSetPropertyData(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 qsize,
                                    const void *q, UInt32 size, const void *in);
OSStatus AudioObjectAddPropertyListener(AudioObjectID obj, const AudioObjectPropertyAddress *a,
                                        AudioObjectPropertyListenerProc fn, void *arg);

// The session set up for the game (play and record, 48 kHz, a short I/O buffer), once; any of the calls above does
// it too. The app asks for the microphone before the game starts (ios/app).
void rs_hal_ios_session(void);

#ifdef __cplusplus
}
#endif
#endif
