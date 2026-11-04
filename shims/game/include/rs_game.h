// Arm64 rewrites of Rocksmith 2014 game-binary functions.
//
// Each function documents the original's address in the decrypted x86_64
// binary (analysis/cg/Rocksmith2014_x64_plain). Every rewrite has a
// differential test that runs the original under Rosetta via
// harness/rsimage.c and compares outputs. The RS_ORIG_* defines name the
// originals for those tests: RC_ADDR(name) is the address in the target's
// tables (tools/targets), which rs_names.h brings in.
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
#define RS_ORIG_ZLIB_ADLER32 RC_ADDR(adler32)
#define RS_ORIG_ZLIB_CRC32 RC_ADDR(crc32)
#define RS_ORIG_ZLIB_INFLATE_RESET RC_ADDR(inflateReset)
#define RS_ORIG_ZLIB_INFLATE_INIT RC_ADDR(inflateInit_) /* inflateInit_(strm, version, size) */
#define RS_ORIG_ZLIB_INFLATE RC_ADDR(inflate)
#define RS_ORIG_ZLIB_INFLATE_END RC_ADDR(inflateEnd)
#define RS_ORIG_ZLIB_UNCOMPRESS RC_ADDR(uncompress)

#define RS_ORIG_AK_GET_ID_FROM_STRING RC_ADDR(rsg_ak_get_id_from_string)

// Statically linked Lua 5.1.4 + LNUM patch (LNUM_DOUBLE LNUM_INT64, the release
// before 2009-04-17), replaced by third_party/lua-5.1.4-lnum. The code is laid out by source file
// in alphabetical order (lapi.c at 0x100e0faf0 ... lzio.c), dead-stripped.
#define RS_ORIG_LUA_GETTOP RC_ADDR(lua_gettop)
#define RS_ORIG_LUA_SETTOP RC_ADDR(lua_settop)
#define RS_ORIG_LUA_INSERT RC_ADDR(lua_insert)
#define RS_ORIG_LUA_TYPE RC_ADDR(lua_type)
#define RS_ORIG_LUA_TONUMBER RC_ADDR(lua_tonumber)
#define RS_ORIG_LUA_TOLSTRING RC_ADDR(lua_tolstring)
#define RS_ORIG_LUA_PUSHLSTRING RC_ADDR(lua_pushlstring)
#define RS_ORIG_LUA_PUSHSTRING RC_ADDR(lua_pushstring)
#define RS_ORIG_LUA_PUSHCCLOSURE RC_ADDR(lua_pushcclosure)
#define RS_ORIG_LUA_GETFIELD RC_ADDR(lua_getfield)
#define RS_ORIG_LUA_CALL RC_ADDR(lua_call)
#define RS_ORIG_LUA_PCALL RC_ADDR(lua_pcall)
#define RS_ORIG_LUA_LOAD RC_ADDR(lua_load)
#define RS_ORIG_LUAL_LOADBUFFER RC_ADDR(luaL_loadbuffer)
#define RS_ORIG_LUAOPEN_BASE RC_ADDR(luaopen_base)
#define RS_ORIG_LUAOPEN_DEBUG RC_ADDR(luaopen_debug)
#define RS_ORIG_LUAOPEN_MATH RC_ADDR(luaopen_math)
#define RS_ORIG_LUAOPEN_PACKAGE RC_ADDR(luaopen_package)
#define RS_ORIG_LUAOPEN_OS RC_ADDR(luaopen_os)
#define RS_ORIG_LUA_NEWSTATE RC_ADDR(lua_newstate) /* luaL_newstate is dead-stripped */
#define RS_ORIG_LUA_CLOSE RC_ADDR(lua_close)
#define RS_ORIG_LUAOPEN_STRING RC_ADDR(luaopen_string)
#define RS_ORIG_LUAOPEN_TABLE RC_ADDR(luaopen_table)
// Gamebryo-modified, not tested: luaL_loadfile (0x100e146d0) opens files through efd, luaopen_io
// (0x100e1eec0) wraps FILE* in efd::File.

