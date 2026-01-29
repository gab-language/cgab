#ifndef _CTHREADS_H_
#define _CTHREADS_H_

/*
 * This is the original license notice.
 * Giving credit to the original authors who did 99 percent of the orginal work.
 */

/*
Copyright (c) 2012 Marcus Geelnard
Copyright (c) 2013-2016 Evan Nemerson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Platform selection code used in cthreads.h API
//=============================================================================

/* Platform selection */
#if !defined(_CTHREADS_PLATFORM_SELECTION_)
#if defined(_WIN32) || defined(__WIN32__) || defined(__WINDOWS__)
#define _CTHREADS_WIN32_
#else
#define _CTHREADS_POSIX_
#endif
#define _CTHREADS_PLATFORM_SELECTION_
#endif

/* POSIX-specific funtionality */
#if defined(_CTHREADS_POSIX_)
#undef _FEATURES_H
#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if !defined(_POSIX_C_SOURCE) || ((_POSIX_C_SOURCE - 0) < 199309L)
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#if !defined(_XOPEN_SOURCE) || ((_XOPEN_SOURCE - 0) < 500)
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif
#define _XPG6
#endif

/* Standard Includes */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

/* Platform-specific includes */
#if defined(_CTHREADS_POSIX_)
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#elif defined(_CTHREADS_WIN32_)
#include <process.h>
#include <sys/timeb.h>
#include <windows.h>
#endif

/* Compiler-specific information */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define CTHREADS_NORETURN _Noreturn
#elif defined(__GNUC__)
#define CTHREADS_NORETURN __attribute__((__noreturn__))
#elif defined(__CLANG__) || defined(__clang__)
#define CTHREADS_NORETURN __attribute__((__noreturn__))
#else
#define CTHREADS_NORETURN
#endif

/* Thread local storage keyword. */
#if !(defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201102L)) &&           \
    !defined(_Thread_local)
#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__CLANG__)
#define _Thread_local __thread
#else
#define _Thread_local __declspec(thread)
#endif
#elif defined(__GNUC__) && defined(__GNUC_MINOR__) &&                          \
    (((__GNUC__ << 8) | __GNUC_MINOR__) < ((4 << 8) | 9))
#define _Thread_local __thread
#endif

/* Destructor Macros */
#if defined(_CTHREADS_WIN32_)
#define TSS_DESTRUCTOR_ITERATIONS 4
#else
#define TSS_DESTRUCTOR_ITERATIONS PTHREAD_DESTRUCTOR_ITERATIONS
#endif

//=============================================================================
// Enums used in threads.h
//=============================================================================

/* Thread exit codes and error codes  */
enum {
  thrd_success = 0, // The requested operation succeeded
  thrd_busy = 1,  // The requested operation failed because a tesource requested
                  // by a test and return function is already in use
  thrd_error = 2, // The requested operation failed
  thrd_nomem = 3, // The requested operation failed because it was unable to
                  // allocate memory
  thrd_timedout = 4 // The time specified in the call was reached without
                    // acquiring the requested resource
};

/* Mutex codes  */
enum { mtx_plain = 0, mtx_recursive = 1, mtx_timed = 2 };

//=============================================================================
// Condition variable macros
//=============================================================================

// TODO: Find an alternative to this
#if defined(_CTHREADS_WIN32_)
#define _CONDITION_EVENT_ONE 0
#define _CONDITION_EVENT_ALL 1
#endif

//=============================================================================
// Types, wrappers, and definitions used in cthreads.h
//=============================================================================

/* Mutex type */
#if defined(_CTHREADS_WIN32_)
typedef struct {
  union {
    CRITICAL_SECTION cs; // Critical section handle (used for non-timed mutexes)
    HANDLE mut;          // Mutex handle (used for timed mutex)
  } mHandle;             // Mutex handle
  int32_t mAlreadyLocked; // TRUE if the mutex is already locked
  int32_t mRecursive;     // TRUE if the mutex is recursive
  int32_t mTimed;         // TRUE if the mutex is timed
} mtx_t;
#else
typedef pthread_mutex_t mtx_t;
#endif

/* Condition variable type */
#if defined(_CTHREADS_WIN32_)
typedef struct {
  HANDLE mEvents[2];                  // Signal and broadcast event HANDLEs.
  uint32_t mWaitersCount;             // Count of the number of waiters.
  CRITICAL_SECTION mWaitersCountLock; // Serialize access to mWaitersCount.
} cnd_t;
#else
typedef pthread_cond_t cnd_t;
#endif

