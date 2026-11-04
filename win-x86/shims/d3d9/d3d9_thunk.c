// COM objects of the D3D9 layer (d3d9_thunk.h).
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9_thunk.h"

// --- objects -------------------------------------------------------------------------------
static d3d9_obj **objs;
static uint32_t nobjs, capobjs;
static uint32_t *free_idx;
static uint32_t nfree, capfree;
static pthread_mutex_t obj_mu = PTHREAD_MUTEX_INITIALIZER;

uint32_t d3d9_obj_publish(d3d9_obj *o, uint32_t vtable) {
    uint32_t g = w32_galloc(16);
    if (!g) return 0;
    pthread_mutex_lock(&obj_mu);
    uint32_t idx;
    if (nfree) {
        idx = free_idx[--nfree];
    } else {
        if (nobjs == capobjs) {
            capobjs = capobjs ? 2 * capobjs : 1024;
            objs = realloc(objs, capobjs * sizeof *objs);
        }
        idx = nobjs++;
    }
    objs[idx] = o;
    pthread_mutex_unlock(&obj_mu);
    o->guest = g;
    o->index = idx;
    o->ref = 1;
    d3d9_wr32(g, vtable);
    d3d9_wr32(g + 4, D3D9_OBJ_MAGIC);
    d3d9_wr32(g + 8, idx);
    return g;
}

d3d9_obj *d3d9_obj_get(uint32_t guest, int kind) {
    if (guest < 0x10000) return 0;
    if (d3d9_rd32(guest + 4) != D3D9_OBJ_MAGIC) return 0;
    uint32_t idx = d3d9_rd32(guest + 8);
    pthread_mutex_lock(&obj_mu);
    d3d9_obj *o = idx < nobjs ? objs[idx] : 0;
    pthread_mutex_unlock(&obj_mu);
    if (!o || o->guest != guest || (kind >= 0 && o->kind != kind)) return 0;
    return o;
}

static void obj_maybe_destroy(d3d9_obj *o) {
    if (__atomic_load_n(&o->ref, __ATOMIC_ACQUIRE) > 0 || __atomic_load_n(&o->iref, __ATOMIC_ACQUIRE) > 0)
        return;
    pthread_mutex_lock(&obj_mu);
    if (o->index < nobjs && objs[o->index] == o) {
        objs[o->index] = 0;
        if (nfree == capfree) {
            capfree = capfree ? 2 * capfree : 1024;
            free_idx = realloc(free_idx, capfree * sizeof *free_idx);
        }
        free_idx[nfree++] = o->index;
    } else {
        o = 0;  // already gone
    }
    pthread_mutex_unlock(&obj_mu);
    if (!o) return;
    uint32_t g = o->guest;
    d3d9_wr32(g + 4, 0);  // a stale guest pointer no longer resolves
    w32_gfree(g, 16);
    if (o->destroy) o->destroy(o);
}

uint32_t d3d9_obj_addref(d3d9_obj *o) {
    if (o->fwd) return d3d9_obj_addref(o->fwd);
    return (uint32_t)__atomic_add_fetch(&o->ref, 1, __ATOMIC_ACQ_REL); }
uint32_t d3d9_obj_release(d3d9_obj *o) {
    if (o->fwd) return d3d9_obj_release(o->fwd);
    int32_t r = __atomic_sub_fetch(&o->ref, 1, __ATOMIC_ACQ_REL);
    if (r < 0) {
        __atomic_store_n(&o->ref, 0, __ATOMIC_RELEASE);
        d3d9_log_once("d3d9: Release of an object with no references (kind %d)\n", o->kind);
        return 0;
    }
    if (r == 0) obj_maybe_destroy(o);
    return (uint32_t)r;
}
void d3d9_obj_ihold(d3d9_obj *o) {
    if (o && o->fwd) o = o->fwd;
    if (o) __atomic_add_fetch(&o->iref, 1, __ATOMIC_ACQ_REL);
}
void d3d9_obj_idrop(d3d9_obj *o) {
    if (o && o->fwd) o = o->fwd;
    if (o && __atomic_sub_fetch(&o->iref, 1, __ATOMIC_ACQ_REL) == 0) obj_maybe_destroy(o);
}

void d3d9_obj_kill(d3d9_obj *o) {
    pthread_mutex_lock(&obj_mu);
    int mine = o->index < nobjs && objs[o->index] == o;
    if (mine) {
        objs[o->index] = 0;
        if (nfree == capfree) {
            capfree = capfree ? 2 * capfree : 1024;
            free_idx = realloc(free_idx, capfree * sizeof *free_idx);
        }
        free_idx[nfree++] = o->index;
    }
    pthread_mutex_unlock(&obj_mu);
    if (!mine) return;
    d3d9_wr32(o->guest + 4, 0);
    w32_gfree(o->guest, 16);
}

// --- logging ---------------------------------------------------------------------------------
#define LOG_KEYS 512
static char *log_keys[LOG_KEYS];
static int nlog;
static pthread_mutex_t log_mu = PTHREAD_MUTEX_INITIALIZER;

void d3d9_log_once(const char *fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&log_mu);
    for (int k = 0; k < nlog; k++)
        if (!strcmp(log_keys[k], b)) {
            pthread_mutex_unlock(&log_mu);
            return;
        }
    if (nlog < LOG_KEYS) log_keys[nlog++] = strdup(b);
    pthread_mutex_unlock(&log_mu);
    fputs(b, stderr);
}
