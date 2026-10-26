// Lua 5.1 differential corpus, shared by the x86_64 differential test (which
// runs it through the original game's Lua) and the arm64 native test (which
// runs it through third_party/lua-5.1.4-lnum and checks the golden results).
//
// The game links Lua 5.1.4 with the LNUM patch (LNUM_DOUBLE + LNUM_INT64:
// numbers are doubles or 64-bit integers), in the release before
// lua514-lnum-20090417; the rebuild uses 20090417. The one known difference:
// the game's luaH_getint checks the array part with a 32-bit compare, so a
// 64-bit key whose low 32 bits index the array part aliases that slot. Its io
// library, luaL_loadfile and the package searchers were rewired by Gamebryo
// onto efd's file system (luaopen_io even allocates efd::File objects for
// stdin/stdout/stderr), so the corpus stays away from files: the io library is
// not opened, and there is no dofile, loadfile, or require of anything not in
// package.preload.
//
// A case runs in a fresh state: lua_newstate, the libraries luaL_openlibs
// opens except io (in linit.c's order), the prelude below, then
// luaL_loadbuffer + lua_pcall of the case source. Its results go
// through the prelude's ser() into one string. Its bytecode (string.dump of
// the compiled chunk) is recorded too, so the parser and code generator are
// compared as well as the VM and the libraries.
#ifndef RS_GAME_LUA_CASES_H
#define RS_GAME_LUA_CASES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vectors.h"

// The part of the Lua C API the driver uses. Filled with the original image's
// functions in the differential test, with the rebuilt library otherwise.
typedef struct lua_State lua_State;
typedef void *(*lua_alloc_fn)(void *ud, void *ptr, size_t osize, size_t nsize);
typedef int (*lua_cfn)(lua_State *);
enum { LUA_OPEN_BASE, LUA_OPEN_PACKAGE, LUA_OPEN_TABLE, LUA_OPEN_OS, LUA_OPEN_STRING, LUA_OPEN_MATH,
       LUA_OPEN_DEBUG, LUA_NOPEN };
static const char *const lua_open_names[LUA_NOPEN] = {"", "package", "table", "os", "string", "math", "debug"};
typedef struct {
  lua_State *(*newstate)(lua_alloc_fn, void *);
  void (*close)(lua_State *);
  lua_cfn open[LUA_NOPEN];  // luaopen_base, luaopen_package, ...
  void (*pushcclosure)(lua_State *, lua_cfn, int);
  void (*pushstring)(lua_State *, const char *);
  void (*call)(lua_State *, int, int);
  int (*loadbuffer)(lua_State *, const char *, size_t, const char *);
  int (*pcall)(lua_State *, int, int, int);
  int (*gettop)(lua_State *);
  void (*settop)(lua_State *, int);
  void (*insert)(lua_State *, int);
  void (*getfield)(lua_State *, int, const char *);
  void (*pushlstring)(lua_State *, const char *, size_t);
  const char *(*tolstring)(lua_State *, int, size_t *);
} lua_api;

#define LUAC_GLOBALSINDEX (-10002)
#define LUAC_MULTRET (-1)

// ser(...) turns any results into a deterministic string: numbers in full
// precision plus tostring, strings %q-quoted, tables with sorted keys, and
// only the type name for functions, userdata and threads (their addresses
// differ between runs).
static const char lua_prelude[] =
    "local function s(v, seen, depth)\n"
    "  local t = type(v)\n"
    "  if t == 'number' then\n"
    "    return string.format('%.17g', v) .. '|' .. tostring(v)\n"
    "  elseif t == 'string' then return string.format('%q', v)\n"
    "  elseif t == 'table' then\n"
    "    if seen[v] then return '<cycle>' end\n"
    "    if depth > 8 then return '<deep>' end\n"
    "    seen[v] = true\n"
    "    local items = {}\n"
    "    for k, x in pairs(v) do\n"
    "      items[#items + 1] = '[' .. s(k, seen, depth + 1) .. ']=' .. s(x, seen, depth + 1)\n"
    "    end\n"
    "    table.sort(items)\n"
    "    local mt = getmetatable(v) and 'mt' or ''\n"
    "    seen[v] = nil\n"
    "    return mt .. '{' .. table.concat(items, ',') .. '}'\n"
    "  elseif t == 'nil' or t == 'boolean' then return tostring(v)\n"
    "  else return t end\n"
    "end\n"
    "function ser(...)\n"
    "  local out = {}\n"
    "  for i = 1, select('#', ...) do out[i] = s((select(i, ...)), {}, 0) end\n"
    "  return select('#', ...) .. ':' .. table.concat(out, ' ')\n"
    "end\n"
    "function __dump(src) return string.dump(assert(loadstring(src, '=case'))) end\n";

