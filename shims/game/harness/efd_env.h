// Gamebryo efd (Foundation) inside the mapped original image, for
// differential tests of stateful efd code. x86_64 only, after rsimage_load().
//
// efd does not initialize itself from static initializers alone. Its 83
// __mod_init_func entries (range RS_RANGE_ENGINE_EFD) build static data: the
// class-info tables behind QueryInterface, static Category constants,
// mutexes and the factory registry. The rest is done by efd::Init (0x1012e2790),
// which the game reaches through its StaticDataManager (0x101300ef0, from
// GameBootstrap via 0x100e340e0):
//   0x1012ef970  the global allocator (efd::MemManager): the game's own
//                CreateGlobalMemoryAllocator (0x100178b10) makes a 0x1c08-byte
//                StandardAllocator with 47 fixed-size pools (also created
//                lazily on the first allocation, 0x1012ef880)
//   0x1012e3ce0  GlobalStringTable (FixedString interning), at 0x101939c70
//   0x10130aec0  CPU description (sysctl hw.*), 0x10193e278
//   0x10130ac70  fast-sqrt mantissa table (0x1012f8d60), 0x10193e6f8
//   0x101308e60  timer base (mach_absolute_time)
//   0x10129fbf0  AssetWeb URI constants
//   0x1012e45c0  the assert handler
// The Logger singleton (0x10193a8b0) is created lazily on the first log call
// with a default sink; efd_env_set_logger() replaces it.
#ifndef EFD_ENV_H
#define EFD_ENV_H

#define RS_ORIG_EFD_INIT 0x1012e2790
#define RS_ORIG_EFD_SHUTDOWN 0x1012e27f0
#define RS_ORIG_EFD_LOGGER_SINGLETON 0x10193a8b0 /* -> {?, ILogger *} */
#define RS_ORIG_EFD_STRING_TABLE 0x101939c70     /* GlobalStringTable * */
#define RS_ORIG_EFD_STRING_COUNT 0x101939c78
#define RS_ORIG_EFD_STRING_COLLISIONS 0x101939c80
#define RS_ORIG_EFD_SQRT_TABLE 0x10193e6f8 /* uint32_t * */

// Runs efd's static initializers and efd::Init. Returns the number of
// initializers run, or -1 if one trapped.
int efd_env_start(void);

// Makes the original efd::GetLogger() return a logger whose ILogger is
// `ilogger` (an object with the ILogger slot layout).
void efd_env_set_logger(void *ilogger);

#endif