/* Thread type */
#if defined(_CTHREADS_WIN32_)
typedef HANDLE thrd_t;
#else
typedef pthread_t thrd_t;
#endif

/* Thread start function. */
typedef int (*thrd_start_t)(void *);

/* Information to pass to the new thread (what to run). */
typedef struct {
  thrd_start_t mFunction; // Pointer to the function to be executed.
  void *mArg;             // Function argument for the thread function.
} thread_start_info;

/* Thread local storage type */
#if defined(_CTHREADS_WIN32_)
typedef DWORD tss_t;
#else
typedef pthread_key_t tss_t;
#endif

/* Destructor function for a thread-specific storage. */
typedef void (*tss_dtor_t)(void *val);

/* Win32 thread-specific storage specifications. Used internally only in
 * cthreads.h. */
#if defined(_CTHREADS_WIN32_)
struct cthreads_tss_data_win32_t {
  void *value;
  tss_t key;
  struct cthreads_tss_data_win32_t *next;
};
static tss_dtor_t cthreads_tss_destructors[1088] = {
    NULL,
};
static _Thread_local struct cthreads_tss_data_win32_t *cthreads_tss_head = NULL;
static _Thread_local struct cthreads_tss_data_win32_t *cthreads_tss_tail = NULL;
#endif /* defined(_CTHREADS_WIN32_) */

/* Once flag */
#if defined(_CTHREADS_WIN32_)
typedef struct {
  LONG volatile status;
  CRITICAL_SECTION lock;
} once_flag;
#define ONCE_FLAG_INIT                                                         \
  {                                                                            \
      0,                                                                       \
  }
#else
#define once_flag pthread_once_t
#define ONCE_FLAG_INIT PTHREAD_ONCE_INIT
#endif

//=============================================================================
// Mutex Functions Declarations
//=============================================================================

/* Create a mutex object. */
static int mtx_init(mtx_t *mtx, int type);
/* Releases any currently used mutex resources. */
static void mtx_destroy(mtx_t *mtx);
/* Locks a given mutex. */
static int mtx_lock(mtx_t *mtx);
/* Block the current thread until the mutex pointed to by mtx is unlocked
 * or time pointed to by ts is reached. In case the mutex is unlocked,
 * the current thread will not be blocked. */
static int mtx_timedlock(mtx_t *__restrict mtx,
                         const struct timespec *__restrict ts);
/* Attempts to lock a given mutex. */
static int mtx_trylock(mtx_t *mtx);
/* Unlock the mutex pointed to by mtx. It may potentially awaken other
   threads waiting on this mutex.  */
static int mtx_unlock(mtx_t *mtx);

//=============================================================================
// Thread Functions Declarations
//=============================================================================

/* Create a new thread. */
static int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
/* Get the ID of the calling thread. */
static thrd_t thrd_current(void);
/* Automatically clean up threads when they exit. */
static int thrd_detach(thrd_t thr);
/* Compare two thread descriptors for equality. */
static int thrd_equal(thrd_t thr0, thrd_t thr1);
/* Stop and exit this thread. */
static CTHREADS_NORETURN void thrd_exit(int res);
/* Wait for a thread to exit. */
static int thrd_join(thrd_t thr, int *res);
/* Sleep for a specific number of seconds and nanoseconds. */
static int thrd_sleep(const struct timespec *duration,
                      struct timespec *remaining);
/* Yield execution to another thread. */
static void thrd_yield(void);

//=============================================================================
// Condition Variable Functions Declarations
//=============================================================================

/* Create a condition variable object. */
static int cnd_init(cnd_t *cond);
/* Free up resources from a condition variable. */
static void cnd_destroy(cnd_t *cond);
/* Wake up a thread waiting on a condition variable. */
static int cnd_signal(cnd_t *cond);
/* Wake up all threads waiting on a condition variable. */
static int cnd_broadcast(cnd_t *cond);
/* Wait for a signal on a condition variable. */
static int cnd_wait(cnd_t *cond, mtx_t *mtx);
/* Wait on a condition variable with a timeout. */
static int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);

#if defined(_CTHREADS_WIN32_)
/* NOTE: This version of the cnd_timedwait() function is only for internal use.
 */
