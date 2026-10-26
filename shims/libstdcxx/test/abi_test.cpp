// Behavioural test of /usr/lib/libstdc++.6.dylib's GNU ABI: exercises every
// libstdc++ symbol family the game imports and checks the object layouts the
// game's inlined GCC 4.2 header code depends on (COW string rep, rb-tree and
// list node bases, ios_base / basic_ios field offsets, raw vtable pointers).
//
// Built with -nostdinc++ -nostdlib++ and linked only against libstdc++.6.tbd,
// so every C++ runtime reference (including __cxa_*, operator new, typeinfo)
// resolves through libstdc++ exactly as the game's do. Run it as arm64 and,
// built fat, under `arch -x86_64` for the Rosetta side.
#include "gnu_abi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

// Implemented in interop_libcxx.cpp (compiled against libc++).
int libcxx_catch(void (*thrower)());

static int g_pass, g_fail;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (cond) {                                                                \
            g_pass++;                                                              \
        } else {                                                                   \
            g_fail++;                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                          \
    } while (0)
#define SECTION(name) printf("[%s]\n", name)

// Vtables, VTTs and statics the game references directly (by data address).
extern "C" char vt_streambuf[] asm("__ZTVSt15basic_streambufIcSt11char_traitsIcEE");
extern "C" char vt_stringbuf[] asm("__ZTVSt15basic_stringbufIcSt11char_traitsIcESaIcEE");
extern "C" char vt_ostringstream[] asm("__ZTVSt19basic_ostringstreamIcSt11char_traitsIcESaIcEE");
extern "C" char vt_stringstream[] asm("__ZTVSt18basic_stringstreamIcSt11char_traitsIcESaIcEE");
extern "C" char vt_basic_ios[] asm("__ZTVSt9basic_iosIcSt11char_traitsIcEE");
extern "C" char vtt_ostringstream[] asm("__ZTTSt19basic_ostringstreamIcSt11char_traitsIcESaIcEE");
extern "C" char vtt_stringstream[] asm("__ZTTSt18basic_stringstreamIcSt11char_traitsIcESaIcEE");
extern "C" char vt_class_type_info[] asm("__ZTVN10__cxxabiv117__class_type_infoE");
extern "C" char vt_si_class_type_info[] asm("__ZTVN10__cxxabiv120__si_class_type_infoE");
extern "C" char vt_vmi_class_type_info[] asm("__ZTVN10__cxxabiv121__vmi_class_type_infoE");
extern "C" char vt_pointer_type_info[] asm("__ZTVN10__cxxabiv119__pointer_type_infoE");
extern "C" char vt_enum_type_info[] asm("__ZTVN10__cxxabiv116__enum_type_infoE");
extern "C" char vt_fundamental_type_info[] asm("__ZTVN10__cxxabiv123__fundamental_type_infoE");
extern "C" char ti_int[] asm("__ZTIi");
extern "C" char ti_const_uchar_ptr[] asm("__ZTIPKh");

static inline void *strip(void *p) {
#if defined(__arm64e__)
    return __builtin_ptrauth_strip(p, 0);
#else
    return p;
#endif
}

