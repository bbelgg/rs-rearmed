// Minimal declarations of the GNU libstdc++ 4.2.1 classes the game uses, laid
// out the way the game's inlined header code expects them. Compile with
// -nostdinc++ so nothing from libc++ (std::__1) is visible: these are the real
// `std::` names, so clang produces exactly the mangled symbols the game imports
// (__ZNSs6appendEPKcm, __ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base, ...),
// and calls go to /usr/lib/libstdc++.6.dylib.
//
// Only what the tests need is declared. Data members are public so tests can
// check the layout directly.
#pragma once

// The _Rep statics are defined (instantiated) inside libstdc++ only.
#pragma clang diagnostic ignored "-Wundefined-var-template"

namespace std {
typedef unsigned long size_t;
typedef long streamsize;

// --- Exceptions (provided by libc++abi, re-exported by libstdc++) -----------
class exception {
public:
    exception() throw() {}
    virtual ~exception() throw();
    virtual const char *what() const throw();
};
class bad_alloc : public exception {
public:
    virtual ~bad_alloc() throw();
};
void __throw_bad_alloc() __attribute__((noreturn));
void __throw_length_error(const char *) __attribute__((noreturn));
bool uncaught_exception() throw();
struct nothrow_t {};
extern const nothrow_t nothrow;

// --- COW basic_string -----------------------------------------------------
// The object is one pointer to the characters; a _Rep header sits right before
// them. _M_refcount is 0 for a sole owner, -1 for a leaked (unshareable) rep.
template <typename C> struct char_traits;
template <typename T> class allocator {
public:
    allocator() throw() {}
    allocator(const allocator &) throw() {}
    ~allocator() throw() {}
};

template <typename C, typename Tr = char_traits<C>, typename A = allocator<C> >
class basic_string {
public:
    struct _Rep {
        size_t _M_length;
        size_t _M_capacity;
        int _M_refcount;
        static size_t _S_empty_rep_storage[];
        static const C _S_terminal;
        void _M_destroy(const A &) throw();
    };

    basic_string(const C *s, const A &a = A());
    basic_string(const basic_string &);
    ~basic_string();

    basic_string &append(const C *s, size_t n);
    basic_string &append(const basic_string &);
    basic_string &append(size_t n, C c);
    basic_string &assign(const C *s, size_t n);
    basic_string &assign(const basic_string &);
    basic_string &replace(size_t pos, size_t n1, const C *s, size_t n2);
    void reserve(size_t n = 0);
    void push_back(C c);
    size_t find_first_not_of(const C *s, size_t pos, size_t n) const;
    void _M_leak_hard();
    void _M_mutate(size_t pos, size_t len1, size_t len2);

    _Rep *rep() const { return reinterpret_cast<_Rep *>(_M_p) - 1; }
    const C *data() const { return _M_p; }
    size_t size() const { return rep()->_M_length; }