// Statically linked libpng 1.5.13 (png.c ... pngtrans.c from 0x100b58c70),
// replaced by third_party/libpng-1.5.13. These are the calls Scaleform's PNG
// loader (0x100d04790, 0x100d04e50, 0x100d05020) makes.
#define RS_ORIG_PNG_SET_SIG_BYTES RC_ADDR(png_set_sig_bytes)
#define RS_ORIG_PNG_SIG_CMP RC_ADDR(png_sig_cmp)
#define RS_ORIG_PNG_CREATE_INFO_STRUCT RC_ADDR(png_create_info_struct)
#define RS_ORIG_PNG_GET_VALID RC_ADDR(png_get_valid)
#define RS_ORIG_PNG_GET_ROWBYTES RC_ADDR(png_get_rowbytes)
#define RS_ORIG_PNG_GET_GAMA RC_ADDR(png_get_gAMA)
#define RS_ORIG_PNG_GET_IHDR RC_ADDR(png_get_IHDR)
#define RS_ORIG_PNG_CREATE_READ_STRUCT RC_ADDR(png_create_read_struct)
#define RS_ORIG_PNG_DESTROY_READ_STRUCT RC_ADDR(png_destroy_read_struct)
#define RS_ORIG_PNG_READ_END RC_ADDR(png_read_end)
#define RS_ORIG_PNG_READ_ROW RC_ADDR(png_read_row)
#define RS_ORIG_PNG_READ_IMAGE RC_ADDR(png_read_image)
#define RS_ORIG_PNG_READ_UPDATE_INFO RC_ADDR(png_read_update_info)
#define RS_ORIG_PNG_READ_INFO RC_ADDR(png_read_info)
#define RS_ORIG_PNG_SET_READ_FN RC_ADDR(png_set_read_fn)
#define RS_ORIG_PNG_SET_STRIP_16 RC_ADDR(png_set_strip_16)
#define RS_ORIG_PNG_SET_PALETTE_TO_RGB RC_ADDR(png_set_palette_to_rgb)
#define RS_ORIG_PNG_SET_EXPAND_GRAY_1_2_4_TO_8 RC_ADDR(png_set_expand_gray_1_2_4_to_8)
#define RS_ORIG_PNG_SET_TRNS_TO_ALPHA RC_ADDR(png_set_tRNS_to_alpha)
#define RS_ORIG_PNG_SET_GRAY_TO_RGB RC_ADDR(png_set_gray_to_rgb)
#define RS_ORIG_PNG_SET_GAMMA RC_ADDR(png_set_gamma)

// Statically linked libjpeg 8d (IJG, 15-Jan-2012), replaced by
// third_party/jpeg-8d. Only the decoder is linked; Scaleform's loader
// (0x100d02820-0x100d04180) is the caller.
#define RS_ORIG_JPEG_CREATE_DECOMPRESS RC_ADDR(jpeg_CreateDecompress) /* jpeg_CreateDecompress(cinfo, 80, 0x290) */
#define RS_ORIG_JPEG_FINISH_DECOMPRESS RC_ADDR(jpeg_finish_decompress)
#define RS_ORIG_JPEG_READ_HEADER RC_ADDR(jpeg_read_header)
#define RS_ORIG_JPEG_ABORT_DECOMPRESS RC_ADDR(jpeg_abort_decompress)
#define RS_ORIG_JPEG_DESTROY_DECOMPRESS RC_ADDR(jpeg_destroy_decompress)
#define RS_ORIG_JPEG_START_DECOMPRESS RC_ADDR(jpeg_start_decompress)
#define RS_ORIG_JPEG_READ_SCANLINES RC_ADDR(jpeg_read_scanlines)
#define RS_ORIG_JPEG_READ_COEFFICIENTS RC_ADDR(jpeg_read_coefficients)
#define RS_ORIG_JPEG_RESYNC_TO_RESTART RC_ADDR(jpeg_resync_to_restart)
#define RS_ORIG_JPEG_STD_ERROR RC_ADDR(jpeg_std_error)

