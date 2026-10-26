// AK::SoundEngine::GetIDFromString(const char*) — original at 0x100ab08b0
// (Wwise SDK region of the game binary). Wwise turns event, bank, bus and RTPC
// names into 32-bit IDs with this; the game calls it for every PostEvent by name.
//
// Behaviour: NULL gives 0. Otherwise the name is lowercased (ASCII A-Z only)
// and hashed with 32-bit FNV-1 (offset basis 0x811c9dc5, prime 0x01000193,
// multiply then xor each byte).
//
// The game copies the name into a fixed 0x110-byte stack buffer first, so
// names longer than 258 bytes overflow it. Wwise names are short; this
// version hashes the whole lowercased input and has no such limit.
#include "rs_game.h"

uint32_t rsg_ak_get_id_from_string(const char *in) {
  if (!in) return 0;
  uint32_t h = 0x811c9dc5u;
  for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
    unsigned char c = *p;
    if ((unsigned char)(c - 'A') <= 25) c += 0x20;
    h = (h * 0x01000193u) ^ c;
  }
  return h;
}