static int cnd_timedwait_win32(cnd_t *cond, mtx_t *mtx, DWORD timeout);
#endif

//=============================================================================
// Thread-Specific Storage Functions Declarations
//=============================================================================

/* Create new thread-specific storage. */
static int tss_create(tss_t *key, tss_dtor_t dtor);
/* Delete a thread-specific storage. */
static void tss_delete(tss_t key);
/* Get the value for a thread-specific storage. */
static void *tss_get(tss_t key);
/* Set the value for a thread-specific storage. */
static int tss_set(tss_t key, void *val);

#if defined(_CTHREADS_WIN32_)
/* NOTE: This function is for internal use only. */
static void cthreads_tss_cleanup_win32(void);
/* NOTE: Only used internally. */
static void NTAPI cthreads_tss_callback_win32(PVOID h, DWORD dwReason,
                                              PVOID pv);
#endif

//=============================================================================
// Call Once Function Declaration
//=============================================================================

/* Call a function one time no matter how many threads try. */
#if defined(_CTHREADS_WIN32_)
static void call_once(once_flag *flag, void (*func)(void));
#else
#define call_once(flag, func) pthread_once(flag, func)
#endif

//=============================================================================
// Mutex Functions Implementations
//=============================================================================

/* Create a mutex object. */
int mtx_init(mtx_t *mtx, int type) {
#if defined(_CTHREADS_WIN32_)
  mtx->mAlreadyLocked = FALSE;
  mtx->mRecursive = type & mtx_recursive;
  mtx->mTimed = type & mtx_timed;
  if (!mtx->mTimed) {
    InitializeCriticalSection(&(mtx->mHandle.cs));
  } else {
    mtx->mHandle.mut = CreateMutex(NULL, FALSE, NULL);
    if (mtx->mHandle.mut == NULL) {
      return thrd_error;
    }
  }
  return thrd_success;
#else
  int ret;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  if (type & mtx_recursive) {
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  }
  ret = pthread_mutex_init(mtx, &attr);
  pthread_mutexattr_destroy(&attr);
  return ret == 0 ? thrd_success : thrd_error;
#endif
}

/* Releases any currently used mutex resources. */
void mtx_destroy(mtx_t *mtx) {
#if defined(_CTHREADS_WIN32_)
  if (!mtx->mTimed) {
    DeleteCriticalSection(&(mtx->mHandle.cs));
  } else {
    CloseHandle(mtx->mHandle.mut);
  }
#else
  pthread_mutex_destroy(mtx);
#endif
}

/* Locks a given mutex. */
int mtx_lock(mtx_t *mtx) {
#if defined(_CTHREADS_WIN32_)
  if (!mtx->mTimed) {
    EnterCriticalSection(&(mtx->mHandle.cs));
  } else {
    switch (WaitForSingleObject(mtx->mHandle.mut, INFINITE)) {
    case WAIT_OBJECT_0:
      break;
    case WAIT_ABANDONED:
    default:
      return thrd_error;
    }
  }
  if (!mtx->mRecursive) {
    while (mtx->mAlreadyLocked)
      Sleep(1); /* Simulate deadlock... */
    mtx->mAlreadyLocked = TRUE;
  }
  return thrd_success;
#else
  return pthread_mutex_lock(mtx) == 0 ? thrd_success : thrd_error;
#endif
}

/* Block the current thread until the mutex pointed to by mtx is unlocked
   or time pointed to by ts is reached. In case the mutex is unlocked,
   the current thread will not be blocked. */
