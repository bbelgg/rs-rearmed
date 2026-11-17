// FFT setups (RS_MODS_FIXES; not in rsmacfixes): the Mac game's note detection makes a vDSP FFT setup for each
// song it starts (vDSP_create_fftsetup, called at 0x10010b180 and 0x10010bb70) and destroys none of them: about
// 0.6 MB each and 1 MB a song, for the whole run, as the original does with the same calls. A setup is tables
// for one size, never written after it is made, and one serves every FFT of its size from any thread. So each
// size is made once, kept, and handed out again, and a destroy of a kept one does nothing.
//
// The game calls the system's functions through its import thunks, which ask here first (thunks/gen_thunks.py:
// HOOKS). Learn & Play's game has an FFT of its own and no such call.
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>

#include "mods_shared.h"

#define FFT_SIZES 32    // log2n: a setup of 2^log2n points
#define FFT_RADIXES 3   // kFFTRadix2, 3, 5

static struct {
  pthread_mutex_t mu;
  void *kept[FFT_RADIXES][FFT_SIZES];
} g_fft = {.mu = PTHREAD_MUTEX_INITIALIZER};

// The system's function: Accelerate is loaded with the game, whose own import it is
static void *fft_system(const char *name) {
  static void *lib;
  if (!lib) lib = dlopen("/System/Library/Frameworks/Accelerate.framework/Accelerate", RTLD_LAZY);
  return lib ? dlsym(lib, name) : NULL;
}

// The setup for 2^log2n points in *out, made the first time it is asked for. 0: the game's own call makes one
// (the fixes are off, a size or radix out of the table's, or the system gave none).
int rs_mods_fftsetup(uint64_t log2n, int32_t radix, void **out) {
  if (!rs_mods_cfg.fixes || log2n >= FFT_SIZES || radix < 0 || radix >= FFT_RADIXES) return 0;
  pthread_mutex_lock(&g_fft.mu);
  void *s = g_fft.kept[radix][log2n];
  if (!s) {
    static void *(*create)(unsigned long, int);
    if (!create) create = (void *(*)(unsigned long, int))fft_system("vDSP_create_fftsetup");
    if (create && (s = create((unsigned long)log2n, radix))) {
      g_fft.kept[radix][log2n] = s;
      rs_mods_logf("FFT setup of 2^%u points made, and kept for every song", (unsigned)log2n);
    }
  }
  pthread_mutex_unlock(&g_fft.mu);
  if (!s) return 0;
  *out = s;
  return 1;
}

// 1 when the setup is a kept one: the game's destroy of it does nothing
int rs_mods_fftsetup_kept(const void *setup) {
  int kept = 0;
  if (!setup) return 0;
  pthread_mutex_lock(&g_fft.mu);
  for (int r = 0; r < FFT_RADIXES && !kept; r++)
    for (int n = 0; n < FFT_SIZES && !kept; n++) kept = g_fft.kept[r][n] == setup;
  pthread_mutex_unlock(&g_fft.mu);
  return kept;
}