// --- COW std::string ------------------------------------------------------
static void test_string() {
    SECTION("std::string (COW)");
    std::string a("hello");
    CHECK(a.size() == 5);
    CHECK(memcmp(a.data(), "hello", 6) == 0);  // NUL-terminated in place
    CHECK(a.rep()->_M_refcount == 0);          // sole owner
    CHECK(a.rep()->_M_capacity >= 5);
    CHECK((char *)a.rep() + gnu::kRepHeader == a.data());

    {
        std::string b(a);  // copy shares the rep
        CHECK(b.data() == a.data());
        CHECK(a.rep()->_M_refcount == 1);
        b.append(" world", 6);  // write unshares
        CHECK(b.data() != a.data());
        CHECK(a.rep()->_M_refcount == 0);
        CHECK(b.size() == 11 && memcmp(b.data(), "hello world", 12) == 0);
        b.append(3, '!');
        b.push_back('?');
        CHECK(b.size() == 15 && memcmp(b.data(), "hello world!!!?", 16) == 0);
        b.replace(0, 5, "HOWDY", 5);
        CHECK(memcmp(b.data(), "HOWDY world!!!?", 16) == 0);
        b.append(a);
        CHECK(b.size() == 20);
        CHECK(b.find_first_not_of("HOWDY", 0, 5) == 5);
        b.assign("x", 1);
        CHECK(b.size() == 1 && b.data()[1] == 0);
        b.assign(a);  // shares again
        CHECK(b.data() == a.data() && a.rep()->_M_refcount == 1);
        b._M_leak_hard();  // what non-const operator[] does: unshare + mark leaked
        CHECK(b.data() != a.data() && b.rep()->_M_refcount == -1);
    }  // ~basic_string -> _Rep::_M_destroy
    CHECK(a.rep()->_M_refcount == 0);

    a.reserve(100);
    CHECK(a.rep()->_M_capacity >= 100 && a.size() == 5);
    a._M_mutate(1, 3, 1);  // "hello" -> "h?o": replaces 3 chars with 1 (uninitialised)
    CHECK(a.size() == 3 && a.data()[0] == 'h' && a.data()[2] == 'o');

    // The empty string is a shared static rep the game's inline code compares
    // against (_S_empty_rep_storage) to skip refcounting.
    std::string e("");
    CHECK((void *)e.rep() == (void *)std::string::_Rep::_S_empty_rep_storage);
    CHECK(e.size() == 0 && e.data()[0] == 0);

    // _Rep::_M_destroy frees a rep the caller has already released, as the
    // game's inlined _M_dispose does once the refcount drops below zero.
    {
        std::string *p = new std::string("disposable");
        std::string::_Rep *r = p->rep();
        r->_M_destroy(std::allocator<char>());
        p->_M_p = (char *)std::string::_Rep::_S_empty_rep_storage + gnu::kRepHeader;
        delete p;
    }

    SECTION("std::wstring (COW)");
    CHECK(std::wstring::_Rep::_S_terminal == L'\0');
    std::wstring w(L"wide");
    CHECK(w.size() == 4 && wmemcmp(w.data(), L"wide", 5) == 0);
    CHECK((char *)w.rep() + gnu::kRepHeader == (const char *)w.data());
    w.reserve(64);
    CHECK(w.rep()->_M_capacity >= 64 && wmemcmp(w.data(), L"wide", 5) == 0);
    std::wstring we(L"");
    CHECK((void *)we.rep() == (void *)std::wstring::_Rep::_S_empty_rep_storage);
}

// --- _Rb_tree (std::map / std::set) ---------------------------------------
struct IntNode : std::_Rb_tree_node_base {
    int key;
};

static int rb_check(std::_Rb_tree_node_base *n, int *ok) {  // returns black height
    if (!n)
        return 1;
    if (n->_M_color == std::_S_red &&
        ((n->_M_left && n->_M_left->_M_color == std::_S_red) ||
         (n->_M_right && n->_M_right->_M_color == std::_S_red)))
        *ok = 0;
    if (n->_M_left && n->_M_left->_M_parent != n)
        *ok = 0;
    if (n->_M_right && n->_M_right->_M_parent != n)
        *ok = 0;
    int l = rb_check(n->_M_left, ok), r = rb_check(n->_M_right, ok);
    if (l != r)
        *ok = 0;
    return l + (n->_M_color == std::_S_black);
}

static void test_rbtree() {
    SECTION("_Rb_tree");
    CHECK(sizeof(std::_Rb_tree_node_base) == 32);
    std::_Rb_tree_node_base header;  // what _Rb_tree_impl initialises inline
    header._M_color = std::_S_red;
    header._M_parent = 0;
    header._M_left = header._M_right = &header;

    enum { N = 200 };
    static IntNode nodes[N];
    for (int i = 0; i < N; i++) {
        IntNode *z = &nodes[i];
        z->key = (i * 37) % N;  // permutation of 0..N-1
        z->_M_left = z->_M_right = z->_M_parent = 0;
        // _M_insert_unique's descent, as inlined into the game.
        std::_Rb_tree_node_base *y = &header, *x = header._M_parent;
        bool left = true;
        while (x) {
            y = x;
            left = z->key < static_cast<IntNode *>(x)->key;
            x = left ? x->_M_left : x->_M_right;
        }
        std::_Rb_tree_insert_and_rebalance(y == &header || left, z, y, header);
    }
    int ok = 1;
    CHECK(header._M_parent->_M_color == std::_S_black);
    rb_check(header._M_parent, &ok);
    CHECK(ok);
    CHECK(static_cast<IntNode *>(header._M_left)->key == 0);       // leftmost
    CHECK(static_cast<IntNode *>(header._M_right)->key == N - 1);  // rightmost

    int expect = 0, sorted = 1;
    for (std::_Rb_tree_node_base *it = header._M_left; it != &header; it = std::_Rb_tree_increment(it))
        sorted &= static_cast<IntNode *>(it)->key == expect++;
    CHECK(sorted && expect == N);
    const std::_Rb_tree_node_base *cit = header._M_left;
    cit = std::_Rb_tree_increment(cit);
    CHECK(static_cast<const IntNode *>(cit)->key == 1);
    CHECK(static_cast<IntNode *>(std::_Rb_tree_decrement(&header))->key == N - 1);  // end()--

    for (int i = 0; i < N; i += 2) {  // erase the even keys
        IntNode *z = 0;
        for (int j = 0; j < N; j++)
            if (nodes[j].key == i)
                z = &nodes[j];
        std::_Rb_tree_rebalance_for_erase(z, header);
    }
    ok = 1;
    rb_check(header._M_parent, &ok);
    CHECK(ok);
    expect = 1, sorted = 1;
    for (std::_Rb_tree_node_base *it = header._M_left; it != &header; it = std::_Rb_tree_increment(it), expect += 2)
        sorted &= static_cast<IntNode *>(it)->key == expect;
    CHECK(sorted && expect == N + 1);
}

