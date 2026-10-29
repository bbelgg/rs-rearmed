// Per-frame GL call tracer (RS_GL_TRACE_DIR, see rsgl.c). The wrappers in
// rsgl.c call rs_tr_call() for every GL function when rs_tr_on is set.
#pragma once
#include <stdint.h>
#include <string.h>

extern int rs_tr_on;  // RS_GL_TRACE_DIR set: shadow state is kept, frames may be recorded

struct rs_targ {
    uint64_t u;      // raw bits of the argument (low bytes)
    uint8_t kind;    // __builtin_classify_type: 1 int, 5 pointer, 8 real
    uint8_t sz;
};

// a[0] is a dummy; a[1..n] are the arguments, in call order.
void rs_tr_call(int idx, const char *params, const struct rs_targ *a, uint64_t ret);
// real: the backend's implementation of every wrapped function by RS_GL_IDX_*.
// sys_gl: the backend is the system framework (else it is never called directly).
void rs_tr_init(void *const *real, const char *const *names, int sys_gl);
// Called before flushBuffer. snap(ctx, path) saves the back buffer as a PNG.
void rs_tr_frame_end(void *ctx, unsigned long frame, void (*snap)(void *, const char *));
// rsgl.c: hand a GL error found by the tracer back to the game's glGetError.
void rs_tr_stash_error(unsigned e);

#define RS_TR_A(x) ({ __typeof__(x) v_ = (x); struct rs_targ t_ = {0};                      \
    memcpy(&t_.u, &v_, sizeof v_ < 8 ? sizeof v_ : 8);                                      \
    t_.kind = (uint8_t)__builtin_classify_type(v_); t_.sz = (uint8_t)sizeof v_; t_; })
#define RS_TR_G(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,N,...) N
#define RS_TR_E1(f,a) f(a)
#define RS_TR_E2(f,a,...) f(a), RS_TR_E1(f,__VA_ARGS__)
#define RS_TR_E3(f,a,...) f(a), RS_TR_E2(f,__VA_ARGS__)
#define RS_TR_E4(f,a,...) f(a), RS_TR_E3(f,__VA_ARGS__)
#define RS_TR_E5(f,a,...) f(a), RS_TR_E4(f,__VA_ARGS__)
#define RS_TR_E6(f,a,...) f(a), RS_TR_E5(f,__VA_ARGS__)
#define RS_TR_E7(f,a,...) f(a), RS_TR_E6(f,__VA_ARGS__)
#define RS_TR_E8(f,a,...) f(a), RS_TR_E7(f,__VA_ARGS__)
#define RS_TR_E9(f,a,...) f(a), RS_TR_E8(f,__VA_ARGS__)
#define RS_TR_E10(f,a,...) f(a), RS_TR_E9(f,__VA_ARGS__)
#define RS_TR_E11(f,a,...) f(a), RS_TR_E10(f,__VA_ARGS__)
#define RS_TR_E12(f,a,...) f(a), RS_TR_E11(f,__VA_ARGS__)
#define RS_TR_MAP(f, ...) RS_TR_G(__VA_ARGS__, RS_TR_E12, RS_TR_E11, RS_TR_E10, RS_TR_E9, \
    RS_TR_E8, RS_TR_E7, RS_TR_E6, RS_TR_E5, RS_TR_E4, RS_TR_E3, RS_TR_E2, RS_TR_E1, x)(f, __VA_ARGS__)
// RS_TR_PACK(a, b) -> compound literal {dummy, a, b}; RS_TR_PACK() -> {dummy}
#define RS_TR_PACK(...) ((const struct rs_targ[]){ {0, 0, 0} __VA_OPT__(, RS_TR_MAP(RS_TR_A, __VA_ARGS__)) })