int mtx_timedlock(mtx_t *__restrict mtx, const struct timespec *__restrict ts) {
#if defined(_CTHREADS_WIN32_)
  struct timespec current_ts;
  DWORD timeoutMs;
  if (!mtx->mTimed) {
    return thrd_error;
  }
  timespec_get(&current_ts, TIME_UTC);
  if ((current_ts.tv_sec > ts->tv_sec) ||
      ((current_ts.tv_sec == ts->tv_sec) &&
       (current_ts.tv_nsec >= ts->tv_nsec))) {
    timeoutMs = 0;
  } else {
    timeoutMs = (DWORD)(ts->tv_sec - current_ts.tv_sec) * 1000;
    timeoutMs += (ts->tv_nsec - current_ts.tv_nsec) / 1000000;
    timeoutMs += 1;
  }
  /* TODO: the timeout for WaitForSingleObject doesn't include time
     while the computer is asleep. */
  switch (WaitForSingleObject(mtx->mHandle.mut, timeoutMs)) {
  case WAIT_OBJECT_0:
    break;
  case WAIT_TIMEOUT:
    return thrd_timedout;
  case WAIT_ABANDONED:
  default:
    return thrd_error;
  }
  if (!mtx->mRecursive) {
    while (mtx->mAlreadyLocked)
      Sleep(1); /* Simulate deadlock... */
    mtx->mAlreadyLocked = TRUE;
  }
  return thrd_success;
#elif defined(_POSIX_TIMEOUTS) && (_POSIX_TIMEOUTS >= 200112L) &&              \
    defined(_POSIX_THREADS) && (_POSIX_THREADS >= 200112L)
  switch (pthread_mutex_timedlock(mtx, ts)) {
  case 0:
    return thrd_success;
  case ETIMEDOUT:
    return thrd_timedout;
  default:
    return thrd_error;
  }
#else
  int rc;
  struct timespec cur, dur;
  /* Try to acquire the lock and, if we fail, sleep for 5ms. */
  while ((rc = pthread_mutex_trylock(mtx)) == EBUSY) {
    timespec_get(&cur, TIME_UTC);
    if ((cur.tv_sec > ts->tv_sec) ||
        ((cur.tv_sec == ts->tv_sec) && (cur.tv_nsec >= ts->tv_nsec))) {
      break;
    }
    dur.tv_sec = ts->tv_sec - cur.tv_sec;
    dur.tv_nsec = ts->tv_nsec - cur.tv_nsec;
    if (dur.tv_nsec < 0) {
      dur.tv_sec--;
      dur.tv_nsec += 1000000000;
    }
    if ((dur.tv_sec != 0) || (dur.tv_nsec > 5000000)) {
      dur.tv_sec = 0;
      dur.tv_nsec = 5000000;
    }
    nanosleep(&dur, NULL);
  }
  switch (rc) {
  case 0:
    return thrd_success;
  case ETIMEDOUT:
  case EBUSY:
    return thrd_timedout;
  default:
    return thrd_error;
  }
#endif
}

#if defined(_CTHREADS_WIN32_)
static int mtx_trylock_acquire_win32(mtx_t *mtx) {
  int ret = 0;
  if (!mtx->mTimed) {
    ret =
        TryEnterCriticalSection(&(mtx->mHandle.cs)) ? thrd_success : thrd_busy;
  } else {
    ret = (WaitForSingleObject(mtx->mHandle.mut, 0) == WAIT_OBJECT_0)
              ? thrd_success
              : thrd_busy;
  }
  return ret;
}
#endif

/* Attempts to lock a given mutex. */
int mtx_trylock(mtx_t *mtx) {
#if defined(_CTHREADS_WIN32_)
  int ret = mtx_trylock_acquire_win32(mtx);
  if ((!mtx->mRecursive) && (ret == thrd_success)) {
    if (mtx->mAlreadyLocked) {
      LeaveCriticalSection(&(mtx->mHandle.cs));
      ret = thrd_busy;
    } else {
      mtx->mAlreadyLocked = TRUE;
    }
  }
  return ret;
#else
  return (pthread_mutex_trylock(mtx) == 0) ? thrd_success : thrd_busy;
#endif
}

/* Unlock the mutex pointed to by mtx. It may potentially awaken other
   threads waiting on this mutex.  */
int mtx_unlock(mtx_t *mtx) {
#if defined(_CTHREADS_WIN32_)
  mtx->mAlreadyLocked = FALSE;
  if (!mtx->mTimed) {
    LeaveCriticalSection(&(mtx->mHandle.cs));
  } else {
    if (!ReleaseMutex(mtx->mHandle.mut)) {
      return thrd_error;
    }
  }
  return thrd_success;
#else
  return pthread_mutex_unlock(mtx) == 0 ? thrd_success : thrd_error;
  ;
#endif
}

//=============================================================================
// Condition Variable Functions Implementations
//=============================================================================