// --- _List_node_base (std::list) ------------------------------------------
struct ListNode {
    std::_List_node_base base;  // first member, as in _List_node<T>
    int key;
};

// Walks a list, checking the back links; returns the keys as a number like 1563.
static int list_keys(std::_List_node_base *head) {
    int keys = 0;
    for (std::_List_node_base *p = head->_M_next; p != head; p = p->_M_next) {
        if (p->_M_next->_M_prev != p)
            return -1;
        keys = keys * 10 + reinterpret_cast<ListNode *>(p)->key;
    }
    return keys;
}

static void test_list() {
    SECTION("_List_node_base");
    ListNode n[6];
    std::_List_node_base a, b;
    a._M_next = a._M_prev = &a;
    b._M_next = b._M_prev = &b;
    for (int i = 0; i < 6; i++) {
        n[i].key = i + 1;
        n[i].base.hook(i < 3 ? &a : &b);  // push_back
    }
    CHECK(list_keys(&a) == 123 && list_keys(&b) == 456);
    n[1].base.unhook();
    CHECK(list_keys(&a) == 13);
    n[2].base.transfer(&n[4].base, &b);  // splice b's [5, end) before 3
    CHECK(list_keys(&a) == 1563);
    CHECK(list_keys(&b) == 4);
    std::_List_node_base::swap(a, b);
    CHECK(list_keys(&a) == 4 && list_keys(&b) == 1563);
}

// --- iostreams ------------------------------------------------------------
static void *vptr_of(const void *obj) { return strip(*(void *const *)obj); }
// Name of the class a vtable (symbol address, not address point) belongs to.
static const char *type_name(const char *vtable) {
    const char *ti = (const char *)strip(*(void *const *)(vtable + 8));
    return (const char *)((uintptr_t)strip(*(void *const *)(ti + 8)) & ~(1ULL << 63));
}

static void test_streams() {
    SECTION("iostreams");
    static std::ios_base::Init init;  // the game's static initialiser
    { std::locale loc; CHECK(loc._M_impl != 0); }

    std::basic_ostringstream<char> *os = new std::basic_ostringstream<char>(std::_S_out);
    unsigned char *base = (unsigned char *)os;

    // vtable address points: [vbase offset, offset-to-top, rtti] + virtuals.
    CHECK(vptr_of(base) == vt_ostringstream + 24);
    CHECK(vptr_of(base + 8) == vt_stringbuf + 16);  // embedded _M_stringbuf
    long vbase = *(long *)((char *)vptr_of(base) - 24);
    CHECK(vbase == (long)(gnu::kOstringstreamSize - gnu::kBasicIosSize));  // 88
    unsigned char *ios = base + vbase;
    CHECK(*(void **)(ios + gnu::kIosStreambufOffset) == base + 8);  // rdbuf()
    CHECK(*(long *)(ios + gnu::kIosPrecisionOffset) == 6);
    CHECK(*(long *)(ios + gnu::kIosWidthOffset) == 0);
    CHECK(*(int *)(ios + gnu::kIosFlagsOffset) == (0x1000 | 0x2));  // skipws | dec
    CHECK(*(int *)(ios + gnu::kIosStateOffset) == 0);

    // The game's inlined stream constructors store *unsigned* vtable pointers.
    // Do the same for the streambuf and make libstdc++ dispatch through it
    // (write -> sputn -> virtual xsputn / overflow).
    *(void **)(base + 8) = vt_stringbuf + 16;

    std::ostream &o = *(std::ostream *)os;
    std::__ostream_insert(o, "pi=", 3);
    o._M_insert<double>(3.25);
    o.put(' ');
    o.write("n=", 2);
    o._M_insert<unsigned long>(1234567890123UL);
    std::basic_ios<char> &bios = *(std::basic_ios<char> *)ios;
    CHECK(bios.widen('x') == 'x');
    bios.clear(std::_S_goodbit);

    std::string s = ((std::basic_stringbuf<char> *)(base + 8))->str();
    printf("  ostringstream -> \"%.*s\"\n", (int)s.size(), s.data());
    CHECK(s.size() == 23 && memcmp(s.data(), "pi=3.25 n=1234567890123", 24) == 0);
    delete os;

    std::string in("42 65535 18446744073709551615");
    std::basic_istringstream<char> *is = new std::basic_istringstream<char>(in, std::_S_in);
    std::istream &i = *(std::istream *)is;
    unsigned int u = 0;
    unsigned short us = 0;
    unsigned long long ull = 0;
    i._M_extract(u)._M_extract(us)._M_extract(ull);
    CHECK(u == 42 && us == 65535 && ull == 18446744073709551615ULL);
    long ivbase = *(long *)((char *)vptr_of(is) - 24);
    CHECK(*(int *)((char *)is + ivbase + gnu::kIosStateOffset) == 2);  // eofbit
    delete is;

    // The rest of what the game's inline stream constructors reference.
    CHECK(vptr_of(vtt_ostringstream) == vt_ostringstream + 24);  // VTT[0]: complete object
    CHECK(vptr_of(vtt_stringstream) == vt_stringstream + 24);
    CHECK(strcmp(type_name(vt_streambuf), "St15basic_streambufIcSt11char_traitsIcEE") == 0);
    CHECK(strcmp(type_name(vt_basic_ios), "St9basic_iosIcSt11char_traitsIcEE") == 0);
}

