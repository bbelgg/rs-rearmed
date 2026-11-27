// The quit's teardown of the game's two JSON tables (not in rsmacfixes; RS Re-ARMed's own, always on). The Mac game
// interns every JSON key and string in one global table (json_key_table) and every number in another
// (json_number_table), each a hash table with a count and a recursive mutex; a JSON document holds a reference on
// each entry it uses, and the last reference's release removes the entry. At the quit MainService's destructor
// (0x1002a98f0) tail-calls json_key_table_free (0x1000f51f0), which calls num_table_free (0x1000f8c30) and then
// frees the key table the same way: the nodes, the buckets, the table and its mutex. Each first checks that its
// table is empty, and if it is not it writes to address 0 (0x1000f8c4d, 0x1000f5212): the game's assert, which in
// the recompiled game is a trap (exit status -5, SIGTRAP, a crash report).
//
// The tables are not empty when the quit comes while documents are still loading: the profile's gear
// (urn:database:json-db:gear_*, the gear manifests: amps, cabinets, pedals, racks) is asked for by name when a
// profile is loaded, read on the asset loader thread and taken in by the ManifestManager on the game thread a few
// frames later. A quit in between (Command-Q at the main menu right after the profile, EXIT GAME just after the
// title) shuts the services down with those answers still owed, and their documents are never released: 124 keys
// and 4 numbers when only the racks were owed, 2559 keys and 26 numbers when all the gear was. The original
// Mac game has the same code and the same race; Learn & Play's game has no such check (its teardown, 0x13fff00,
// frees the table whatever is left).
//
// Here a table that is empty is freed as the game frees it. One that is not is kept, with its mutex: the
// documents that still hold entries are never touched again before the process ends (rsp_exit, right after the
// shutdown), and should one be released after all, its entry is still in a table to be removed from. A table that
// is already gone (no load ever made it) is nothing to free, where the game would assert.
#include <stdint.h>

// The table at *slot (+0x08 buckets begin, +0x10 buckets end, +0x20 count; a node's first word is the next node)
// and its mutex at *lock_slot, freed with del (operator delete) and mutex_destroy, or kept. Returns the entries
// left: 0 when it was freed (or there was none).
uint64_t rs_quit_table_free(uint64_t *slot, uint64_t *lock_slot, void (*del)(void *), int (*mutex_destroy)(void *)) {
  uint64_t *t = (uint64_t *)(uintptr_t)*slot;
  if (t && t[4]) return t[4];
  if (t) {
    uint64_t *bk = (uint64_t *)(uintptr_t)t[1];
    uint64_t nb = (t[2] - t[1]) / 8;
    for (uint64_t i = 0; i < nb; i++) {
      for (uint64_t *n = (uint64_t *)(uintptr_t)bk[i]; n;) {
        uint64_t *next = (uint64_t *)(uintptr_t)n[0];
        del(n);
        n = next;
      }
      bk[i] = 0;
    }
    t[4] = 0;
    if (bk) del(bk);
    del(t);
  }
  *slot = 0;
  void *m = (void *)(uintptr_t)*lock_slot;
  if (m) {
    mutex_destroy(m);
    del(m);
  }
  *lock_slot = 0;
  return 0;
}
