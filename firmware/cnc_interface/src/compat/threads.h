#ifndef COMPAT_THREADS_H
#define COMPAT_THREADS_H

// This header provides a basic compatibility layer for C11 threads on platforms
// that don't support it natively, like macOS, by wrapping pthreads.

#if defined(__APPLE__) || defined(__MACH__)

#include <errno.h>  // For ETIMEDOUT
#include <pthread.h>
#include <stdlib.h>  // For calloc
#include <time.h>    // For timespec
#include <unistd.h>  // For usleep

// Polyfill for pthread_mutex_timedlock on macOS
static inline int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                                          const struct timespec *abs_timeout) {
  int ret;
  struct timespec current_time;

  // Loop until the timeout is reached or the lock is acquired
  while ((ret = pthread_mutex_trylock(mutex)) == EBUSY) {
    clock_gettime(CLOCK_REALTIME, &current_time);
    if (current_time.tv_sec > abs_timeout->tv_sec ||
        (current_time.tv_sec == abs_timeout->tv_sec &&
         current_time.tv_nsec >= abs_timeout->tv_nsec)) {
      return ETIMEDOUT;
    }
    // Sleep for a short duration to avoid consuming CPU
    usleep(1000);  // 1ms sleep
  }
  return ret;
}

// 1. Type definitions
typedef pthread_t thrd_t;
typedef pthread_mutex_t mtx_t;

// 2. Enum definitions
enum {
  thrd_success = 0,
  thrd_error = 1,
  thrd_timedout = 2,
  thrd_busy = 3,
  thrd_nomem = 4
};

enum { mtx_plain = 0, mtx_recursive = 1, mtx_timed = 2 };

// 3. Function prototypes / inline implementations

typedef int (*thrd_start_t)(void *);

// Helper struct to pass data to the pthread start routine
typedef struct {
  thrd_start_t start_routine;
  void *arg;
} _thrd_wrapper_arg;

// The actual start routine for pthread
static void *_thrd_wrapper_function(void *arg) {
  _thrd_wrapper_arg *wrapper_arg = (_thrd_wrapper_arg *)arg;
  int res = wrapper_arg->start_routine(wrapper_arg->arg);
  free(wrapper_arg);
  return (void *)(intptr_t)res;
}

static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
  _thrd_wrapper_arg *wrapper_arg =
      (_thrd_wrapper_arg *)calloc(1, sizeof(_thrd_wrapper_arg));
  if (wrapper_arg == NULL) {
    return thrd_nomem;
  }
  wrapper_arg->start_routine = func;
  wrapper_arg->arg = arg;

  if (pthread_create(thr, NULL, _thrd_wrapper_function, wrapper_arg) != 0) {
    free(wrapper_arg);
    return thrd_error;
  }
  return thrd_success;
}

static inline int mtx_init(mtx_t *mutex, int type) {
  if (!mutex) return thrd_error;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);

  if (type == mtx_recursive) {
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  } else {
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
  }

  int result = pthread_mutex_init(mutex, &attr);
  pthread_mutexattr_destroy(&attr);

  return result == 0 ? thrd_success : thrd_error;
}

static inline int mtx_lock(mtx_t *mutex) {
  return pthread_mutex_lock(mutex) == 0 ? thrd_success : thrd_error;
}

static inline int mtx_timedlock(mtx_t *mutex, const struct timespec *ts) {
  int ret = pthread_mutex_timedlock(mutex, ts);
  if (ret == 0) return thrd_success;
  return (ret == ETIMEDOUT) ? thrd_timedout : thrd_error;
}

static inline int mtx_unlock(mtx_t *mutex) {
  return pthread_mutex_unlock(mutex) == 0 ? thrd_success : thrd_error;
}

static inline void mtx_destroy(mtx_t *mutex) { pthread_mutex_destroy(mutex); }

#else
// For other platforms (like Linux or Windows with a modern GCC/Clang), just
// include the standard header.
#include <threads.h>
#endif  // __APPLE__

#endif  // COMPAT_THREADS_H
