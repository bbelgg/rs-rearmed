// The rebuilt Lua (third_party/lua-5.1.4-lnum) behind the lua_api of lua_cases.h.
#ifndef RS_GAME_LUA_REWRITE_H
#define RS_GAME_LUA_REWRITE_H

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "lua_cases.h"

static const lua_api lua_rewrite_api = {
    .newstate = lua_newstate,
    .close = lua_close,
    .open = {luaopen_base, luaopen_package, luaopen_table, luaopen_os, luaopen_string,
             luaopen_math, luaopen_debug},
    .pushcclosure = lua_pushcclosure,
    .pushstring = lua_pushstring,
    .call = lua_call,
    .loadbuffer = luaL_loadbuffer,
    .pcall = lua_pcall,
    .gettop = lua_gettop,
    .settop = lua_settop,
    .insert = lua_insert,
    .getfield = lua_getfield,
    .pushlstring = lua_pushlstring,
    .tolstring = lua_tolstring,
};

#endif
