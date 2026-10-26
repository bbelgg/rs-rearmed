// Arm64 rewrites of Rocksmith 2014 game-binary functions.
//
// Each function documents the original's address in the decrypted x86_64
// binary (analysis/cg/Rocksmith2014_x64_plain). Every rewrite has a
// differential test that runs the original under Rosetta via
// harness/rsimage.c and compares outputs.
#ifndef RS_GAME_H
#define RS_GAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// AK::SoundEngine::GetIDFromString (0x100ab08b0): lowercase + FNV-1 32.
uint32_t rsg_ak_get_id_from_string(const char *name);

#ifdef __cplusplus
}
#endif

// Statically linked zlib 1.2.3 in the original, replaced by the system libz
// (same API, verified by the differential test):
#define RS_ORIG_ZLIB_ADLER32 0x100e071d0
#define RS_ORIG_ZLIB_CRC32 0x100e076f0
#define RS_ORIG_ZLIB_INFLATE_FAST 0x100e0a280
#define RS_ORIG_ZLIB_INFLATE_RESET 0x100e0b070
#define RS_ORIG_ZLIB_INFLATE_INIT 0x100e0b120 /* inflateInit_(strm, version, size) */
#define RS_ORIG_ZLIB_INFLATE 0x100e0b260
#define RS_ORIG_ZLIB_INFLATE_END 0x100e0cf20
#define RS_ORIG_ZLIB_UNCOMPRESS 0x100e0fa10

#define RS_ORIG_AK_GET_ID_FROM_STRING 0x100ab08b0

// Statically linked Lua 5.1.4 + LNUM patch (LNUM_DOUBLE LNUM_INT64, the release
// before 2009-04-17), replaced by third_party/lua-5.1.4-lnum. The code is laid out by source file
// in alphabetical order (lapi.c at 0x100e0faf0 ... lzio.c), dead-stripped.
#define RS_ORIG_LUA_GETTOP 0x100e0fcf0
#define RS_ORIG_LUA_SETTOP 0x100e0fd10
#define RS_ORIG_LUA_INSERT 0x100e0ff00
#define RS_ORIG_LUA_TYPE 0x100e10260
#define RS_ORIG_LUA_TONUMBER 0x100e10ca0
#define RS_ORIG_LUA_TOLSTRING 0x100e10fb0
#define RS_ORIG_LUA_PUSHLSTRING 0x100e11810
#define RS_ORIG_LUA_PUSHSTRING 0x100e11870
#define RS_ORIG_LUA_PUSHCCLOSURE 0x100e11a10
#define RS_ORIG_LUA_GETFIELD 0x100e11c80
#define RS_ORIG_LUA_CALL 0x100e12970
#define RS_ORIG_LUA_PCALL 0x100e129c0
#define RS_ORIG_LUA_LOAD 0x100e12b20
#define RS_ORIG_LUAL_LOADFILE 0x100e146d0 /* Gamebryo-modified: opens files through efd */
#define RS_ORIG_LUAL_LOADBUFFER 0x100e14ab0
#define RS_ORIG_LUAOPEN_BASE 0x100e14b40
#define RS_ORIG_LUAOPEN_DEBUG 0x100e19100
#define RS_ORIG_LUAL_OPENLIBS 0x100e1ed30
#define RS_ORIG_LUAOPEN_IO 0x100e1eec0 /* Gamebryo-modified: FILE* wrapped in efd::File */
#define RS_ORIG_LUAOPEN_MATH 0x100e23040
#define RS_ORIG_LUAOPEN_PACKAGE 0x100e24090
#define RS_ORIG_LUAOPEN_OS 0x100e256b0
#define RS_ORIG_LUA_NEWSTATE 0x100e29dd0 /* luaL_newstate is dead-stripped */
#define RS_ORIG_LUA_CLOSE 0x100e2a1e0
#define RS_ORIG_LUAOPEN_STRING 0x100e2a5c0
#define RS_ORIG_LUAOPEN_TABLE 0x100e2e290