// --- Exceptions, RTTI, runtime -------------------------------------------
struct Base {
    virtual ~Base() {}
};
struct Derived : Base {};
struct Other {
    virtual ~Other() {}
};
struct Multi : Base, Other {};
enum Color { kRed, kBlue };
extern "C" char ti_Base[] asm("__ZTI4Base");
extern "C" char ti_Derived[] asm("__ZTI7Derived");
extern "C" char ti_Multi[] asm("__ZTI5Multi");
extern "C" char ti_Color[] asm("__ZTI5Color");
struct Pure {
    virtual void f() = 0;
};

static void throw_length() { std::__throw_length_error("too long"); }
static void throw_alloc() { std::__throw_bad_alloc(); }
static void throw_int() { throw 7; }

static void test_runtime() {
    SECTION("exceptions / RTTI");
    int got = 0;
    try {
        throw_length();
    } catch (const std::exception &e) {
        got = strcmp(e.what(), "too long") == 0;
    }
    CHECK(got);
    got = 0;
    try {
        throw_alloc();
    } catch (const std::bad_alloc &) {
        got = 1;
    }
    CHECK(got);
    got = 0;
    try {
        try {
            throw_int();
        } catch (int) {
            throw;  // __cxa_rethrow
        }
    } catch (int v) {
        got = v == 7 && !std::uncaught_exception();
    }
    CHECK(got);

    // Exceptions crossing between libstdc++ and libc++ code: one runtime.
    CHECK(libcxx_catch(throw_length) == 1);  // caught as std::length_error by libc++
    CHECK(libcxx_catch(throw_alloc) == 2);   // caught as std::bad_alloc by libc++
    CHECK(libcxx_catch(throw_int) == 3);

    Derived d;
    Base *b = &d;
    CHECK(dynamic_cast<Derived *>(b) == &d);  // __dynamic_cast
    CHECK(dynamic_cast<Derived *>(new Base) == 0);
    // Typeinfo objects are GNU/Itanium layout: vptr into a __cxxabiv1 vtable + name.
    CHECK(vptr_of(ti_int) == vt_fundamental_type_info + 16);
    CHECK(strcmp(*(const char **)(ti_int + 8), "i") == 0);
    CHECK(vptr_of(ti_const_uchar_ptr) == vt_pointer_type_info + 16);
    CHECK(vptr_of(ti_Base) == vt_class_type_info + 16);
    CHECK(vptr_of(ti_Derived) == vt_si_class_type_info + 16);
    got = 0;
    try {
        throw Multi();
    } catch (const Other &) {
        got = 1;
    }
    CHECK(got && vptr_of(ti_Multi) == vt_vmi_class_type_info + 16);
    got = 0;
    try {
        throw kBlue;
    } catch (Color c) {
        got = c == kBlue;
    }
    CHECK(got && vptr_of(ti_Color) == vt_enum_type_info + 16);

    static int constructed = 0;
    struct Once {
        Once() { constructed++; }
    };
    for (int i = 0; i < 3; i++) {
        static Once once;  // __cxa_guard_acquire / release
        (void)once;
    }
    CHECK(constructed == 1);

    struct Impl : Pure {  // Pure's vtable references __cxa_pure_virtual
        void f() {}
    } impl;
    impl.f();

    void *p = operator new(16, std::nothrow);
    CHECK(p != 0);
    operator delete(p, std::nothrow);
    int *arr = new int[4];
    arr[3] = 1;
    delete[] arr;
}

int main() {
    printf("libstdc++ GNU ABI test (%s)\n",
#if defined(__x86_64__)
           "x86_64"
#else
           "arm64"
#endif
    );
    test_string();
    test_rbtree();
    test_list();
    test_streams();
    test_runtime();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