    C *_M_p;
};
typedef basic_string<char> string;
typedef basic_string<wchar_t> wstring;

// --- Red-black tree base (std::map / std::set) ----------------------------
enum _Rb_tree_color { _S_red = false, _S_black = true };
struct _Rb_tree_node_base {
    _Rb_tree_color _M_color;
    _Rb_tree_node_base *_M_parent;
    _Rb_tree_node_base *_M_left;
    _Rb_tree_node_base *_M_right;
};
_Rb_tree_node_base *_Rb_tree_increment(_Rb_tree_node_base *) throw();
const _Rb_tree_node_base *_Rb_tree_increment(const _Rb_tree_node_base *) throw();
_Rb_tree_node_base *_Rb_tree_decrement(_Rb_tree_node_base *) throw();
void _Rb_tree_insert_and_rebalance(const bool insert_left, _Rb_tree_node_base *x,
                                   _Rb_tree_node_base *p, _Rb_tree_node_base &header) throw();
_Rb_tree_node_base *_Rb_tree_rebalance_for_erase(_Rb_tree_node_base *z,
                                                 _Rb_tree_node_base &header) throw();

// --- Doubly linked list base (std::list) ----------------------------------
struct _List_node_base {
    _List_node_base *_M_next;
    _List_node_base *_M_prev;
    static void swap(_List_node_base &x, _List_node_base &y) throw();
    void transfer(_List_node_base *first, _List_node_base *last) throw();
    void hook(_List_node_base *position) throw();
    void unhook() throw();
};

// --- iostreams ------------------------------------------------------------
// Objects are treated as opaque storage of the GNU size; the tests only call
// exported members and check fields at known offsets.
enum _Ios_Openmode { _S_app = 1, _S_ate = 2, _S_bin = 4, _S_in = 8, _S_out = 16, _S_trunc = 32,
                     _S_ios_openmode_end = 1L << 16 };
enum _Ios_Iostate { _S_goodbit = 0, _S_badbit = 1, _S_eofbit = 2, _S_failbit = 4,
                    _S_ios_iostate_end = 1L << 16 };

class ios_base {
public:
    class Init {
    public:
        Init();
        ~Init();
    };
};

class locale {
public:
    locale() throw();
    ~locale() throw();
    void *_M_impl;
};

template <typename C, typename Tr = char_traits<C> > class basic_streambuf;

template <typename C, typename Tr = char_traits<C> > class basic_ios {
public:
    C widen(char c) const;
    void clear(_Ios_Iostate state = _S_goodbit);
};

template <typename C, typename Tr = char_traits<C> > class basic_ostream {
public:
    basic_ostream &put(C c);
    basic_ostream &write(const C *s, streamsize n);
    template <typename V> basic_ostream &_M_insert(V v);
};
typedef basic_ostream<char> ostream;
template <typename C, typename Tr>
basic_ostream<C, Tr> &__ostream_insert(basic_ostream<C, Tr> &, const C *, streamsize);

template <typename C, typename Tr = char_traits<C> > class basic_istream {
public:
    template <typename V> basic_istream &_M_extract(V &v);
};
typedef basic_istream<char> istream;

template <typename C, typename Tr = char_traits<C>, typename A = allocator<C> >
class basic_stringbuf {
public:
    basic_string<C, Tr, A> str() const;
};

template <typename C, typename Tr = char_traits<C>, typename A = allocator<C> >
class basic_ostringstream {
public:
    explicit basic_ostringstream(_Ios_Openmode mode = _S_out);
    ~basic_ostringstream();
    alignas(16) unsigned char _M_storage[512];  // real object is 352 bytes
};

template <typename C, typename Tr = char_traits<C>, typename A = allocator<C> >
class basic_istringstream {
public:
    explicit basic_istringstream(const basic_string<C, Tr, A> &s, _Ios_Openmode mode = _S_in);
    ~basic_istringstream();
    alignas(16) unsigned char _M_storage[512];
};
}  // namespace std

void *operator new(std::size_t, const std::nothrow_t &) throw();
void operator delete(void *, const std::nothrow_t &) throw();

// GNU 4.2.1 LP64 layout constants the game's inlined code bakes in.
namespace gnu {
const unsigned long kRepHeader = 24;           // sizeof(_Rep_base)
const unsigned long kStreambufSize = 64;       // vptr, 6 buffer ptrs, locale
const unsigned long kStringbufSize = 80;       // + _M_mode, _M_string
const unsigned long kIosBaseSize = 216;
const unsigned long kBasicIosSize = 264;       // + tie, fill, streambuf, 3 facet caches
const unsigned long kOstringstreamSize = 352;  // ostream vptr + stringbuf + virtual basic_ios
const unsigned long kIosStreambufOffset = 232; // basic_ios::_M_streambuf
const unsigned long kIosPrecisionOffset = 8;
const unsigned long kIosWidthOffset = 16;
const unsigned long kIosFlagsOffset = 24;
const unsigned long kIosStateOffset = 32;      // _M_streambuf_state
}  // namespace gnu