/* Create a condition variable object. */
int cnd_init(cnd_t *cond) {
#if defined(_CTHREADS_WIN32_)
  cond->mWaitersCount = 0;
  /* Init critical section */
  InitializeCriticalSection(&cond->mWaitersCountLock);
  /* Init events */
  cond->mEvents[_CONDITION_EVENT_ONE] = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (cond->mEvents[_CONDITION_EVENT_ONE] == NULL) {
    cond->mEvents[_CONDITION_EVENT_ALL] = NULL;
    return thrd_error;
  }
  cond->mEvents[_CONDITION_EVENT_ALL] = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (cond->mEvents[_CONDITION_EVENT_ALL] == NULL) {
    CloseHandle(cond->mEvents[_CONDITION_EVENT_ONE]);
    cond->mEvents[_CONDITION_EVENT_ONE] = NULL;
    return thrd_error;
  }
  return thrd_success;
#else
  return pthread_cond_init(cond, NULL) == 0 ? thrd_success : thrd_error;
#endif
}

/* Free up resources from a condition variable. */
void cnd_destroy(cnd_t *cond) {
#if defined(_CTHREADS_WIN32_)
  if (cond->mEvents[_CONDITION_EVENT_ONE] != NULL) {
    CloseHandle(cond->mEvents[_CONDITION_EVENT_ONE]);
  }
  if (cond->mEvents[_CONDITION_EVENT_ALL] != NULL) {
    CloseHandle(cond->mEvents[_CONDITION_EVENT_ALL]);
  }
  DeleteCriticalSection(&cond->mWaitersCountLock);
#else
  pthread_cond_destroy(cond);
#endif
}

/* Wake up a thread waiting on a condition variable. */
int cnd_signal(cnd_t *cond) {
#if defined(_CTHREADS_WIN32_)
  int haveWaiters;
  /* Are there any waiters? */
  EnterCriticalSection(&cond->mWaitersCountLock);
  haveWaiters = (cond->mWaitersCount > 0);
  LeaveCriticalSection(&cond->mWaitersCountLock);
  /* If we have any waiting threads, send them a signal */
  if (haveWaiters) {
    if (SetEvent(cond->mEvents[_CONDITION_EVENT_ONE]) == 0) {
      return thrd_error;
    }
  }
  return thrd_success;
#else
  return pthread_cond_signal(cond) == 0 ? thrd_success : thrd_error;
#endif
}

/* Wake up all threads waiting on a condition variable. */
int cnd_broadcast(cnd_t *cond) {
#if defined(_CTHREADS_WIN32_)
  int haveWaiters;
  /* Are there any waiters? */
  EnterCriticalSection(&cond->mWaitersCountLock);
  haveWaiters = (cond->mWaitersCount > 0);
  LeaveCriticalSection(&cond->mWaitersCountLock);
  /* If we have any waiting threads, send them a signal */
  if (haveWaiters) {
    if (SetEvent(cond->mEvents[_CONDITION_EVENT_ALL]) == 0) {
      return thrd_error;
    }
  }
  return thrd_success;
#else
  return pthread_cond_broadcast(cond) == 0 ? thrd_success : thrd_error;
#endif
}

#if defined(_CTHREADS_WIN32_)
int cnd_timedwait_win32(cnd_t *cond, mtx_t *mtx, DWORD timeout) {
  DWORD result;
  int lastWaiter;
  /* Increment number of waiters */
  EnterCriticalSection(&cond->mWaitersCountLock);
  ++cond->mWaitersCount;
  LeaveCriticalSection(&cond->mWaitersCountLock);
  /* Release the mutex while waiting for the condition (will decrease
     the number of waiters when done)... */
  mtx_unlock(mtx);
  /* Wait for either event to become signaled due to cnd_signal() or
     cnd_broadcast() being called */
  result = WaitForMultipleObjects(2, cond->mEvents, FALSE, timeout);
  if (result == WAIT_TIMEOUT) {
    /* The mutex is locked again before the function returns, even if an error
     * occurred */
    mtx_lock(mtx);
    return thrd_timedout;
  } else if (result == WAIT_FAILED) {
    /* The mutex is locked again before the function returns, even if an error
     * occurred */
    mtx_lock(mtx);
    return thrd_error;
  }
  /* Check if we are the last waiter */
  EnterCriticalSection(&cond->mWaitersCountLock);
  --cond->mWaitersCount;
  lastWaiter = (result == (WAIT_OBJECT_0 + _CONDITION_EVENT_ALL)) &&
               (cond->mWaitersCount == 0);
  LeaveCriticalSection(&cond->mWaitersCountLock);
  /* If we are the last waiter to be notified to stop waiting, reset the event
   */
  if (lastWaiter) {
    if (ResetEvent(cond->mEvents[_CONDITION_EVENT_ALL]) == 0) {
      /* The mutex is locked again before the function returns, even if an error
       * occurred */
      mtx_lock(mtx);
      return thrd_error;
    }
  }
  /* Re-acquire the mutex */
  mtx_lock(mtx);
  return thrd_success;
}
#endif