// Gamebryo LightSpeed efd (Foundation), engine.efd 0x10127a980-0x101319bd0.
// Rewrites in include/rs_efd.h; harness/efd_env.h sets efd up in the image.
#define RS_ORIG_EFD_ID_SET_TYPE RC_ADDR(rsg_efd_id_set_type) /* UniversalID.cpp */
#define RS_ORIG_EFD_ID_SET_USAGE RC_ADDR(rsg_efd_id_set_usage)
#define RS_ORIG_EFD_ID_SET_SHARD RC_ADDR(rsg_efd_id_set_shard)
#define RS_ORIG_EFD_ID_SET_NET_ID RC_ADDR(rsg_efd_id_set_net_id)
#define RS_ORIG_EFD_ID_SET_BASE_ID RC_ADDR(rsg_efd_id_set_base_id)
#define RS_ORIG_EFD_CATEGORY_U0 RC_ADDR(rsg_efd_category_init_u0)
#define RS_ORIG_EFD_CATEGORY_SVC RC_ADDR(rsg_efd_category_init_svc)
#define RS_ORIG_EFD_CATEGORY_U2 RC_ADDR(rsg_efd_category_init_u2)
#define RS_ORIG_EFD_FS_INIT RC_ADDR(rsg_efd_fixedstring_init) /* FixedString */
#define RS_ORIG_EFD_FS_INIT_CSTR RC_ADDR(rsg_efd_fixedstring_init_cstr)
#define RS_ORIG_EFD_FS_INIT_COPY RC_ADDR(rsg_efd_fixedstring_init_copy)
#define RS_ORIG_EFD_FS_RELEASE RC_ADDR(rsg_efd_fixedstring_release)
#define RS_ORIG_EFD_FS_C_STR RC_ADDR(rsg_efd_fixedstring_c_str)
#define RS_ORIG_EFD_FS_ASSIGN RC_ADDR(rsg_efd_fixedstring_assign)
#define RS_ORIG_EFD_FS_EXISTS RC_ADDR(rsg_efd_fixedstring_exists)
#define RS_ORIG_EFD_FS_LENGTH RC_ADDR(rsg_efd_fixedstring_length)
#define RS_ORIG_EFD_FS_EQUALS RC_ADDR(rsg_efd_fixedstring_equals)
#define RS_ORIG_EFD_DS_INIT RC_ADDR(rsg_efd_datastream_init) /* DataStream.cpp */
#define RS_ORIG_EFD_DS_INIT_COPY RC_ADDR(rsg_efd_datastream_init_copy)
#define RS_ORIG_EFD_DS_DTOR RC_ADDR(rsg_efd_datastream_destroy)
#define RS_ORIG_EFD_DS_READ RC_ADDR(rsg_efd_datastream_read)
#define RS_ORIG_EFD_DS_WRITE RC_ADDR(rsg_efd_datastream_write)
#define RS_ORIG_EFD_DS_SEEK RC_ADDR(rsg_efd_datastream_seek)
#define RS_ORIG_EFD_DS_WRITE_STREAM RC_ADDR(rsg_efd_datastream_write_stream)
#define RS_ORIG_EFD_DS_READ_STREAM RC_ADDR(rsg_efd_datastream_read_stream)
#define RS_ORIG_EFD_DS_WRITE_ID RC_ADDR(rsg_efd_datastream_write_id)
#define RS_ORIG_EFD_DS_READ_ID RC_ADDR(rsg_efd_datastream_read_id)
#define RS_ORIG_EFD_UTF8_UNITS_AT RC_ADDR(rsg_efd_utf8_units_at) /* utf8string */
#define RS_ORIG_EFD_UTF8_UNITS RC_ADDR(rsg_efd_utf8_units)
#define RS_ORIG_EFD_UTF8_TAIL_MISSING RC_ADDR(rsg_efd_utf8_tail_missing)
#define RS_ORIG_EFD_UTF8_LENGTH RC_ADDR(rsg_efd_utf8string_length)
#define RS_ORIG_EFD_UTF8_TO_UPPER RC_ADDR(rsg_efd_utf8string_to_upper)
#define RS_ORIG_EFD_UTF8_TO_LOWER RC_ADDR(rsg_efd_utf8string_to_lower)
#define RS_ORIG_EFD_PATH_STANDARDIZE RC_ADDR(rsg_efd_path_standardize)
#define RS_ORIG_EFD_SNPRINTF RC_ADDR(efd_snprintf)
#define RS_ORIG_EFD_SQRT_TABLE_NEW RC_ADDR(efd_sqrt_table_new)
#define RS_ORIG_EFD_MSGHELPER_CTOR RC_ADDR(rsg_efd_msghelper_init) /* IMessageHelperSystemService.cpp */
#define RS_ORIG_EFD_MSGHELPER_ON_NET_ID RC_ADDR(rsg_efd_msghelper_on_net_id)
#define RS_ORIG_EFD_MSGHELPER_PREINIT RC_ADDR(rsg_efd_msghelper_on_preinit)
#define RS_ORIG_EFD_MSGHELPER_SHUTDOWN RC_ADDR(rsg_efd_msghelper_on_shutdown)
#define RS_ORIG_EFD_MSGHELPER_INIT RC_ADDR(rsg_efd_msghelper_on_init)
#define RS_ORIG_EFD_MSGHELPER_TICK RC_ADDR(rsg_efd_msghelper_on_tick)
#define RS_ORIG_EFD_MSGHELPER_NAME RC_ADDR(rsg_efd_msghelper_display_name)
#define RS_ORIG_EFD_GET_CLASS_ID RC_ADDR(efd_ibase_get_class_id) /* IBase slot 7, shared */
#define RS_ORIG_EFD_ALLOC RC_ADDR(efd_alloc) /* (size, hint) */
#define RS_ORIG_EFD_FREE RC_ADDR(efd_free) /* (ptr, size or -1) */

#endif
