// dyld interposition helper.
//
// RS_INTERPOSE(my_fopen, fopen) puts a {replacement, replacee} pair in the
// __DATA,__interpose section. When the dylib is loaded (DYLD_INSERT_LIBRARIES or
// a normal LC_LOAD_DYLIB), dyld rebinds every *other* image's references to
// `replacee` so they call `replacement`. References from inside this dylib are
// not rebound, so the replacement can call the original by its normal name.
#pragma once

#define RS_INTERPOSE(replacement, replacee)                                    \
    __attribute__((used)) static const struct {                               \
        const void *rs_replacement;                                           \
        const void *rs_replacee;                                              \
    } rs_interpose_##replacee                                                 \
        __attribute__((section("__DATA,__interpose"))) = {                    \
            (const void *)(unsigned long)&replacement,                        \
            (const void *)(unsigned long)&replacee,                           \
    }
