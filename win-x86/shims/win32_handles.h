// Kernel object handles of the Win32 seam (win32_ksync.c): files, find searches, file mappings,
// events, mutexes, semaphores, waitable timers and threads share one table, as on Windows, so
// CloseHandle, DuplicateHandle and the waits take any of them.
//
// A handle is 0x1000 + 4 * index. All waitable state is guarded by one process-wide lock
// (w32_hlock); a wait on several objects of different kinds is a re-check loop, woken by each
// change of an object it waits for (w32_hwake_obj).
#ifndef WIN32_HANDLES_H
#define WIN32_HANDLES_H

#include <pthread.h>
#include <stdint.h>

#include "win32_shims.h"

enum { H_FREE, H_FILE, H_FIND, H_MAPPING, H_EVENT, H_MUTEX, H_SEM, H_TIMER, H_THREAD };

typedef struct w32_obj {
  int type;
  int refs;            // handles open on it (DuplicateHandle, a named object opened again), waits on it
  char *name;          // a named event / mutex / semaphore / timer, else 0
  union {
    struct { int fd; int console; int dir; char *path; uint32_t access; } file;
    struct { void *dir, *dir2; char *hostdir, *hostdir2, *over; char *pattern; } find;  // 2: the game folder's, after an overlay's
    struct { int fd; uint32_t size; } map;
    struct { int manual, signaled; } ev;
    struct { uint32_t owner; int count; } mu;          // owner: guest thread id
    struct { int32_t count, max; } sem;
    struct { int manual, signaled, armed; uint64_t due_ns; uint32_t period_ms; } tm;
    struct { int done, suspended, prio, prio_set; uint32_t code, tid; pthread_t pt; } th;
  } u;
} w32_obj;

extern pthread_mutex_t w32_hlock;
extern pthread_cond_t w32_hwake;

// Under w32_hlock, after a change of o's state: wake the threads waiting for o (win32_ksync.c).
void w32_hwake_obj(w32_obj *o);
extern unsigned long w32_wait_wakeups;  // returns from a wait's sleep, under w32_hlock (tests)

// A new handle on a new object of this type (zeroed); 0 when the table is full. The object is
// returned in *o.
uint32_t w32_handle_new(int type, w32_obj **o);
// The object behind a handle (of `type`, or any when type < 0), 0 when it is not one.
w32_obj *w32_handle_get(uint32_t h, int type);
// A second handle on the object behind h.
uint32_t w32_handle_dup(uint32_t h);
// CloseHandle: the handle's slot freed, its object too with its last reference; 0 when h is no
// handle.
int w32_handle_close(uint32_t h);

// The file descriptor of a file handle (the console's for the standard handles), -1 when h is not
// a file.
int w32_file_fd(uint32_t h);

#endif