// Hand-written cases: language features, every library function that does not
// touch files, error paths, and LNUM's integer/float boundaries.
static const char *const lua_fixed_cases[] = {
    // --- basics, numbers and LNUM ---------------------------------------
    "return 1, 2.5, -0, 1/0, -1/0, 0/0 ~= 0/0",
    "return 7 / 2, 7 % 3, -7 % 3, 7 % -3, 7.5 % 2, -7.5 % 2, 2 ^ 10, 2 ^ 0.5",
    "return 9223372036854775807, -9223372036854775807 - 1, 9223372036854775807 + 1",
    "return 9223372036854775807 * 2, 3037000500 * 3037000500, -3037000500 * 3037000500",
    "return 2^53, 2^53 + 1, 2^63, -2^63, 2^64, 1e15, 1e16, 123456789012345678",
    "return 0x7fffffffffffffff, 0xffffffffffffffff, 0x10, 0XaBc, 0x",
    "return 10 // 3",
    "return math.floor(-3.5), math.ceil(-3.5), math.floor(2^62 + 0.0), math.floor(1e300)",
    "return math.huge, -math.huge, math.pi, math.abs(-9223372036854775807 - 1)",
    "return 5 // 0",
    "return 1 % 0, 1.5 % 0, 0 % 0",
    "return (2^31) * (2^31), 2147483647 + 1, -2147483648 - 1, 65536 * 65536",
    "return 4611686018427387904 + 4611686018427387904, 4611686018427387904 * 2",
    "return 100 - 1e-9, 0.1 + 0.2, 1 - 0.9, 3 * 1.1",
    "return 1e308 * 10, -1e308 * 10, 5e-324, 5e-324 / 2",
    "return tostring(1e100), tostring(-0.0), tostring(1/3), tostring(12345678901234)",
    "return tonumber('10'), tonumber('  10  '), tonumber('0x1F'), tonumber('1e2'), "
    "tonumber('1e'), tonumber(''), tonumber('9223372036854775808'), tonumber('-9223372036854775808')",
    "return tonumber('ff', 16), tonumber('zz', 36), tonumber('777', 8), tonumber('102', 2), "
    "tonumber('7fffffffffffffff', 16), tonumber('8000000000000000', 16)",
    "return tonumber(' 0x10 '), tonumber('1 2'), tonumber('.5'), tonumber('5.'), tonumber('inf'), "
    "tonumber('nan'), tonumber('-0x10')",
    // Not 2^53 + 1 vs 2^53 here: the game's LNUM folds 64-bit constants whose low
    // 32 bits index the constant table's array part onto that slot (see above).
    "return 1 == 1.0, 2^31 == 2^31 + 1, 4294967295 == 4294967294, -0 == 0",
    "return 1 < 2, 1 < 1.5, 'a' < 'b', 'Z' < 'a', '' < 'a', 9223372036854775807 < 9223372036854775807 + 1",
    "return 10 .. 20, 1.5 .. '', 9223372036854775807 .. '', -0.0 .. ''",
    "return #'hello', #{1, 2, 3, nil, 5}, #{n = 1}, #{}",
    "return '10' + 5, '3' * '4', '0x10' + 0, ' 2 ' ^ 2, '9223372036854775807' + 0",
    "local a = 'x' + 1",
    "local t = {} return t < t",
    "return {} == {}, 'abc' == 'abc'",
    "return -(-9223372036854775807 - 1), - -9223372036854775807",
    "return math.fmod(7, 3), math.fmod(-7, 3), math.fmod(7, -3), math.fmod(7.5, 2), math.fmod(1, 0)",
    "return math.modf(3.7), math.modf(-3.7), math.modf(5), math.modf(1/0)",
    "return math.frexp(8), math.frexp(0.3), math.ldexp(0.5, 4), math.ldexp(1, 1024)",
    "return math.sqrt(2), math.sqrt(-1) ~= math.sqrt(-1), math.exp(1), math.log(10), math.log10(1000)",
    "return math.sin(1), math.cos(1), math.tan(1), math.asin(0.5), math.acos(0.5), math.atan(1), "
    "math.atan2(1, -1), math.sinh(1), math.cosh(1), math.tanh(1)",
    "return math.deg(math.pi), math.rad(180), math.pow(2, 0.5), math.pow(-8, 1/3)",
    "return math.max(3, 9, -1, 9.5), math.min(3, 9, -1, -1.5), math.max(1), math.min(2^63, 1)",
    "return math.max()",
    "math.randomseed(42) return math.random(), math.random(100), math.random(-5, 5), math.random(1, 1)",
    "math.randomseed(7) local t = {} for i = 1, 20 do t[i] = math.random(1000) end return t",
    "return math.random(0)",
    "return math.random(5, 1)",
    "return math.floor(-0.0), math.ceil(-0.5), math.abs(-0.0), math.floor(9223372036854775807)",
    "local x = 0 for i = 1, 10 do x = x + i end return x",
    "local x = 0 for i = 10, 1, -3 do x = x * 10 + i end return x",
    "local x = {} for i = 1, 2, 0.25 do x[#x + 1] = i end return x",
    "local n = 0 for i = 9223372036854775800, 9223372036854775807 do n = n + 1 if n > 20 then break end end return n",
    "for i = 1, 'x' do end",
    "for i = 1, 10, 0 do return i end",
    "local t = {} for i = 1.0, 3 do t[i] = i end return t, t[1], t[1.0], t[2.0]",
    "local t = {[1] = 'a', [1.0] = 'b', [2^53] = 'c'} return t[1], t[2^53], t[9007199254740992]",
    "local t = {} t[9223372036854775807] = 1 t[-9223372036854775807 - 1] = 2 t[2^63] = 3 return t",
    // --- strings --------------------------------------------------------
    "return string.len('abc'), ('x'):rep(5), string.rep('ab', 3, nil), string.rep('', 1000), string.rep('a', 0), string.rep('a', -1)",
    "return string.sub('hello', 2, 4), ('hello'):sub(-3), ('hello'):sub(2), ('hello'):sub(0), ('hello'):sub(10), ('hello'):sub(-100, 2)",
    "return string.upper('Hello W\\195\\182rld'), string.lower('ABC def'), string.reverse('abc'), string.reverse('')",
    "return string.byte('ABC'), string.byte('ABC', 2), string.byte('ABC', 1, -1), string.byte('', 1), string.byte('ABC', 10)",
    "return string.char(72, 105, 0, 255), string.char()",
    "return string.char(256)",
    "return string.format('%d %5d %-5d| %05d %+d %x %X %o %c', 42, 42, 42, 42, 42, 255, 255, 8, 65)",
    "return string.format('%e %E %f %g %G %.3f %10.2f %-10.2f| %.0f', 1234.5678, 1234.5678, 1234.5678, 1234.5678, 0.00001234, 3.14159, 2.5, 2.5, 0.5)",
    "return string.format('%s %10s %-10s| %.2s %q', 'abc', 'abc', 'abc', 'abc', 'a\\nb\\0c\"d\\\\')",
    "return string.format('%d', 9223372036854775807), string.format('%d', -9223372036854775807 - 1), string.format('%x', -1)",
    "return string.format('%d', 3.7), string.format('%d', -3.7), string.format('%d', 2^63), string.format('%d', 1e20)",
    "return string.format('%5.1s|%%|%i', 'xyz', 7)",
    "return string.format('%y', 1)",
    "return string.format('%d', 'x')",
    "return string.format('%s %s', 1)",
    "return string.format('%s', nil)",
    "return string.format('%10.3q', 'x')",
    "return string.format('%-+ #0d', 5)",
    "return string.format('%123d', 5)",
    "return string.format('%g %g %g %g', 1e20, 1e-20, 100000, 1000000)",
    "return string.format('%.14g %.14g %.20g', 0.1, 1/3, 0.1)",
    "return string.find('hello world', 'o w'), string.find('hello', 'l+'), string.find('hello', 'xyz'), string.find('a.b', '.', 1, true)",
    "return string.find('hello', ''), string.find('hello', '', 10), string.find('', ''), string.find('abc', 'b', -1), string.find('abc', 'b', -10)",
    "return string.find('key = value', '(%w+)%s*=%s*(%w+)'), string.find('  x', '^%s*()')",
    "return string.match('2024-01-15', '(%d+)-(%d+)-(%d+)'), string.match('hello', '.-(l+)(.*)'), string.match('abc', '()b()')",
    "return string.match('  trim me  ', '^%s*(.-)%s*$'), string.match('[[x]]', '%[%[(.*)%]%]'), string.match('f(a(b)c)', '%b()')",
    "return string.match('THE (quick) fox', '%f[%a]%a+'), string.match('x=1, y=2', '%f[%w]%w+=%d'), string.match('aaa', 'a-b')",
    "return string.match('hello', '%')",
    "return string.match('hello', '[a')",
    "return string.match('hello', '(()')",
    "return string.match('hello', '%1')",
    "return string.match(string.rep('a', 300), string.rep('(a)', 33))",
    "return string.gsub('hello world', 'o', '0'), string.gsub('hello', '', '-'), string.gsub('abc', '%w', '%0%0'), string.gsub('abc', 'b', '%%')",
    "return string.gsub('hello world', '(%w+)', '<%1>'), string.gsub('abc', '.', {a = 1, b = true}), string.gsub('abc', '.', function(c) return c:byte() end)",
    "return string.gsub('abc', '.', function(c) if c == 'b' then return false end return c .. c end), string.gsub('aaa', 'a', 'b', 2), string.gsub('aaa', 'a', 'b', 0)",
    "return string.gsub('abc', '(b)', '%2')",
    "return string.gsub('abc', '.', 1)",
    "return string.gsub('x', 'x', {x = {}})",
    "local t = {} for w in string.gmatch('one two  three', '%a+') do t[#t + 1] = w end return t",
    "local t = {} for k, v in string.gmatch('a=1, b=2, c=3', '(%w+)=(%w+)') do t[k] = tonumber(v) end return t",
    "local t = {} for p in string.gmatch('abc', '()') do t[#t + 1] = p end return t",
    "return ('%d items'):format(3), ('abc'):upper(), #('x'):rep(100)",
    "local s = '' for i = 0, 255 do s = s .. string.char(i) end return string.format('%q', s)",
    "local s = '' for i = 0, 255 do s = s .. string.char(i) end return s:upper(), s:lower(), s:gsub('%c', '.'), s:gsub('%p', ''), s:gsub('%s', '_'), s:gsub('%x', '')",
    "local s = '' for i = 0, 255 do s = s .. string.char(i) end return s:gsub('%a', ''), s:gsub('%d', ''), s:gsub('%l', ''), s:gsub('%u', ''), s:gsub('%w', ''), s:gsub('[%z]', 'Z')",
    "return 'a\\tb\\nc\\\\d\\'e\\\"f\\65\\066\\0067'",
    "return [[long\nstring]], [==[with ]] inside]==], #[[\n\nx]]",
    "return 'unfinished",
    "return [[unfinished",
    "return '\\300'",
    "return ('x'):bad()",
    "return string.rep('abc', 10000):len(), select('#', string.byte(string.rep('a', 100), 1, -1))",
    "return tostring(nil), tostring(true), tostring(12), tostring('x'), type(tostring({})), type(print)",
    "return #string.format('%99.99f', 1/3)",
    // --- tables -------------------------------------------------------
    "local t = {5, 2, 8, 1, 9, 3} table.sort(t) return t",
    "local t = {5, 2, 8, 1, 9, 3} table.sort(t, function(a, b) return a > b end) return t",
    "local t = {'banana', 'apple', 'Cherry', 'date'} table.sort(t) return t",
    "local t = {3, 1, 'x'} table.sort(t)",
    "local t = {} for i = 1, 200 do t[i] = (i * 7919) % 211 end table.sort(t) return t",
    "local t = {1, 2, 3} table.sort(t, function(a, b) return true end) return t",
    "local t = {1, 2, 3} table.insert(t, 4) table.insert(t, 1, 0) table.insert(t, 3, 'x') return t",
    "local t = {1, 2, 3} return table.remove(t), table.remove(t, 1), t, table.remove({}), table.remove(t, 5)",
    "return table.concat({1, 2, 3}, ', '), table.concat({'a', 'b'}, '', 2), table.concat({}, 'x'), table.concat({1, 2, 3}, '-', 2, 3)",
    "return table.concat({1, {}, 3})",
    "return table.maxn({1, 2, nil, 4, [10] = 1, [10.5] = 2}), table.maxn({}), table.getn({1, 2, 3})",
    "local t = setmetatable({}, {__index = function(t, k) return k * 2 end}) return t[21], rawget(t, 21)",
    "local t = {} table.foreach({a = 1}, function(k, v) t[k] = v end) table.foreachi({5, 6}, function(i, v) t[i] = v end) return t",
    "table.insert({}, 1, 2, 3)",
    "local t = {n = 0} table.setn(t, 3)",
    "return next({}), next({10}), type(next), rawlen",
    "local t = {10, 20, 30, x = 1} local s = 0 for i, v in ipairs(t) do s = s + i * v end return s",
    "local t = {a = 1, b = 2, c = 3} local s = 0 for k, v in pairs(t) do s = s + v end return s",
    "return next({}, 'nokey')",
    "local t = {1, 2, 3} for k in pairs(t) do t[k] = nil end return t, next(t)",
    "return unpack({1, 2, 3}), unpack({1, 2, 3}, 2), unpack({1, 2, 3}, 2, 5), select('#', unpack({}, 1, 3))",
    "return unpack({}, 1, 1e8)",
    "return select('#'), select('#', nil, nil), select(2, 'a', 'b', 'c'), select(-1, 'a', 'b')",
    "return select(0, 1)",
    "local t = {} t[1] = 1 t[2] = 2 t[4] = 4 return #t == 4 or #t == 2",
    "local t = {} for i = 1, 1000 do t[i] = i end for i = 1, 1000, 2 do t[i] = nil end local c = 0 for _ in pairs(t) do c = c + 1 end return c",
    "local t = {} t[nil] = 1",
    "local t = {} t[0/0] = 1",
    "return rawequal(1, 1.0), rawequal('a', 'a'), rawequal({}, {}), rawget({5}, 1), rawset({}, 'k', 'v')",
    // --- functions, closures, varargs, metatables -----------------------------
    "local function f(...) return select('#', ...), ... end return f(1, nil, 3, nil)",
    "local function f(a, b, ...) local t = {...} return a, b, #t, arg end return f(1)",
    "local function fact(n) if n <= 1 then return 1 end return n * fact(n - 1) end return fact(20), fact(21), fact(25)",
    "local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end return fib(25)",
    "local function c() local n = 0 return function() n = n + 1 return n end end local a, b = c(), c() a() a() return a(), b()",
    "local fs = {} for i = 1, 3 do fs[i] = function() return i end end return fs[1](), fs[2](), fs[3]()",
    "local function tail(n) if n == 0 then return 'done' end return tail(n - 1) end return tail(100000)",
    "local function rec(n) return 1 + rec(n + 1) end return rec(1)",
    "local function f() return f() + 1 end local ok, e = pcall(f) return ok, e",
    "local mt = {__add = function(a, b) return 'add' end, __eq = function() return true end, __lt = function() return true end, "
    "__le = function() return false end, __concat = function() return 'cat' end, __unm = function() return 'unm' end, "
    "__len = function() return 99 end, __call = function(self, x) return 'call' .. x end} "
    "local a, b = setmetatable({}, mt), setmetatable({}, mt) return a + 1, 1 + a, a == b, a < b, a <= b, a .. 'x', -a, #a, a(5)",
    "local mt = {__index = {x = 1}, __newindex = function(t, k, v) rawset(t, k, v * 2) end} local t = setmetatable({}, mt) t.y = 5 return t.x, t.y",
    "local mt = {__tostring = function() return 'OBJ' end} return tostring(setmetatable({}, mt))",
    "local mt = {__tostring = function() return 5 end} return tostring(setmetatable({}, mt))",
    "local mt = {__metatable = 'locked'} local t = setmetatable({}, mt) return getmetatable(t), pcall(setmetatable, t, {})",
    "local t = setmetatable({}, {__mode = 'k'}) return getmetatable(t).__mode",
    "local a = setmetatable({}, {__div = function() return 'div' end, __mod = function() return 'mod' end, __pow = function() return 'pow' end, __mul = function() return 'mul' end, __sub = function() return 'sub' end}) return a / 1, a % 1, a ^ 1, a * 1, a - 1",
    "local a = setmetatable({}, {__index = function(t, k) return t[k .. 'x'] end}) return a.b",
    "return getmetatable('').__index == string, getmetatable(1), getmetatable(nil)",
    "local t = setmetatable({}, {__index = string}) return t.format('%d', 5)",
    "return (function(...) return select('#', ...) end)(table.unpack and 1 or nil)",
    "local f = function(a, b) return a end return f(1, 2, 3), (f(1, 2))",
    "local t = {f = function(self, x) return self.v + x end, v = 10} return t:f(5), t.f(t, 6)",
    "local x = 10 do local x = 20 end return x",
    "local a, b, c = (function() return 1, 2, 3 end)() return a, b, c",
    "local a, b = 1 return a, b",
    "local t = {(function() return 1, 2, 3 end)(), (function() return 1, 2, 3 end)()} return #t",
    "return (nil)()",
    "local t = nil return t.x",
    "local t = {} return t.x.y",
    "return #nil",
    "return -{}",
    "return {} .. 'x'",
    "return 1 < 'x'",
    "return {} < {}",
    "return math.floor('x')",
    "undefinedfunction()",
    "local t = {} t.a.b.c = 1",
    "string.nope()",
    // --- errors, pcall, xpcall --------------------------------------------
    "error('plain')",
    "error('nolevel', 0)",
    "error('level2', 2)",
    "error({code = 42})",
    "error()",
    "error(nil)",
    "error(12)",
    "return pcall(error, 'x'), pcall(error, {1}), pcall(error)",
    "return pcall(function() error('inner', 1) end)",
    "return pcall(function() local x = nil; return x.y end)",
    "return xpcall(function() error('boom') end, function(m) return 'handled: ' .. m end)",
    "return xpcall(function() return 1, 2 end, print)",
    "return xpcall(function() error('boom') end, function(m) error('again') end)",
    "return pcall(pcall), pcall(xpcall), pcall(select)",
    "return pcall(string.rep), pcall(string.sub), pcall(table.concat, 1), pcall(setmetatable, 1, {})",
    "return pcall(tostring), pcall(tonumber), pcall(tonumber, 'x', 99), pcall(tonumber, 10, 1)",
    "return pcall(ipairs), pcall(pairs, nil), pcall(rawget, 1, 1), pcall(rawset, {}, nil, 1)",
    "return assert(1, 2, 3)",
    "assert(false)",
    "assert(nil, 'custom assert')",
    "assert(false, {1})",
    "return select(2, pcall(error, setmetatable({}, {__tostring = function() return 'E' end})))",
    "local ok, e = pcall(string.format, '%d', {}) return ok, e",
    "local ok, e = pcall(function() return 1 + {} end) return ok, e",
    "local ok, e = pcall(function() local a = {} return a.b.c end) return ok, e",
    "local ok, e = pcall(function() local up = nil return function() return up.x end end) return ok, select(2, pcall(e))",
    "return debug.traceback('msg', 1) ~= nil, type(debug.traceback()), debug.traceback(12)",
    "local function f() return debug.traceback('tb') end return f()",
    "return debug.getinfo(1, 'S').source, debug.getinfo(1, 'S').what, debug.getinfo(1, 'l').currentline, debug.getinfo(print).what",
    "local function f(a, b) local c = 3 return debug.getlocal(1, 1), debug.getlocal(1, 2), debug.getlocal(1, 3) end return f(10, 20)",
    "local x = 5 local function f() return x end return debug.getupvalue(f, 1), debug.setupvalue(f, 1, 7), f()",
    "local t = {} debug.setmetatable(t, {__index = {z = 1}}) return t.z, debug.getmetatable(t).__index.z, debug.getregistry ~= nil",
    "local n = 0 debug.sethook(function() n = n + 1 end, 'l') local x = 1 x = x + 1 x = x + 1 debug.sethook() return n > 0, debug.gethook()",
    "return debug.getinfo(100), pcall(debug.getinfo, 1, '>')",
    "local f = function() end return debug.getfenv(f) == _G, getfenv(1) == _G, getfenv(0) == _G",
    "local env = {x = 5} local f = loadstring('return x') setfenv(f, env) return f(), pcall(setfenv, 1, 1)",
    "return setfenv(print, {})",
    // --- load, loadstring, string.dump ------------------------------------
    "return loadstring('return 1 + 1')()",
    "return loadstring('return +')",
    "return loadstring('x = ', 'mychunk')",
    "return loadstring('return ...', '=named')(1, 2)",
    "return loadstring(string.dump(function(a) return a * 2 end))(21)",
    "return loadstring('\\27Lua garbage')",
    "local parts = {'return ', '1', ' + ', '2'} local i = 0 return load(function() i = i + 1 return parts[i] end)()",
    "return load(function() return {} end)",
    "return load(function() error('reader') end)",
    "return pcall(load, 'not a function')",
    "local s = string.dump(loadstring('local a = 9223372036854775807 local b = 2^63 return a, b, 0.1')) return #s > 0, loadstring(s)()",
    "return string.dump(print)",
    "local f = loadstring('return function() end')() return pcall(string.dump, f)",
    "return #string.dump(function() local t = {} for i = 1, 100 do t[i] = i * 1.5 end return t end)",
    // --- syntax errors -------------------------------------------------------
    "x = = 1",
    "local function end",
    "return 1 +",
    "if x then",
    "for i = 1 do end",
    "local t = {1, 2,, 3}",
    "return ...",
    "function f() return ... end",
    "x = 1 return; y = 2",
    "break",
    "goto label",
    "local a <const> = 1",
    "return 0x1p4",
    "return 1e",
    "return 3..2",
    "a.b:c = 1",
    "f() = 1",
    "local x = 'a' 'b'",
    "return \"\\x41\"",
    "return 'a\\\nb'",
    "--[[ unfinished comment",
    "return 1 -- comment\n + 2 --[==[ long ]==] + 3",
    "local a = {[1] = 1, [2] = 2, n = 3, 4, 5; 6} return a",
    "return (1)(2)",
    "x, y = 1",
    "local t = {} t.x, t.y = 1, 2, 3 return t",
    "local a, b, c = 1, 2 return a, b, c",
    "do local a = 1 end return a",
    "while false do end repeat local x = 1 until x == 1 return 'ok'",
    "local i = 0 while true do i = i + 1 if i > 5 then break end end return i",
    "local i = 0 repeat i = i + 1 until i >= 3 return i",
    "if nil then return 1 elseif false then return 2 else return 3 end",
    "return not nil, not 0, not '', nil and 1, false or 2, 1 and 2, nil or false",
    "return 1 and nil, false and error('x'), true or error('y')",
    "return 2 ^ 3 ^ 2, -2 ^ 2, not 1 == 2, 1 .. 2 .. 3, 'a' .. 'b' == 'ab'",
    "return 1 + 2 * 3 - 4 / 2, (1 + 2) * 3, 7 % 4 * 2, 2 * 3 % 4",
    "local s = 'return ' .. string.rep('(', 300) .. '1' .. string.rep(')', 300) return loadstring(s)",
    "local s = 'local t = {' .. string.rep('{', 300) .. string.rep('}', 300) .. '}' return loadstring(s)",
    "local s = {} for i = 1, 250 do s[#s + 1] = 'local v' .. i .. ' = ' .. i end return loadstring(table.concat(s, '\\n'))",
    "local s = {} for i = 1, 70 do s[#s + 1] = 'a' .. i end return loadstring('local ' .. table.concat(s, ',') .. ' = 1 return function() return ' .. table.concat(s, '+') .. ' end')",
    "local s = 'return ' .. string.rep('1 + ', 1000) .. '1' return loadstring(s)()",
    "local s = 'return {' .. string.rep('1, ', 100000) .. '}' return #loadstring(s)()",
    "local s = 'x = 1 ' .. string.rep('x = x + 1 ', 5000) .. 'return x' return loadstring(s)()",
    // --- coroutines -------------------------------------------------------
    "local co = coroutine.create(function(a, b) local c = coroutine.yield(a + b) local d, e = coroutine.yield(c * 2) return d + e end) "
    "return coroutine.resume(co, 1, 2), coroutine.resume(co, 10), coroutine.resume(co, 3, 4), coroutine.resume(co), coroutine.status(co)",
    "local gen = coroutine.wrap(function() for i = 1, 3 do coroutine.yield(i) end end) return gen(), gen(), gen()",
    "local gen = coroutine.wrap(function() error('in coroutine') end) return gen()",
    "local co = coroutine.create(function() error({1, 2}) end) return coroutine.resume(co)",
    "return coroutine.yield(1)",
    "return coroutine.running(), coroutine.status(coroutine.create(print))",
    "local co co = coroutine.create(function() return coroutine.status(co), coroutine.running() == co end) return coroutine.resume(co)",
    "local co = coroutine.create(function() coroutine.resume(coroutine.running()) end) return coroutine.resume(co)",
    "local co = coroutine.wrap(function() local t = {} setmetatable(t, {__index = function() return coroutine.yield(1) end}) return t.x end) return pcall(co)",
    "local co = coroutine.create(function() pcall(coroutine.yield, 1) return 2 end) return coroutine.resume(co), coroutine.resume(co)",
    "return pcall(coroutine.resume, 1), pcall(coroutine.create, 1), pcall(coroutine.status, nil)",
    "local function p(n) if n == 0 then coroutine.yield('deep') return 'back' end return p(n - 1) end local co = coroutine.wrap(p) return co(150), co()",
    // --- os and package (no files) -----------------------------------------
    "return os.date('!%Y-%m-%d %H:%M:%S', 0), os.date('!%Y-%m-%d %H:%M:%S', 1234567890), os.date('!%j %a %A %b %B %p %y', 1e9)",
    "local t = os.date('!*t', 1234567890) return t",
    "return os.time({year = 2020, month = 1, day = 1, hour = 12}) - os.time({year = 2019, month = 1, day = 1, hour = 12})",
    "return os.time({year = 2000, month = 13, day = 40, hour = 0, isdst = false}) - os.time({year = 2001, month = 2, day = 9, hour = 0, isdst = false})",
    "return pcall(os.time, {year = 2000}), os.difftime(10, 5), os.difftime(5)",
    "return type(os.time()), type(os.clock()), os.getenv('RS_LUA_TEST_UNSET_VAR'), type(os.tmpname)",
    "return os.date('!%c', 0) ~= nil, pcall(os.date, '*t', 'x')",
    "return os.setlocale(), os.setlocale('C'), os.setlocale(nil, 'numeric'), os.setlocale('xx_INVALID')",
    "return os.remove('/nonexistent/rs_lua_test'), os.rename('/nonexistent/a', '/nonexistent/b')",
    "package.preload.mymod = function(name) return {name = name, v = 1} end local m = require('mymod') return m, require('mymod') == m, package.loaded.mymod == m",
    "return type(package.loaders), #package.loaders, type(package.path), type(package.cpath), package.config",
    "module('mod_x', package.seeall) x = 5 return mod_x.x, _NAME, _PACKAGE",
    "return pcall(package.loadlib, 'libnothing.dylib', 'f')",
    "package.loaded.fake = 'value' return require('fake')",
    "package.preload.err = function() error('in loader') end return pcall(require, 'err')",
    "package.preload.cyc = function() return require('cyc') end return pcall(require, 'cyc')",
    // --- globals and environment ------------------------------------------
    "return _VERSION, _LNUM, type(_G), _G._G == _G, type(newproxy), type(gcinfo)",
    "local p = newproxy(true) getmetatable(p).__index = function() return 'proxy' end return p.x, type(p), type(newproxy(p))",
    "return pcall(newproxy, 1)",
    "return collectgarbage('step') ~= nil, collectgarbage('setpause', 200), collectgarbage('setstepmul', 200), collectgarbage('collect'), collectgarbage('stop'), collectgarbage('restart')",
    "return pcall(collectgarbage, 'bogus')",
    "local keys = {} for k in pairs(_G) do keys[#keys + 1] = k end table.sort(keys) return table.concat(keys, ' ')",
    "local libs = {} for _, n in ipairs({'string', 'table', 'math', 'os', 'coroutine', 'debug', 'package'}) do "
    "local ks = {} for k in pairs(_G[n]) do ks[#ks + 1] = k end table.sort(ks) libs[n] = table.concat(ks, ' ') end return libs",
    "local mt = getmetatable('') local ks = {} for k in pairs(mt) do ks[#ks + 1] = k end return ks",
    "setmetatable(_G, {__index = function(_, k) return 'missing ' .. k end}) return undefinedname",
    "setmetatable(_G, {__newindex = function(_, k) error('no globals: ' .. k) end}) newglobal = 1",
    // --- weak tables and gc (outcomes that do not depend on timing) ---------
    "local t = setmetatable({}, {__mode = 'v'}) t[1] = {} t[2] = 'str' collectgarbage() collectgarbage() return t[1], t[2]",
    "local t = setmetatable({}, {__mode = 'k'}) t[{}] = 1 t['s'] = 2 collectgarbage() collectgarbage() local n = 0 for _ in pairs(t) do n = n + 1 end return n",
    "local log = {} do local p = newproxy(true) getmetatable(p).__gc = function() log[#log + 1] = 'gc' end end collectgarbage() collectgarbage() return #log",
    "local t = {} for i = 1, 10000 do t[i] = tostring(i) .. 'x' end collectgarbage() return #t, t[9999]",
    "local s = {} for i = 1, 2000 do s[i] = string.rep('x', i % 50) end return #table.concat(s)",
    // --- larger programs -----------------------------------------------------
    "local function qs(t, lo, hi) if lo < hi then local p, i = t[hi], lo - 1 for j = lo, hi - 1 do if t[j] <= p then i = i + 1 t[i], t[j] = t[j], t[i] end end "
    "t[i + 1], t[hi] = t[hi], t[i + 1] qs(t, lo, i) qs(t, i + 2, hi) end end local t = {} local x = 12345 for i = 1, 500 do x = (x * 1103515245 + 12345) % 2147483648 t[i] = x end "
    "qs(t, 1, #t) local ok = true for i = 2, #t do ok = ok and t[i - 1] <= t[i] end return ok, t[1], t[250], t[500]",
    "local sieve, primes = {}, {} for i = 2, 2000 do if not sieve[i] then primes[#primes + 1] = i for j = i * i, 2000, i do sieve[j] = true end end end return #primes, primes[#primes]",
    "local function q(n) local c = 0 local cols, d1, d2 = {}, {}, {} local function go(r) if r > n then c = c + 1 return end for col = 1, n do "
    "if not cols[col] and not d1[r + col] and not d2[r - col + n] then cols[col], d1[r + col], d2[r - col + n] = true, true, true go(r + 1) "
    "cols[col], d1[r + col], d2[r - col + n] = nil, nil, nil end end end go(1) return c end return q(6), q(7)",
    "local s = 0 for i = 1, 100000 do s = s + i * i end return s",
    "local s = 0.0 for i = 1, 10000 do s = s + 1 / i end return s",
    "local h = 0 for i = 1, 5000 do h = (h * 31 + i) % 4294967296 end return h",
    "local h = 1 for i = 1, 70 do h = h * 3 end return h, h % 7, h / 3",
    "local t = {} for i = 1, 100 do t[#t + 1] = string.format('%03d:%x', i, i * 2654435761 % 2^32) end return table.concat(t, ',')",
    "local words = {} for w in ('the quick brown fox jumps over the lazy dog the end'):gmatch('%a+') do words[w] = (words[w] or 0) + 1 end return words",
    "local function serialize(o) if type(o) == 'table' then local r = {} for k, v in pairs(o) do r[#r + 1] = tostring(k) .. '=' .. serialize(v) end table.sort(r) return '{' .. table.concat(r, ';') .. '}' end return tostring(o) end "
    "return serialize({1, 2, {a = 'x', b = {c = true}}, n = 0.5})",
    "local Account = {} Account.__index = Account function Account.new(b) return setmetatable({balance = b}, Account) end "
    "function Account:deposit(v) self.balance = self.balance + v end function Account:withdraw(v) if v > self.balance then error('insufficient funds') end self.balance = self.balance - v end "
    "local a = Account.new(100) a:deposit(50) local ok, e = pcall(a.withdraw, a, 500) return a.balance, ok, e",
    "local json = {} local function enc(v) local t = type(v) if t == 'table' then local r = {} for i, x in ipairs(v) do r[i] = enc(x) end return '[' .. table.concat(r, ',') .. ']' "
    "elseif t == 'string' then return '\"' .. v:gsub('[%c\"\\\\]', function(c) return string.format('\\\\u%04x', c:byte()) end) .. '\"' else return tostring(v) end end "
    "return enc({1, 'two', {3, 'f\\nour'}, true, 5.5})",
};