// Statically linked libpng 1.5.13 (png.c ... pngtrans.c from 0x100b58c70),
// replaced by third_party/libpng-1.5.13. These are the calls Scaleform's PNG
// loader (0x100d04790, 0x100d04e50, 0x100d05020) makes.
#define RS_ORIG_PNG_SET_SIG_BYTES 0x100b58c70
#define RS_ORIG_PNG_SIG_CMP 0x100b5aea0
#define RS_ORIG_PNG_CREATE_INFO_STRUCT 0x100b5b170
#define RS_ORIG_PNG_GET_VALID 0x100b5c4a0
#define RS_ORIG_PNG_GET_ROWBYTES 0x100b5c4c0
#define RS_ORIG_PNG_GET_GAMA 0x100b5c4e0
#define RS_ORIG_PNG_GET_IHDR 0x100b5c510
#define RS_ORIG_PNG_CREATE_READ_STRUCT 0x100b61590
#define RS_ORIG_PNG_DESTROY_READ_STRUCT 0x100b61780
#define RS_ORIG_PNG_READ_END 0x100b61880
#define RS_ORIG_PNG_READ_ROW 0x100b61c90
#define RS_ORIG_PNG_READ_IMAGE 0x100b62170
#define RS_ORIG_PNG_READ_UPDATE_INFO 0x100b62240
#define RS_ORIG_PNG_READ_INFO 0x100b62270
#define RS_ORIG_PNG_CREATE_READ_STRUCT_2 0x100b626d0
#define RS_ORIG_PNG_SET_READ_FN 0x100b62970
#define RS_ORIG_PNG_SET_STRIP_16 0x100b62a30
#define RS_ORIG_PNG_SET_PALETTE_TO_RGB 0x100b62a50
#define RS_ORIG_PNG_SET_EXPAND_GRAY_1_2_4_TO_8 0x100b62a70
#define RS_ORIG_PNG_SET_TRNS_TO_ALPHA 0x100b62a90
#define RS_ORIG_PNG_SET_GRAY_TO_RGB 0x100b62ab0
#define RS_ORIG_PNG_SET_GAMMA 0x100b66cb0

// Statically linked libjpeg 8d (IJG, 15-Jan-2012), replaced by
// third_party/jpeg-8d. Only the decoder is linked; Scaleform's loader
// (0x100d02820-0x100d04180) is the caller.
#define RS_ORIG_JPEG_CREATE_DECOMPRESS 0x100b40bf0 /* jpeg_CreateDecompress(cinfo, 80, 0x290) */
#define RS_ORIG_JPEG_CONSUME_INPUT 0x100b40d50
#define RS_ORIG_JPEG_FINISH_DECOMPRESS 0x100b41030
#define RS_ORIG_JPEG_READ_HEADER 0x100b41100
#define RS_ORIG_JPEG_ABORT_DECOMPRESS 0x100b41180
#define RS_ORIG_JPEG_DESTROY_DECOMPRESS 0x100b41190
#define RS_ORIG_JPEG_START_DECOMPRESS 0x100b411a0
#define RS_ORIG_JPEG_READ_SCANLINES 0x100b413b0
#define RS_ORIG_JPEG_READ_COEFFICIENTS 0x100b42800
#define RS_ORIG_JPEG_RESYNC_TO_RESTART 0x100b44270
#define RS_ORIG_JPEG_STD_ERROR 0x100b57920
#define RS_ORIG_JINIT_MEMORY_MGR 0x100b57b50

