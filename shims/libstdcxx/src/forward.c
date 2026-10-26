// libstdc++ forwarding shim.
//
// macOS still ships GNU libstdc++ 4.2.1 as /usr/lib/libstdc++.6.dylib in both
// the arm64e and x86_64 dyld shared caches (it is only gone from the SDK), so
// this dylib implements nothing: it re-exports the system library wholesale,
// and through it libc++abi. Point the game's LC_LOAD_DYLIB at it to get a place
// to override individual libstdc++ symbols: anything defined in this file wins
// over the re-exported definition.
//
// Its install name can't be /usr/lib/libstdc++.6.dylib: dyld matches loaded
// images by install name, so the re-exported system library would resolve back
// to this dylib and no symbol would be found.
#include <stdio.h>
#include <stdlib.h>

__attribute__((constructor)) static void rs_libstdcxx_init(void) {
    if (getenv("RS_SHIM_VERBOSE"))
        fprintf(stderr, "[rs-libstdcxx] forwarding to /usr/lib/libstdc++.6.dylib\n");
}