#define LUA_FIXED_CASES ((int)(sizeof lua_fixed_cases / sizeof *lua_fixed_cases))
#define LUA_GEN_CASES 3000
#define LUA_CASES (LUA_FIXED_CASES + LUA_GEN_CASES)

// Random operands that sit on LNUM's integer/float boundaries.
static inline void lua_gen_operand(char *out, size_t cap) {
  static const char *const edge[] = {
      "0", "1", "-1", "2", "3", "7", "-7", "0.5", "-0.5", "1e15", "1e16", "2^31", "2^32", "2^53",
      "2^62", "2^63", "-2^63", "9223372036854775807", "(-9223372036854775807 - 1)",
      "4611686018427387904", "3037000499", "3037000500", "0.1", "1/3", "1e308", "-1e308",
      "math.huge", "-math.huge", "0x7fffffff", "0xffffffff", "'12'", "'0x10'", "' 3.5 '",
      "4294967296", "-4294967296", "1e-310", "65536", "255", "-0.0", "123456789012"};
  uint32_t r = vec_next() % 4;
  if (r == 0) {
    snprintf(out, cap, "%s", edge[vec_next() % (sizeof edge / sizeof *edge)]);
  } else if (r == 1) {
    snprintf(out, cap, "%d", (int)(vec_next() % 2001) - 1000);
  } else if (r == 2) {
    snprintf(out, cap, "%u%09u", vec_next() % 100000, vec_next() % 1000000000);
  } else {
    snprintf(out, cap, "%d.%03u", (int)(vec_next() % 201) - 100, vec_next() % 1000);
  }
}