/* Wait for a signal on a condition variable. */
int cnd_wait(cnd_t *cond, mtx_t *mtx) {
#if defined(_CTHREADS_WIN32_)
  return cnd_timedwait_win32(cond, mtx, INFINITE);
#else
  return pthread_cond_wait(cond, mtx) == 0 ? thrd_success : thrd_error;
#endif
}

/* Wait on a condition variable with a timeout. */
int cnd_timedwait(cnd_t *__restrict cond, mtx_t *__restrict mtx,
                  const struct timespec *__restrict ts) {
#if defined(_CTHREADS_WIN32_)
  struct timespec now;
  if (timespec_get(&now, TIME_UTC) == TIME_UTC) {
    unsigned long long nowInMilliseconds =
        now.tv_sec * 1000 + now.tv_nsec / 1000000;
    unsigned long long tsInMilliseconds =
        ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
    DWORD delta = (tsInMilliseconds > nowInMilliseconds)
                      ? (DWORD)(tsInMilliseconds - nowInMilliseconds)
                      : 0;
    return cnd_timedwait_win32(cond, mtx, delta);
  } else
    return thrd_error;
#else
  int ret;
  ret = pthread_cond_timedwait(cond, mtx, ts);
  if (ret == ETIMEDOUT) {
    return thrd_timedout;
  }
  return ret == 0 ? thrd_success : thrd_error;
#endif
}

//=============================================================================
// Win32-Specific TSS Functions (only for internal use)
//=============================================================================

#if defined(_CTHREADS_WIN32_)
void cthreads_tss_cleanup_win32(void) {
  struct cthreads_tss_data_win32_t *data;
  int iteration;
  unsigned int again = 1;
  void *value;
  for (iteration = 0; iteration < TSS_DESTRUCTOR_ITERATIONS && again > 0;
       iteration++) {
    again = 0;
    for (data = cthreads_tss_head; data != NULL; data = data->next) {
      if (data->value != NULL) {
        value = data->value;
        data->value = NULL;
        if (cthreads_tss_destructors[data->key] != NULL) {
          again = 1;
          cthreads_tss_destructors[data->key](value);
        }
      }
    }
  }
  while (cthreads_tss_head != NULL) {
    data = cthreads_tss_head->next;
    free(cthreads_tss_head);
    cthreads_tss_head = data;
  }
  cthreads_tss_head = NULL;
  cthreads_tss_tail = NULL;
}

void NTAPI cthreads_tss_callback_win32(PVOID h, DWORD dwReason, PVOID pv) {
  (void)h;
  (void)pv;
  if (cthreads_tss_head != NULL &&
      (dwReason == DLL_THREAD_DETACH || dwReason == DLL_PROCESS_DETACH)) {
    cthreads_tss_cleanup_win32();
  }
}

#if defined(_MSC_VER)
#ifdef _M_X64
#pragma const_seg(".CRT$XLB")
#else
#pragma data_seg(".CRT$XLB")
#endif
PIMAGE_TLS_CALLBACK static p_thread_callback = cthreads_tss_callback_win32;
#ifdef _M_X64
#pragma data_seg()
#else
#pragma const_seg()
#endif
#else
PIMAGE_TLS_CALLBACK static p_thread_callback
    __attribute__((section(".CRT$XLB"))) = cthreads_tss_callback_win32;
#endif

#endif // defined(_CTHREADS_WIN32_)

//=============================================================================
// Thread Functions Implementations
//=============================================================================

