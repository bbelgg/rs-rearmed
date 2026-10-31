// Override module `audio`, the sink's ring (src/audio.h): at LatencyBuffer=1 only the output ring is small.
//
// LatencyBuffer (the audio engine setting in the game's Audio settings, 0x101873e0c) has one effect: the game
// copies it into AkPlatformInitSettings+0x54 (uNumRefillsInVoice, 0x101326ae9) before AK::SoundEngine::Init
// (0x100ab0a30), which keeps it for the whole engine (0x101907df4). The CoreAudio sink reads it once more
// when it is created (0x100aa6792 -> Init 0x100aa7be0: ring = refills x 256 frames per channel, kept at
// sink+0x94); the ring is the only part that adds output latency. In the engine the count is the refill
// buffers of each voice: 2 is double-buffered, 1 leaves each voice a single buffer. So at 1
// (RS_AUDIO_RING_SPLIT=0 turns this off) the engine runs with 2, which costs no latency, and only the sink's
// ring stays at 1 x 256 frames; the game's setting, its menu and Rocksmith.ini keep 1.
//   - a pre hook on the AK::SoundEngine::Init hook point raises the settings' 1 to 2;
//   - F_100aa7be0, the sink's Init, sizes the ring from the requested count instead;
//   - the output device's buffer is capped at half the ring, not half the engine's count (audio_out.c).
#include "audio.h"
#include "rc_hook.h"

#define PS_REFILLS RC_OFF(ak_ps_refills)  // AkPlatformInitSettings: u16 uNumRefillsInVoice
#define AK_SUCCESS 1u
#define AK_INSUFFICIENT_MEMORY 0x34u

static _Atomic uint32_t g_ring_refills;  // the ring's refill count when it differs from the engine's (0: same)

uint32_t rs_audio_ring_refills(void) {
  uint32_t r = atomic_load(&g_ring_refills);
  if (r) return r;
  r = RC_LD(uint16_t, RC_A(AK_PD_REFILLS));
  return r ? r : 4;  // what Wwise uses for 0 (0x100ad1f08)
}

static int before_ak_init(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  (void)OV_U64(a);
  uint8_t *ps = OV_PTR(a, uint8_t);
  if (!ps || *(uint16_t *)(ps + PS_REFILLS) != 1 || !rs_audio_env_on("RS_AUDIO_RING_SPLIT", 1)) return 0;
  atomic_store(&g_ring_refills, 1);
  *(uint16_t *)(ps + PS_REFILLS) = 2;
  rs_audio_log("LatencyBuffer 1: the engine runs with 2 refills, the output ring with 1 (256 frames)");
  return 0;
}

__attribute__((constructor)) static void ring_init(void) {
  if (rc_hook_add(RC_ADDR(ak_init), before_ak_init, NULL, NULL)) rs_audio_log("no AK::SoundEngine::Init hook point");
}

// Sink Init(this, const u32 *refills): the original stores *refills at +0x94, allocates the ring and
// returns AK_Success when it got one (+0x28), else AK_InsufficientMemory.
void RC_FN(ak_sink_ring_init)(rc_ctx *c) {
  OV_ARGS_OF(ak_sink_ring_init, a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  uint32_t refills = *OV_PTR(a, uint32_t);
  uint32_t ring = atomic_load(&g_ring_refills);
  if (!ring) ring = refills;
  *(uint32_t *)(self + RC_OFF(ak_sink_refills)) = ring;
  OV_GUEST(ak_sink_ring_alloc, (uint64_t)(uintptr_t)self, (uint64_t)ring << 8);
  ov_ret_u32(c, *(uint64_t *)(self + RC_OFF(ak_sink_ring)) ? AK_SUCCESS : AK_INSUFFICIENT_MEMORY);
}
