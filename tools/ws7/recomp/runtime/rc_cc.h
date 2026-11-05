// Calling conventions (RC_CC_<name> in rc_target_names.h)
#ifndef RC_CC_H
#define RC_CC_H

#define RC_CC_SYSV 0
#define RC_CC_CDECL 1
#define RC_CC_STDCALL 2
#define RC_CC_THISCALL 3
#define RC_CC_FASTCALL 4
#define RC_CC_CUSTOM 5  // register arguments only that function uses (win-x86, LTCG): read by hand

#endif