/* Thread wrapper function. */
#if defined(_CTHREADS_WIN32_)
static DWORD WINAPI thread_wrapper_function(LPVOID aArg)
#elif defined(_CTHREADS_POSIX_)
static void *thread_wrapper_function(void *aArg)
#endif
{
  thrd_start_t fun;
  void *arg;
  int res;
  /* Get thread startup information */
  thread_start_info *ti = (thread_start_info *)aArg;
  fun = ti->mFunction;
  arg = ti->mArg;
  /* The thread is responsible for freeing the startup information */
  free((void *)ti);
  /* Call the actual client thread function */
  res = fun(arg);
#if defined(_CTHREADS_WIN32_)
  if (cthreads_tss_head != NULL) {
    cthreads_tss_cleanup_win32();
  }
  return (DWORD)res;
#else
  return (void *)(intptr_t)res;
#endif
}

/* Create a new thread. */
int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
  /* Fill out the thread startup information (passed to the thread wrapper,
     which will eventually free it) */
  thread_start_info *ti =
      (thread_start_info *)malloc(sizeof(thread_start_info));
  if (ti == NULL) {
    return thrd_nomem;
  }
  ti->mFunction = func;
  ti->mArg = arg;
/* Create the thread */
#if defined(_CTHREADS_WIN32_)
  *thr = CreateThread(NULL, 0, thread_wrapper_function, (LPVOID)ti, 0, NULL);
#elif defined(_CTHREADS_POSIX_)
  if (pthread_create(thr, NULL, thread_wrapper_function, (void *)ti) != 0) {
    *thr = 0;
  }
#endif
  /* Did we fail to create the thread? */
  if (!*thr) {
    free(ti);
    return thrd_error;
  }
  return thrd_success;
}

/* Get the ID of the calling thread. */
thrd_t thrd_current(void) {
#if defined(_CTHREADS_WIN32_)
  return GetCurrentThread();
#else
  return pthread_self();
#endif
}

/* Automatically clean up threads when they exit. */
int thrd_detach(thrd_t thr) {
#if defined(_CTHREADS_WIN32_)
  /* https://stackoverflow.com/questions/12744324/how-to-detach-a-thread-on-windows-c#answer-12746081
   */
  return CloseHandle(thr) != 0 ? thrd_success : thrd_error;
#else
  return pthread_detach(thr) == 0 ? thrd_success : thrd_error;
#endif
}

/* Compare two thread descriptors for equality. */
int thrd_equal(thrd_t thr0, thrd_t thr1) {
#if defined(_CTHREADS_WIN32_)
  return GetThreadId(thr0) == GetThreadId(thr1);
#else
  return pthread_equal(thr0, thr1);
#endif
}

/* Stop and exit this thread. */
void thrd_exit(int res) {
#if defined(_CTHREADS_WIN32_)
  if (cthreads_tss_head != NULL) {
    cthreads_tss_cleanup_win32();
  }
  ExitThread((DWORD)res);
#else
  pthread_exit((void *)(intptr_t)res);
#endif
}

/* Wait for a thread to exit. */
int thrd_join(thrd_t thr, int *res) {
#if defined(_CTHREADS_WIN32_)
  DWORD dwRes;
  if (WaitForSingleObject(thr, INFINITE) == WAIT_FAILED) {
    return thrd_error;
  }
  if (res != NULL) {
    if (GetExitCodeThread(thr, &dwRes) != 0) {
      *res = (int)dwRes;
    } else {
      return thrd_error;
    }
  }
  CloseHandle(thr);
#elif defined(_CTHREADS_POSIX_)
  void *pres;
  if (pthread_join(thr, &pres) != 0) {
    return thrd_error;
  }
  if (res != NULL) {
    *res = (int)(intptr_t)pres;
  }
#endif
  return thrd_success;
}

/* Sleep for a specific number of seconds and nanoseconds. */
int thrd_sleep(const struct timespec *duration, struct timespec *remaining) {
#if !defined(_CTHREADS_WIN32_)
  int res = nanosleep(duration, remaining);
  if (res == 0) {
    return 0;
  } else if (errno == EINTR) {
    return -1;
  } else {
    return -2;
  }
#else
  struct timespec start;
  DWORD t;
  timespec_get(&start, TIME_UTC);
  t = SleepEx((DWORD)(duration->tv_sec * 1000 + duration->tv_nsec / 1000000 +
                      (((duration->tv_nsec % 1000000) == 0) ? 0 : 1)),
              TRUE);
  if (t == 0) {
    return 0;
  } else {
    if (remaining != NULL) {
      timespec_get(remaining, TIME_UTC);
      remaining->tv_sec -= start.tv_sec;
      remaining->tv_nsec -= start.tv_nsec;
      if (remaining->tv_nsec < 0) {
        remaining->tv_nsec += 1000000000;
        remaining->tv_sec -= 1;
      }
    }
    return (t == WAIT_IO_COMPLETION) ? -1 : -2;
  }
#endif
}