// Generated case i (0-based among generated cases). Must be called in the
// same order by both tests (it drives the shared RNG).
static inline void lua_gen_case(char *out, size_t cap, int i) {
  static const char *const binops[] = {"+", "-", "*", "/", "%", "^", "==", "<", "<=", "..", "~="};
  static const char *const fmts[] = {"%d", "%5.2f", "%g", "%x", "%e", "%.0f", "%s", "%q", "%i",
                                     "%-8d|", "%+.3g", "%o", "%c", "%X", "%.14g", "%10.4s"};
  static const char *const pats[] = {"%a+", "%d+", "(%w)(%w)", "[aeiou]", "^%s*(.-)%s*$", "%p",
                                     ".-", "(.)%1", "[^%s]+", "%f[%w]%w+", "%bxy", "x*", "[%d%.]+"};
  char a[64], b[64], c[64];
  lua_gen_operand(a, sizeof a);
  lua_gen_operand(b, sizeof b);
  lua_gen_operand(c, sizeof c);
  switch (i % 6) {
    case 0:  // arithmetic and comparison mixes
      snprintf(out, cap, "return %s %s %s, %s %s %s, (%s %s %s) %s %s", a,
               binops[vec_next() % 11], b, b, binops[vec_next() % 6], c, a,
               binops[vec_next() % 6], b, binops[vec_next() % 6], c);
      break;
    case 1:  // number <-> string
      snprintf(out, cap,
               "local x = %s return tostring(x), tonumber(tostring(x)), x .. '', math.floor(x), "
               "string.format('%s', x)",
               a, fmts[vec_next() % (sizeof fmts / sizeof *fmts)]);
      break;
    case 2: {  // string library on random text
      char text[48];
      size_t n = vec_next() % 40;
      for (size_t k = 0; k < n; k++) {
        uint32_t r = vec_next() % 10;
        text[k] = r < 5 ? 'a' + vec_next() % 26 : r < 7 ? '0' + vec_next() % 10 : r < 8 ? ' ' : ".,x-y"[vec_next() % 5];
      }
      text[n] = 0;
      const char *p = pats[vec_next() % (sizeof pats / sizeof *pats)];
      snprintf(out, cap,
               "local s = '%s' return s:find('%s'), s:match('%s'), s:gsub('%s', '<%%0>'), s:sub(%d, %d), "
               "s:upper(), s:rep(%u, ',')",
               text, p, p, p, (int)(vec_next() % 21) - 10, (int)(vec_next() % 21) - 10, vec_next() % 3);
      break;
    }
    case 3: {  // table operations
      int n = (int)(vec_next() % 30);
      snprintf(out, cap,
               "local t = {} for i = 1, %d do t[i] = (i * %u) %% %u end table.sort(t) "
               "table.insert(t, %d, 'x') return #t, table.concat(t, ' '), table.remove(t, 1), select('#', unpack(t))",
               n, 1 + vec_next() % 9973, 1 + vec_next() % 97, 1 + (int)(vec_next() % (n + 1)));
      break;
    }
    case 4:  // math library
      snprintf(out, cap,
               "local x, y = %s, %s return math.max(x, y), math.min(x, y), math.fmod(x, y), math.abs(x), "
               "math.floor(x), math.ceil(y), math.modf(x), math.ldexp(1, %d), math.frexp(y)",
               a, b, (int)(vec_next() % 64));
      break;
    default:  // loops with integer accumulation that may overflow into floats
      snprintf(out, cap, "local s = %s for i = 1, %u do s = s * %s + i end return s, s == s",
               a, 1 + vec_next() % 40, b);
      break;
  }
}