// Gamebryo LightSpeed efd (Foundation), engine.efd 0x10127a980-0x101319bd0.
// Rewrites in include/rs_efd.h; harness/efd_env.h sets efd up in the image.
#define RS_ORIG_EFD_ID_SET_TYPE 0x101303210 /* UniversalID.cpp */
#define RS_ORIG_EFD_ID_SET_USAGE 0x101303330
#define RS_ORIG_EFD_ID_SET_SHARD 0x101303450
#define RS_ORIG_EFD_ID_SET_NET_ID 0x1013035a0
#define RS_ORIG_EFD_ID_SET_BASE_ID 0x101303610
#define RS_ORIG_EFD_CATEGORY_U0 0x1012f44b0
#define RS_ORIG_EFD_CATEGORY_SVC 0x1012f4520
#define RS_ORIG_EFD_CATEGORY_U2 0x1012f45a0
#define RS_ORIG_EFD_FS_INIT 0x1012e2320 /* FixedString */
#define RS_ORIG_EFD_FS_INIT_CSTR 0x1012e2340
#define RS_ORIG_EFD_FS_INIT_COPY 0x1012e23a0
#define RS_ORIG_EFD_FS_RELEASE 0x1012e23e0
#define RS_ORIG_EFD_FS_C_STR 0x1012e2480
#define RS_ORIG_EFD_FS_ASSIGN 0x1012e2490
#define RS_ORIG_EFD_FS_EXISTS 0x1012e2510
#define RS_ORIG_EFD_FS_LENGTH 0x1012e2530
#define RS_ORIG_EFD_FS_EQUALS 0x1012e2550
#define RS_ORIG_EFD_ST_CREATE 0x1012e3ce0 /* GlobalStringTable */
#define RS_ORIG_EFD_ST_DESTROY 0x1012e3d10
#define RS_ORIG_EFD_ST_CTOR 0x1012e3d90
#define RS_ORIG_EFD_ST_ADD 0x1012e3ef0
#define RS_ORIG_EFD_ST_FIND 0x1012e4090
#define RS_ORIG_EFD_ST_INSERT 0x1012e4220
#define RS_ORIG_EFD_ST_REMOVE 0x1012e4380
#define RS_ORIG_EFD_DS_INIT 0x1012c1fe0 /* DataStream.cpp */
#define RS_ORIG_EFD_DS_INIT_COPY 0x1012c2080
#define RS_ORIG_EFD_DS_DTOR 0x1012c2130
#define RS_ORIG_EFD_DS_DELETE 0x1012c2190
#define RS_ORIG_EFD_DS_READ 0x1012c2200
#define RS_ORIG_EFD_DS_WRITE 0x1012c2300
#define RS_ORIG_EFD_DS_SEEK 0x1012c2460
#define RS_ORIG_EFD_DS_WRITE_STREAM 0x1012c2550
#define RS_ORIG_EFD_DS_READ_STREAM 0x1012c26b0
#define RS_ORIG_EFD_DS_WRITE_ID 0x1013036c0
#define RS_ORIG_EFD_DS_READ_ID 0x101303770
#define RS_ORIG_EFD_UTF8_UNITS_AT 0x101303810 /* utf8string */
#define RS_ORIG_EFD_UTF8_UNITS 0x101303830
#define RS_ORIG_EFD_UTF8_TAIL_MISSING 0x101303850
#define RS_ORIG_EFD_UTF8_LENGTH 0x1013050e0
#define RS_ORIG_EFD_UTF8_TO_UPPER 0x101306460
#define RS_ORIG_EFD_UTF8_TO_LOWER 0x1013064d0
#define RS_ORIG_EFD_PATH_STANDARDIZE 0x1012f6d60
#define RS_ORIG_EFD_SNPRINTF 0x10130b200
#define RS_ORIG_EFD_SQRT_TABLE_NEW 0x1012f8d60
#define RS_ORIG_EFD_MSGHELPER_CTOR 0x1012e5470 /* IMessageHelperSystemService.cpp */
#define RS_ORIG_EFD_MSGHELPER_ON_NET_ID 0x1012e53a0
#define RS_ORIG_EFD_MSGHELPER_PREINIT 0x1012e54a0
#define RS_ORIG_EFD_MSGHELPER_SHUTDOWN 0x1012e5680
#define RS_ORIG_EFD_MSGHELPER_INIT 0x1012e5ba0
#define RS_ORIG_EFD_MSGHELPER_TICK 0x1012e5bb0
#define RS_ORIG_EFD_MSGHELPER_NAME 0x1012e5bd0
#define RS_ORIG_EFD_GET_CLASS_ID 0x10001cb60 /* IBase slot 7, shared */
#define RS_ORIG_EFD_SM_GET_SYSTEM_SERVICE 0x1012fd300 /* ServiceManager slot 15 */
#define RS_ORIG_EFD_ALLOC 0x1012efc90  /* (size, hint) */
#define RS_ORIG_EFD_FREE 0x1012f00f0   /* (ptr, size or -1) */

#endif