/* Yield execution to another thread. */
void thrd_yield(void) {
#if defined(_CTHREADS_WIN32_)
  Sleep(0);
#else
  sched_yield();
#endif
}

//=============================================================================
// Thread-Specific Storage Functions Implementations
//=============================================================================

/* Create new thread-specific storage. */
int tss_create(tss_t *key, tss_dtor_t dtor) {
#if defined(_CTHREADS_WIN32_)
  *key = TlsAlloc();
  if (*key == TLS_OUT_OF_INDEXES) {
    return thrd_error;
  }
  cthreads_tss_destructors[*key] = dtor;
#else
  if (pthread_key_create(key, dtor) != 0) {
    return thrd_error;
  }
#endif
  return thrd_success;
}

/* Delete a thread-specific storage. */
void tss_delete(tss_t key) {
#if defined(_CTHREADS_WIN32_)
  struct cthreads_tss_data_win32_t *data =
      (struct cthreads_tss_data_win32_t *)TlsGetValue(key);
  struct cthreads_tss_data_win32_t *prev = NULL;
  if (data != NULL) {
    if (data == cthreads_tss_head) {
      cthreads_tss_head = data->next;
    } else {
      prev = cthreads_tss_head;
      if (prev != NULL) {
        while (prev->next != data) {
          prev = prev->next;
        }
      }
    }
    if (data == cthreads_tss_tail) {
      cthreads_tss_tail = prev;
    }
    free(data);
  }
  cthreads_tss_destructors[key] = NULL;
  TlsFree(key);
#else
  pthread_key_delete(key);
#endif
}

/* Get the value for a thread-specific storage. */
void *tss_get(tss_t key) {
#if defined(_CTHREADS_WIN32_)
  struct cthreads_tss_data_win32_t *data =
      (struct cthreads_tss_data_win32_t *)TlsGetValue(key);
  if (data == NULL) {
    return NULL;
  }
  return data->value;
#else
  return pthread_getspecific(key);
#endif
}

/* Set the value for a thread-specific storage. */
int tss_set(tss_t key, void *val) {
#if defined(_CTHREADS_WIN32_)
  struct cthreads_tss_data_win32_t *data =
      (struct cthreads_tss_data_win32_t *)TlsGetValue(key);
  if (data == NULL) {
    data = (struct cthreads_tss_data_win32_t *)malloc(
        sizeof(struct cthreads_tss_data_win32_t));
    if (data == NULL) {
      return thrd_error;
    }
    data->value = NULL;
    data->key = key;
    data->next = NULL;
    if (cthreads_tss_tail != NULL) {
      cthreads_tss_tail->next = data;
    } else {
      cthreads_tss_tail = data;
    }

    if (cthreads_tss_head == NULL) {
      cthreads_tss_head = data;
    }
    if (!TlsSetValue(key, data)) {
      free(data);
      return thrd_error;
    }
  }
  data->value = val;
#else
  if (pthread_setspecific(key, val) != 0) {
    return thrd_error;
  }
#endif
  return thrd_success;
}

#if defined(_CTHREADS_WIN32_)
void call_once(once_flag *flag, void (*func)(void)) {
  /* The idea here is that we use a spin lock (via the
     InterlockedCompareExchange function) to restrict access to the
     critical section until we have initialized it, then we use the
     critical section to block until the callback has completed
     execution. */
  while (flag->status < 3) {
    switch (flag->status) {
    case 0:
      if (InterlockedCompareExchange(&(flag->status), 1, 0) == 0) {
        InitializeCriticalSection(&(flag->lock));
        EnterCriticalSection(&(flag->lock));
        flag->status = 2;
        func();
        flag->status = 3;
        LeaveCriticalSection(&(flag->lock));
        return;
      }
      break;
    case 1:
      break;
    case 2:
      EnterCriticalSection(&(flag->lock));
      LeaveCriticalSection(&(flag->lock));
      break;
    }
  }
}
#endif /* defined(_CTHREADS_WIN32_) */

#ifdef __cplusplus
}
#endif

#endif // _CTHREADS_H_