// One case's outcome: load status, run status (-1 if not run), the result or
// error message string, and the chunk's string.dump bytes.
typedef struct {
  int load, run;
  char *result;
  size_t result_len;
  char *dump;
  size_t dump_len;
} lua_outcome;

static void *lua_test_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud;
  (void)osize;
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, nsize);
}

static inline char *lua_copy(const char *s, size_t n) {
  char *r = malloc(n + 1);
  if (s) memcpy(r, s, n);
  r[n] = 0;
  return r;
}

// Copy the top of the stack as a string ("<non-string>" if it has no string form).
static inline char *lua_take_top(const lua_api *api, lua_State *L, size_t *len) {
  const char *s = api->tolstring(L, -1, len);
  if (!s) {
    *len = 12;
    return lua_copy("<non-string>", 12);
  }
  return lua_copy(s, *len);
}

static inline void lua_run_case(const lua_api *api, const char *src, lua_outcome *o) {
  memset(o, 0, sizeof *o);
  o->run = -1;
  lua_State *L = api->newstate(lua_test_alloc, NULL);
  for (int i = 0; i < LUA_NOPEN; i++) {  // luaL_openlibs without io
    api->pushcclosure(L, api->open[i], 0);
    api->pushstring(L, lua_open_names[i]);
    api->call(L, 1, 0);
  }
  if (api->loadbuffer(L, lua_prelude, sizeof lua_prelude - 1, "=prelude") != 0 ||
      api->pcall(L, 0, 0, 0) != 0) {
    o->result = lua_take_top(api, L, &o->result_len);
    o->load = 99;
    api->close(L);
    return;
  }
  // Bytecode of the case (a syntax error gives the message instead).
  api->getfield(L, LUAC_GLOBALSINDEX, "__dump");
  api->pushlstring(L, src, strlen(src));
  api->pcall(L, 1, 1, 0);
  o->dump = lua_take_top(api, L, &o->dump_len);
  api->settop(L, 0);

  o->load = api->loadbuffer(L, src, strlen(src), "=case");
  if (o->load == 0) {
    o->run = api->pcall(L, 0, LUAC_MULTRET, 0);
    if (o->run == 0) {
      int n = api->gettop(L);
      api->getfield(L, LUAC_GLOBALSINDEX, "ser");
      api->insert(L, 1);
      if (api->pcall(L, n, 1, 0) != 0) o->run = 100;  // ser itself failed
    }
  }
  o->result = lua_take_top(api, L, &o->result_len);
  api->close(L);
}

static inline void lua_free_outcome(lua_outcome *o) {
  free(o->result);
  free(o->dump);
}

#endif
