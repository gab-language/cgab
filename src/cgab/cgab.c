/**
 *  MIT License
 *
 *  Copyright (c) 2023-2026 Teddy Randby
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

#include "engine.h"
#include "gab.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

// -- EG --

struct errdetails {
  const char *src_name, *tok_name, *msg_name;
  uint64_t token, row, col_begin, col_end, byte_begin, byte_end;
  enum gab_status status;
  int wkid;
};

uint64_t gab_eglen(struct gab_eg *eg) { return eg->len; }

gab_value *gab_egerrs(struct gab_eg *eg) {
  v_gab_value_thrd errs;
  v_gab_value_thrd_drain(&eg->err, &errs);

  if (!errs.len)
    return nullptr;

  v_gab_value_thrd_push(&errs, gab_nil);

  /* Just free the mutex, leave the pointer to be cleaned up by caller */
  mtx_destroy(&errs.mtx);
  gab_assert(errs.len > 0,
             "The array of errors shall have len > 0 in this codepath");
  gab_assert(
      errs.data != nullptr,
      "The array of errors returned shall not be null when errs.len > 0");
  return errs.data;
};

struct primitive {
  const char *name;
  union {
    gab_value val;
    enum gab_kind kind;
    const char *message;
  };
  gab_value primitive;
};

struct primitive all_primitives[] = {
    {
        .name = mGAB_TYPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_TYPE),
    },
};

struct primitive val_primitives[] = {
    {
        .name = mGAB_EQ,
        .val = gab_cundefined,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_EQ),
    },
    {
        .name = mGAB_CONS,
        .val = gab_cundefined,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CONS),
    },
};

struct primitive msg_primitives[] = {
    {
        .name = mGAB_MAKE,
        .message = tGAB_LIST,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LIST),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_FIBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_FIBER),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_RECORD),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_SHAPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SHAPE),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_CHANNEL,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CHANNEL),
    },
    {
        .name = mGAB_BND,
        .message = "false",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LND),
    },
    {
        .name = mGAB_BOR,
        .message = "false",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LOR),
    },
    {
        .name = mGAB_LIN,
        .message = "false",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LIN),
    },
    {
        .name = mGAB_BND,
        .message = "true",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LND),
    },
    {
        .name = mGAB_BOR,
        .message = "true",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LOR),
    },
    {
        .name = mGAB_LIN,
        .message = "true",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LIN),
    },
};

struct primitive kind_primitives[] = {
    {
        .name = mGAB_BIN,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BIN),
    },
    {
        .name = mGAB_BIN,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BIN),
    },
    {
        .name = mGAB_BOR,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BOR),
    },
    {
        .name = mGAB_BND,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BND),
    },
    {
        .name = mGAB_LSH,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LSH),
    },
    {
        .name = mGAB_RSH,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_RSH),
    },
    {
        .name = mGAB_ADD,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_ADD),
    },
    {
        .name = mGAB_SUB,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SUB),
    },
    {
        .name = mGAB_MUL,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_MUL),
    },
    {
        .name = mGAB_DIV,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_DIV),
    },
    {
        .name = mGAB_MOD,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_MOD),
    },
    {
        .name = mGAB_LT,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LT),
    },
    {
        .name = mGAB_LTE,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LTE),
    },
    {
        .name = mGAB_GT,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_GT),
    },
    {
        .name = mGAB_GTE,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_GTE),
    },
    {
        .name = mGAB_LT,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_STR_LT),
    },
    {
        .name = mGAB_LTE,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_STR_LTE),
    },
    {
        .name = mGAB_GT,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_STR_GT),
    },
    {
        .name = mGAB_GTE,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_STR_GTE),
    },
    {
        .name = mGAB_ADD,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CONCAT),
    },
    {
        .name = mGAB_MAKE,
        .kind = kGAB_SHAPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_MAKE_SHAPE),
    },
    {
        .name = mGAB_SPLATLIST,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATLIST),
    },
    {
        .name = mGAB_SPLATLIST,
        .kind = kGAB_SHAPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATSHAPE),
    },
    {
        .name = mGAB_SPLATDICT,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATDICT),
    },
    {
        .name = mGAB_CONS,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CONS_RECORD),
    },
    {
        .name = mGAB_USE,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_USE),
    },
    {
        .name = mGAB_CALL,
        .kind = kGAB_NATIVE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CALL_NATIVE),
    },
    {
        .name = mGAB_CALL,
        .kind = kGAB_BLOCK,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CALL_BLOCK),
    },
    {
        .name = mGAB_PUT,
        .kind = kGAB_CHANNEL,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_PUT),
    },
    {
        .name = mGAB_TAKE,
        .kind = kGAB_CHANNEL,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_TAKE),
    },
};

struct native {
  const char *name;
  union {
    enum gab_kind kind;
    const char *message;
    const char *box_type;
  };
  gab_native_f native;
};

enum gab_signal gab_yield(struct gab_triple gab) {
  if (gab_sigwaiting(gab)) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
#if cGAB_LOG_EG
    fprintf(stderr, "[WORKER %i] RECV SIG: %i\n", gab.wkid, sig.signal);
#endif
    return sig.signal;
  }

  return sGAB_IGN;
}

void gab_busywait(struct gab_triple gab) {
  if (gab.eg->wait > 0) {
    thrd_sleep(&(const struct timespec){.tv_nsec = gab.eg->wait}, nullptr);
  }

  thrd_yield();
}

int32_t gab_njobs(struct gab_triple gab) {
  struct gab_sig sig = atomic_load(&gab.eg->sig);
  return __builtin_popcountl(sig.mask);
}

void gab_jbalive(struct gab_triple gab, int32_t wkid) {
  for (;;) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    struct gab_sig next = {
        .schedule = sig.schedule,
        .signal = sig.signal,
        .mask = sig.mask | (1 << wkid),
    };

    gab_assert(!(next.signal == sGAB_IGN && next.schedule == 0),
               "Signalling GAB_IGN is invalid");
    if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
      break;

    gab_busywait(gab);
  }
}

bool gab_jbisalive(struct gab_triple gab, int32_t wkid) {
  struct gab_sig sig = atomic_load(&gab.eg->sig);
  return sig.mask & (1 << wkid);
}

void gab_jbunalive(struct gab_triple gab, int32_t wkid) {
  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      // This should remain the *only* place in the system
      // where we propagate the TERM signal.
      gab_sigpropagate(gab);
      break;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    struct gab_sig next = {
        .schedule = sig.schedule,
        .signal = sig.signal,
        .mask = sig.mask & ~(1 << wkid),
    };

    gab_assert(!(next.signal == sGAB_IGN && next.schedule == 0),
               "Signalling GAB_IGN is invalid");
    if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
      break;
  }
}

int32_t gc_job(void *data) {
  struct gab_triple *g = data;
  struct gab_triple gab = *g;
  gab_assert(gab.wkid == 0, "The GC worker shall have wkid = 0");

  struct gab_job *job = gab.eg->jobs + gab.wkid;

  cnd_init(&gab.eg->gc_cnd);
  mtx_lock(&gab.eg->gc_mtx);

  while (gab_njobs(gab) > 0) {
    int res = cnd_wait(&gab.eg->gc_cnd, &gab.eg->gc_mtx);

    if (res == thrd_timedout)
      continue;

    for (;;) {
      struct gab_sig sig = atomic_load(&gab.eg->sig);
      struct gab_sig expected = {sig.mask, -2, sig.signal};
      struct gab_sig desired = {sig.mask, -1, sig.signal};
      if (atomic_compare_exchange_weak(&gab.eg->sig, &expected, desired))
        break;
    }

    if (res == thrd_error)
      continue;

  read_signal:
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      gab_sigclear(gab);
      continue;
    case sGAB_COLL: {
      gab_gcdocollect(gab);
      gab_sigclear(gab);

      struct gab_sig sig = atomic_load(&gab.eg->sig);
      assert(!(sig.schedule == 0 && sig.signal == sGAB_IGN));

      continue;
    }
    case sGAB_IGN:
      gab_busywait(gab);
      // If we woke up due to a signal, we need
      // to continue looping until we receive the signal.
      if (res == thrd_success && gab_njobs(gab) > 0)
        goto read_signal;

      break;
    }

    /*
     * TODO @cgab @runtime: Coordinate work stealing here, where we are
     * guaranteed to *not* be collecting.
     *
     * if we have spare jobs:
     *  look for the first worker
     */
  }

#if cGAB_LOG_EG
  fprintf(stderr, "[GCWORKER] BAILING\n");
#endif
  free(g);
  v_gab_value_destroy(&job->lock_keep);
  cnd_destroy(&gab.eg->gc_cnd);
  mtx_unlock(&gab.eg->gc_mtx);
  return 0;
}

/*
 * TODO @cgab @runtime: Implement some form of work stealing (Or preemption).
 */
static inline bool worker_isrunning(struct gab_triple gab,
                                    struct gab_job *job) {
  if (q_gab_value_is_empty(&job->queue))
    return false;

  gab_value fiber = q_gab_value_peek(&job->queue);
  return gab_valkind(fiber) == kGAB_FIBERRUNNING;
}

static const char *gab_opcode_names[] = {
#define OP_CODE(name) #name,
#include "bytecode.h"
#undef OP_CODE
#undef GAB_OPCODE_NAMES_IMPL
};

static inline bool worker_step(struct gab_triple gab, struct gab_job *job) {
  switch (gab_yield(gab)) {
  case sGAB_COLL:
    gab_gcepochnext(gab);
    gab_sigpropagate(gab);
    break;
  case sGAB_TERM:
    return false;
  default:
    break;
  }

#if cGAB_LOG_EG
  gab_fprintf(stderr, "[WORKER $] TAKING WITH $ tries\n", gab_number(gab.wkid),
              gab_number(cGAB_WORKER_IDLE_TRIES));
#endif

  /*
   * Pull a value form the global work queue.
   */
  gab_value fiber =
      gab_tchntake(gab, gab.eg->work_channel, cGAB_WORKER_IDLE_TRIES);

#if cGAB_LOG_EG
  gab_fprintf(stderr, "[WORKER $] chntake result: $\n", gab_number(gab.wkid),
              fiber);
#endif

  // Terminate if requested.
  if (fiber == gab_cinvalid)
    return false;

  // If the channel closed, terminate
  if (fiber == gab_cundefined)
    return false;

  // If we timed out or closed, pull from our specific work_channel.
  if (fiber == gab_ctimeout)
    fiber = gab_tchntake(gab, job->work_channel, cGAB_WORKER_IDLE_TRIES);

  // Terminate if requested.
  if (fiber == gab_cinvalid)
    return false;

  // If the channel closed, terminate
  if (fiber == gab_cundefined)
    return false;

  if (fiber == gab_ctimeout) {
    // We have no work from our specific channel, or global channel.
    // If our local queue is empty, then we have no work to do.
    if (q_gab_value_is_empty(&job->queue))
      return gab_busywait(gab), true;

#if cGAB_LOG_EG
    gab_fprintf(stderr, "[WORKER $] RESORTING TO LOCALQUEUE $\n",
                gab_number(gab.wkid), fiber);

    size_t i = 0;
    for (size_t h = job->queue.head; h < job->queue.tail; h++, i++) {
      gab_value d = job->queue.data[h & (job->queue.cap - 1)];
      gab_assert(gab_valkind(d) == kGAB_FIBER,
                 "[WORKER %i] Fibers in queue shall only have kind "
                 "kGAB_FIBER, not %d.",
                 gab.wkid, gab_valkind(d));
      gab_fprintf(stderr, "[WORKER $] $ is waiting at $\n",
                  gab_number(gab.wkid), d, gab_number(i));
    }
#endif

  } else {
    gab_assert(gab_valkind(fiber) == kGAB_FIBER,
               "Fibers in the queue shall only have type kGAB_FIBER, not %d.",
               gab_valkind(fiber));

    // Our global take succeeded - append to our local queue.
    if (!q_gab_value_push(&job->queue, fiber))
      gab_assert(false, "Shall not fail to append fiber to local queue.");
  }

  // Peek at job to do on the queue.
  fiber = q_gab_value_peek(&job->queue);

  gab_assert(gab_valkind(fiber) != kGAB_FIBERDONE,
             "Fibers about to be run shall not be kGAB_FIBERDONE.");

  gab_assert(gab_valkind(fiber) == kGAB_FIBER,
             "Fibers in the queue shall only have type kGAB_FIBER, not %d.",
             gab_valkind(fiber));

  gab_assert(q_gab_value_peek(&job->queue) == fiber,
             "The fiber about to be run shall be at the front of the queue.");

#if cGAB_LOG_EG
  gab_fprintf(stderr, "[WORKER $] EXECUTING $\n", gab_number(gab.wkid), fiber);
#endif

  // Run our fiber.
  union gab_value_pair res = gab_vmexec(gab, fiber);

#if cGAB_LOG_EG
  gab_fprintf(stderr, "[WORKER $] EXECUTED: $ -> $\n", gab_number(gab.wkid),
              fiber, res.status);

  if (res.status == gab_ctimeout) {
    fprintf(stderr, "[WORKER %i] TIMED OUT FROM %s\n", gab.wkid,
            gab_opcode_names[*GAB_VAL_TO_FIBER(fiber)->vm.ip]);
  }
#endif

  // We did work - pop it off the queue now.
  gab_value popped = q_gab_value_pop(&job->queue);
  gab_assert(fiber == popped, "The popped fiber shall match the fiber we ran "
                              "off the front of the queue.");

  switch (res.status) {
  case gab_ctimeout:
    gab_assert(!gab_fibisrunning(popped),
               "A popped fiber shall not be running");
    gab_assert(!gab_fibisdone(popped), "A timedout fiber shall not be done");

    gab_assert(gab_valkind(fiber) == kGAB_FIBER,
               "Fibers in the queue shall only have type kGAB_FIBER, not %d.",
               gab_valkind(fiber));
#if cGAB_LOG_EG
#endif

    // We did not complete the work. Push back onto our queue.
    if (!q_gab_value_push(&job->queue, fiber))
      gab_assert(
          false,
          "There is guaranteed to be space for the fiber in this codepath.");
    break;
  // We completed the work. Nothing else to do.
  case gab_cvalid:
    gab_assert(gab_fibisdone(popped), "A valid fiber shall be done");
    // We panicked. Crash the system.
    if (res.aresult->data[0] != gab_ok) {
      gab_value err = res.aresult->data[1];
      if (err != gab_cinvalid) {
        gab_iref(gab, err);
        gab_egkeep(gab.eg, err);

        v_gab_value_thrd_push(&gab.eg->err, err);

        gab_sigterm(gab);
      }
    }
    break;

  // We were interruppted by sGAB_TERM. Signal will be handled below.
  case gab_cinvalid:
    gab_assert(gab_fibisdone(popped), "A terminated fiber shall be done");
    return false;
  default:
    gab_assert(false, "Unhandled result.status value");
  }

  return true;
}

static inline void worker_bail(struct gab_triple gab, struct gab_job *job) {
#if cGAB_LOG_EG
  fprintf(stderr, "[WORKER %i] BAILING\n", gab.wkid);
#endif

  // Wait for the terminate signal to arrive for this thread
  while (!gab_sigwaiting(gab))
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      continue;
    case sGAB_TERM:
      goto bail;
    case sGAB_IGN:
      break;
    }

bail:
  while (!q_gab_value_is_empty(&job->queue)) {
    gab_value fiber = q_gab_value_peek(&job->queue);

    gab_assert(gab_sigwaiting(gab), "While bailing, there shall be a sGAB_TERM "
                                    "signal waiting for this worker");

    gab_assert(gab_valkind(fiber) == kGAB_FIBER,
               "Fibers in the queue should only have type kGAB_FIBER, not %d",
               gab_valkind(fiber));

    // Run each queued fiber. Since there is a TERM signal waiting on this
    // worker, each fiber will terminate itself here, in one instruction.
    union gab_value_pair res = gab_vmexec(gab, fiber);
#if cGAB_LOG_EG
    if (res.status == gab_ctimeout)
      gab_fprintf(stderr, "[WORKER $] Failed to term $\n", gab_number(gab.wkid),
                  fiber);
#endif
    // Ensure that the termination occurred.
    gab_assert(res.status != gab_ctimeout,
               "One step of execution shall 'bail' the fiber. %s did not bail.",
               gab_opcode_names[*gab_fibvm(fiber)->ip]);

    gab_assert(gab_fibisdone(fiber), "A terminated fiber shall be done");

    // gab_value err = gab_fibstacktrace(gab, fiber);
    //
    // gab_iref(gab, err);
    // gab_egkeep(gab.eg, err);
    //
    // v_gab_value_thrd_push(&gab.eg->err, err);

    // Truly pop off the fiber now.
    gab_value popped = q_gab_value_pop(&job->queue);

    gab_assert(job->locked == 0,
               "The worker shall have a balanced 'lock' value of 0 when "
               "bailed. Saw %d. Last ran: %s.",
               job->locked, gab_opcode_names[*gab_fibvm(popped)->ip]);
  }

  gab_assert(q_gab_value_is_empty(&job->queue),
             "The queue shall be empty once all fibers have bailed");

  gab_assert(
      job->locked == 0,
      "The worker (%i) shall have a balanced 'lock' value of 0 when bailed. "
      "Saw %d.",
      gab.wkid, job->locked);

  gab_jbunalive(gab, gab.wkid);

  v_gab_value_destroy(&job->lock_keep);
}

uint64_t gab_egalive(struct gab_eg *eg) {
  struct gab_sig sig = atomic_load(&eg->sig);
  return __builtin_popcountl(sig.mask);
}

int32_t worker_job(void *data) {
  struct gab_triple *g = data;
  struct gab_triple gab = *g;

  gab_assert(gab.wkid > 1,
             "A workers id shall be greater than 1 (0 and 1 are reserved)");

  gab_jbalive(gab, gab.wkid);

  struct gab_job *job = gab.eg->jobs + gab.wkid;

#if cGAB_LOG_EG
  gab_fprintf(stderr, "[WORKER $] SPAWNED\n", gab_number(gab.wkid));
#endif

  while (worker_step(gab, job))
    ;

  worker_bail(gab, job);

#if cGAB_LOG_EG
  fprintf(stderr, "[WORKER %i] CLOSING\n", gab.wkid);
#endif

  free(g);

  return 0;
}

struct gab_job *next_available_job(struct gab_triple gab) {
  for (;;) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    uint64_t shifted = sig.mask >> gab.wkid;
    uint64_t next_available = __builtin_ctzl(~shifted);

    uint64_t idx = gab.wkid + next_available;

    if (idx >= gab.eg->len)
      return nullptr;

    struct gab_sig next = {
        .schedule = sig.schedule,
        .signal = sig.signal,
        .mask = sig.mask | (1 << idx),
    };

    gab_assert(!(next.signal == sGAB_IGN && next.schedule == 0), "");
    if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
      return gab.eg->jobs + idx;

    gab_busywait(gab);
  }

  // No room for new jobs
  return nullptr;
}

bool gab_jbcreate(struct gab_triple gab, struct gab_job *job, int(fn)(void *),
                  gab_value fiber) {
  if (!job)
    return false;

#if cGAB_LOG_EG
  fprintf(stderr, "[WORKER %i] spawning %lu\n", gab.wkid, job - gab.eg->jobs);
#endif

  job->locked = 0;
  v_gab_value_create(&job->lock_keep, 8);
  q_gab_value_create(&job->queue, 32);

  job->work_channel = gab_channel(gab);
  gab_iref(gab, job->work_channel);
  gab_egkeep(gab.eg, job->work_channel);

  if (fiber != gab_cundefined)
    if (!q_gab_value_push(&job->queue, fiber))
      gab_assert(false, "The queue shall always have space in this codepath");

  if (!fn)
    return true;

  struct gab_triple *gabcpy = malloc(sizeof(struct gab_triple));
  memcpy(gabcpy, &gab, sizeof(struct gab_triple));
  gabcpy->wkid = job - gab.eg->jobs;

  gab_assert(gabcpy->wkid != 1,
             "The copy's worker id shall not be the 'main thread' id");

  return thrd_create(&job->td, fn, gabcpy) == thrd_success;
}

bool gab_wkspawn(struct gab_triple gab, gab_value fiber) {
  return gab_jbcreate(gab, next_available_job(gab), worker_job, fiber);
}

union gab_value_pair gab_create(struct gab_create_argt args,
                                struct gab_triple gab_out[static 1]) {
  uint64_t njobs = args.jobs ? args.jobs : cGAB_DEFAULT_NJOBS;

  if (njobs > 29)
    return (union gab_value_pair){{gab_cinvalid}};

  uint64_t actual_njobs = njobs + 2;

  uint64_t egsize =
      sizeof(struct gab_eg) + sizeof(struct gab_job) * actual_njobs;

  struct gab_eg *eg = malloc(egsize);
  memset(eg, 0, egsize);

  eg->wait = args.wait;
  eg->len = actual_njobs;
  eg->hash_seed = time(nullptr);
  atomic_init(&eg->sig, (struct gab_sig){0, -1, 0});

  eg->args = gab_nil;

  // The only non-zero initialization that jobs need is epoch = 1
  for (uint64_t i = 0; i < eg->len; i++)
    eg->jobs[i].epoch = 1;

  v_gab_value_thrd_create(&eg->err, 8);

  mtx_init(&eg->shapes_mtx, mtx_plain);
  mtx_init(&eg->sources_mtx, mtx_plain);
  mtx_init(&eg->gc_mtx, mtx_plain);
  mtx_init(&eg->modules_mtx, mtx_plain);

  d_gab_src_create(&eg->sources, 8);

  gab_out->eg = eg;
  gab_out->flags = args.flags;
  gab_out->wkid = 1;

  struct gab_triple gab = *gab_out;

  // Maybe we can create/reserve a slot in jobs for
  // the user's main thread here?
  // gab wkid 1 can always be main thread.
  // we can have a flag that says 'detatch' or something
  // and will allow the main thread to begin contributing to the system.
  bool res = gab_jbcreate(gab, gab.eg->jobs + 1, nullptr, gab_cundefined);
  gab_assert(res, "Job creation shall not fail for the main thread");

  gab_jbalive(gab, 1);

  gab_gccreate(gab);

  res = gab_jbcreate(gab, gab.eg->jobs, gc_job, gab_cundefined);
  gab_assert(res, "Job creation shall not fail for the gc thread");

  gab_jbalive(gab, 0);

  gab_gclock(gab);

  eg->types[kGAB_NUMBER] = gab_string(gab, tGAB_NUMBER);
  eg->types[kGAB_BINARY] = gab_string(gab, tGAB_BINARY);
  eg->types[kGAB_STRING] = gab_string(gab, tGAB_STRING);
  eg->types[kGAB_MESSAGE] = gab_string(gab, tGAB_MESSAGE);
  eg->types[kGAB_PROTOTYPE] = gab_string(gab, tGAB_PROTOTYPE);
  eg->types[kGAB_NATIVE] = gab_string(gab, tGAB_NATIVE);
  eg->types[kGAB_BLOCK] = gab_string(gab, tGAB_BLOCK);
  eg->types[kGAB_SHAPE] = gab_string(gab, tGAB_SHAPE);
  eg->types[kGAB_SHAPELIST] = gab_string(gab, tGAB_SHAPE);
  eg->types[kGAB_RECORD] = gab_string(gab, tGAB_RECORD);
  eg->types[kGAB_RECORDNODE] = gab_string(gab, tGAB_RECORD);
  eg->types[kGAB_BOX] = gab_string(gab, tGAB_BOX);
  eg->types[kGAB_FIBER] = gab_string(gab, tGAB_FIBER);
  eg->types[kGAB_FIBERDONE] = gab_string(gab, tGAB_FIBER);
  eg->types[kGAB_FIBERRUNNING] = gab_string(gab, tGAB_FIBER);
  eg->types[kGAB_CHANNEL] = gab_string(gab, tGAB_CHANNEL);
  eg->types[kGAB_CHANNELCLOSED] = gab_string(gab, tGAB_CHANNEL);
  eg->types[kGAB_PRIMITIVE] = gab_string(gab, tGAB_PRIMITIVE);

  gab_niref(gab, 1, kGAB_NKINDS, eg->types);
  gab_negkeep(gab.eg, kGAB_NKINDS, eg->types);

  eg->shapes = __gab_shape(gab, 0);
  atomic_init(&eg->messages, gab_erecord(gab));
  atomic_init(&eg->macros, gab_erecord(gab));

  eg->work_channel = gab_iref(gab, gab_channel(gab));

  eg->args = gab_iref(gab, gab_slist(gab, 1, args.nargs, args.args));

  int nroots = 0;
  for (int i = 0; args.roots[i] != nullptr; i++) {
    gab_assert(nroots < cGAB_RESOURCE_MAX,
               "number of roots shall not exceed cGAB_RESOURCE_MAX");
    gab.eg->resroots[nroots++] = args.roots[i];
  }

  int nres = 0;
  for (int i = 0; args.resources[i].prefix != nullptr; i++) {
    gab_assert(nres < cGAB_RESOURCE_MAX,
               "number of resources shall not exceed cGAB_RESOURCE MAX");
    gab.eg->res[nres++] = args.resources[i];
  }

  for (int i = 0; i < LEN_CARRAY(kind_primitives); i++) {
    gab_egkeep(
        gab.eg,
        gab_iref(gab,
                 gab_def(gab, (struct gab_def_argt){
                                  gab_message(gab, kind_primitives[i].name),
                                  gab_type(gab, kind_primitives[i].kind),
                                  kind_primitives[i].primitive,
                              })));
  }

  for (int i = 0; i < LEN_CARRAY(val_primitives); i++) {
    gab_egkeep(
        gab.eg,
        gab_iref(gab, gab_def(gab, (struct gab_def_argt){
                                       gab_message(gab, val_primitives[i].name),
                                       val_primitives[i].val,
                                       val_primitives[i].primitive,
                                   })));
  }

  for (int i = 0; i < LEN_CARRAY(msg_primitives); i++) {
    gab_egkeep(
        gab.eg,
        gab_iref(gab,
                 gab_def(gab, (struct gab_def_argt){
                                  gab_message(gab, msg_primitives[i].name),
                                  gab_message(gab, msg_primitives[i].message),
                                  msg_primitives[i].primitive,
                              })));
  }

  for (int i = 0; i < LEN_CARRAY(all_primitives); i++) {
    for (int t = 0; t < kGAB_NKINDS; t++) {
      gab_egkeep(
          gab.eg,
          gab_iref(gab,
                   gab_def(gab, (struct gab_def_argt){
                                    gab_message(gab, all_primitives[i].name),
                                    gab_type(gab, t),
                                    all_primitives[i].primitive,
                                })));
    }
  }

  gab_gcunlock(gab);

  size_t len = 0;
  struct gab_package *cursor = args.packages;
  while (cursor->package)
    len++, cursor++;

  size_t nargs = 0;
  gab_value vargs[len + 1];
  const char *sargs[len + 1];

  sargs[nargs] = "";
  vargs[nargs] = gab_ok;
  nargs++;

  // Use each module that's asked for, in order.
  // Build up an array of names and values.
  for (int i = 0; i < len; i++) {
    struct gab_package *pkg = args.packages + i;

    union gab_value_pair res = gab_use(gab, (struct gab_use_argt){
                                                .spackage_name = pkg->package,
                                                .smodule_name = pkg->module,
                                                .len = nargs,
                                                .sargv = sargs,
                                                .argv = vargs,
                                            });
    if (res.status == gab_ctimeout)
      res = gab_fibawait(gab, res.vresult);

    // If any of these uses fail, return the failure.
    if (res.status != gab_cvalid)
      return res;

    if (res.aresult->data[0] != gab_ok)
      return res;

    vargs[nargs] = res.aresult->data[1];
    sargs[nargs] = pkg->alias    ? pkg->alias
                   : pkg->module ? pkg->module
                                 : pkg->package;
    nargs++;
  }

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(vargs, nargs),
  };
}

void dec_child_shapes(struct gab_triple gab, gab_value shp) {
  gab_assert(gab_valkind(shp) == kGAB_SHAPE ||
                 gab_valkind(shp) == kGAB_SHAPELIST,
             "Shall only operate on shapes");
  struct gab_oshape *shape = GAB_VAL_TO_SHAPE(shp);

  uint64_t len = shape->transitions.len / 2;

  for (size_t i = 0; i < len; i++)
    dec_child_shapes(gab, v_gab_value_val_at(&shape->transitions, i * 2 + 1));

  gab_dref(gab, shp);
}

void gab_destroy(struct gab_triple gab) {
  gab_assert(gab.wkid == 1, "Shall only be called from the main thread");

  bool res = gab_sigterm(gab);
  gab_assert(res, "Sigterm shall not fail when destryoing");

  if (gab_jbisalive(gab, gab.wkid))
    worker_bail(gab, gab.eg->jobs + 1);

  while (gab_njobs(gab) > 1)
    gab_busywait(gab);

  gab_dref(gab, gab.eg->work_channel);
  gab_dref(gab, gab.eg->args);
  gab_ndref(gab, 1, gab.eg->scratch.len, gab.eg->scratch.data);

  // for (uint64_t i = 0; i < gab.eg->strings.cap; i++)
  //   if (d_strings_iexists(&gab.eg->strings, i))
  //     gab_dref(gab, __gab_obj(d_strings_ikey(&gab.eg->strings, i)));

  if (gab_valkind(gab.eg->shapes) == kGAB_SHAPELIST)
    dec_child_shapes(gab, gab.eg->shapes);

  atomic_store(&gab.eg->messages, gab_cinvalid);
  gab.eg->shapes = gab_cinvalid;

  gab_assert(gab_njobs(gab) == 1,
             "There shall only be one thread remaining - the gc thread");

  /**
   * Four consececutive collections are needed here because
   * of the delayed nature of the RC algorithm.
   *
   * Decrements are process an epoch *after* they are queued.
   *
   * There are three epochs tracked, so we need three collections
   * to ensure that all rc events are processed.
   */

  // First, clear any pending signals.
  // I'm not sure if this makes sense to do here,
  // Or if this is the responsibility of user to clear
  // signal *before* calling this function.
  // gab_sigclear(gab);

  // gab_gcloglen(gab);
  res = gab_sigcoll(gab);
  while (gab_signaling(gab))
    gab_busywait(gab);

  gab_assert(res, "sigcoll shall not fail");
  struct gab_sig sig = atomic_load(&gab.eg->sig);
  gab_assert(sig.signal == sGAB_IGN,
             "After collection, signal shall be sGAB_IGN");

  // gab_gcloglen(gab);
  res = gab_sigcoll(gab);
  while (gab_signaling(gab))
    gab_busywait(gab);

  gab_assert(res, "sigcoll shall not fail");
  sig = atomic_load(&gab.eg->sig);
  gab_assert(sig.signal == sGAB_IGN,
             "After collection, signal shall be sGAB_IGN");

  // gab_gcloglen(gab);
  res = gab_sigcoll(gab);

  while (gab_signaling(gab))
    gab_busywait(gab);

  gab_assert(res, "sigcoll shall not fail");
  sig = atomic_load(&gab.eg->sig);
  gab_assert(sig.signal == sGAB_IGN,
             "After collection, signal shall be sGAB_IGN");

  // gab_gcloglen(gab);
  res = gab_sigcoll(gab);

  while (gab_signaling(gab))
    gab_busywait(gab);

  gab_assert(res, "sigcoll shall not fail");
  sig = atomic_load(&gab.eg->sig);
  gab_assert(sig.signal == sGAB_IGN,
             "After collection, signal shall be sGAB_IGN");

  gab_gcassertdone(gab);

  /*assert(gab.eg->bytes_allocated == 0);*/
  gab_assert(gab_njobs(gab) == 1,
             "There shall only be one worker alive - the gc thread");

  gab_gcdestroy(gab);

  gab_sigterm(gab);

  thrd_join(gab.eg->jobs[0].td, nullptr);

  for (uint64_t i = 0; i < gab.eg->sources.cap; i++) {
    if (d_gab_src_iexists(&gab.eg->sources, i)) {
      struct gab_src *s = d_gab_src_ival(&gab.eg->sources, i);
      gab_srcdestroy(s);
    }
  }

  d_strings_destroy(&gab.eg->strings);
  d_gab_modules_destroy(&gab.eg->modules);
  d_gab_src_destroy(&gab.eg->sources);

  v_gab_value_destroy(&gab.eg->scratch);
  v_gab_value_thrd_destroy(&gab.eg->err);

  mtx_destroy(&gab.eg->shapes_mtx);
  mtx_destroy(&gab.eg->gc_mtx);
  mtx_destroy(&gab.eg->sources_mtx);
  mtx_destroy(&gab.eg->modules_mtx);

  free(gab.eg);
}

bool repl_check_res(struct gab_triple gab, union gab_value_pair res) {
  gab_value *err = gab_egerrs(gab.eg);

  while (gab_signaling(gab))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      gab_sigpropagate(gab);
      break;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      continue;
    }

  if (err) {
    for (gab_value *thiserr = err; *thiserr != gab_nil; thiserr++) {
      gab_assert(gab_valkind(*thiserr) == kGAB_RECORD,
                 "An error value shall be a record");

      if (*thiserr == res.vresult)
        continue;

      const char *errstr = gab_errtocs(gab, *thiserr);

      if (errstr)
        puts(errstr);

      // TODO @cgab @bug: Do something else if errtocs fails
      gab_assert(errstr, "gab_errtocs should not produce nil. This happens if "
                         "you try to create strings while signalling.");
    };

    free(err);
  }

  if (res.status != gab_cvalid) {
    const char *errstr = gab_errtocs(gab, res.vresult);

    if (errstr)
      puts(errstr);

    return true;
  }

  return err != nullptr;
}

bool repl_check_needmore(struct gab_triple gab, union gab_value_pair res) {
  if (res.status != gab_cinvalid)
    return false;

  gab_value err = res.vresult;
  gab_value status = gab_mrecat(gab, err, "status");
  gab_assert(status != gab_cundefined,
             "The error record shall have a status field");

  gab_assert(gab_valkind(status) == kGAB_STRING,
             "The status field shall be a string, not %d.",
             gab_valkind(status));

  const char *status_name = gab_strdata(&status);
  if (!strcmp(status_name, "UNEXPECTED_EOF"))
    return true;

  return false;
}

/*
 * Should be able to take work here on 1st worker maybe.
 */
void repl_wait_for(struct gab_triple gab, struct gab_repl_argt *args,
                   gab_value fib) {
  while (!gab_fibisdone(fib)) {
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      // Fallthrough
    case sGAB_TERM:
      gab_sigpropagate(gab);
      // Fallthrough
    default:
      gab_busywait(gab);
      continue;
    }
  }
}

void gab_repl(struct gab_triple gab, struct gab_repl_argt args) {
  uint64_t iterations = 0;
  gab_value env = gab_cinvalid;

  args.welcome_message = args.welcome_message ? args.welcome_message : "";
  args.result_prefix = args.result_prefix ? args.result_prefix : "";
  args.prompt_prefix = args.prompt_prefix ? args.prompt_prefix : "";
  args.promptmore_prefix =
      args.promptmore_prefix ? args.promptmore_prefix : args.prompt_prefix;

  printf("%s\n", args.welcome_message);

  v_char source = {};

  for (;;) {

  readmore:
    char *line;
    if (source.len)
      line = args.readline(args.promptmore_prefix);
    else
      line = args.readline(args.prompt_prefix);

    if (!line)
      return;

    if (line[0] == '\0')
      continue;

    if (args.add_hist)
      args.add_hist(line);

    v_char_spush(&source, s_char_cstr(line));
    v_char_push(&source, '\n');

    iterations++;

  retry:
    // Skip self
    size_t reclen = env == gab_cinvalid ? 0 : (gab_reclen(env) - 1);

    size_t len = reclen + args.len;

    // Allocate the maximum number of space we may need
    const char *keys[len + 1];
    gab_value keyvals[len + 1];
    gab_value vals[len + 1];

    // TODO @engine @bug: The calls to string below need to have signals handled
    // This *kind* of works, but a fiber can signal us *after* we pass this
    // point.
    while (gab_signaling(gab))
      switch (gab_yield(gab)) {
      case sGAB_TERM:
        gab_sigpropagate(gab);
        break;
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        continue;
      }

    // Insert local names from env
    for (size_t i = 0; i < reclen; i++) {
      size_t index = i + 1;
      keyvals[i] = gab_ukrecat(env, index);
      vals[i] = gab_uvrecat(env, index);
      keys[i] = gab_strdata(keyvals + i);
    }

    // If there is a local name in the env, skip it from the args.
    // otherwise, add an element for this.
    size_t n = reclen;
    for (size_t i = 0; i < args.len; i++) {
      gab_value vkey = gab_string(gab, args.sargv[i]);
      if (vkey == gab_cinvalid)
        goto retry;

      if (env != gab_cinvalid && gab_rechas(env, vkey))
        continue;

      keys[n] = args.sargv[i];
      vals[n] = args.argv[i];
      n++;
    }

    // Append the iterations number to the end of the given name
    char unique_name[strlen(args.name) + 16];
    snprintf(unique_name, sizeof(unique_name), "%s:%" PRIu64 "", args.name,
             iterations);

    union gab_value_pair block = gab_build(gab, (struct gab_parse_argt){
                                                    .name = unique_name,
                                                    .source = source.data,
                                                    .source_len = source.len,
                                                    .flags = args.flags,
                                                    .len = n,
                                                    .argv = keys,
                                                });
    if (repl_check_needmore(gab, block))
      goto readmore;

    if (repl_check_res(gab, block))
      goto fin;

    // gab_value before_env = gab_blkshp(block.vresult);

    union gab_value_pair fiber = gab_arun(gab, (struct gab_run_argt){
                                                   .flags = args.flags,
                                                   .len = n,
                                                   .argv = vals,
                                                   .main = block.vresult,
                                               });

    if (repl_check_res(gab, fiber))
      goto fin;

    repl_wait_for(gab, &args, fiber.vresult);

    union gab_value_pair res = gab_fibawait(gab, fiber.vresult);

    /* Setup env regardless of run failing/succeeding */
    // TODO @bug: replace awaite - thats gross.
    // how else can I get variables to work in the repl?
    // gab_value new_env = gab_fibawaite(gab, fiber.vresult);

    /* Sometimes the env that is returned from here is
     *  an env from a ~different~ block. This is because
     *  we always tailcall, so the bottom frame can change the block
     *  it belongs to throughout execution.
     **/

    // if (env == gab_cinvalid || new_env == gab_cinvalid)
    // env = new_env;
    // If the block's environment is equal to the fiber's final environment
    // then we know we *didn't* tailcall out of the block.
    // TODO @cgab @bug: Don't leak this reccat below
    // else if (before_env == gab_recshp(new_env))
    // env = gab_iref(gab, gab_reccat(gab, env, new_env));

    // assert(env != gab_cinvalid);

    flockfile(stdout);

    if (repl_check_res(gab, res))
      goto fin;

    for (int32_t i = 1; i < res.aresult->len; i++) {
      gab_value arg = res.aresult->data[i];

      if (i == res.aresult->len - 1) {
        gab_fvalinspect(stdout, gab_pvalintos(gab, arg, ""), -1);
      } else {
        gab_fvalinspect(stdout, gab_pvalintos(gab, arg, ""), -1);
        printf(" ");
      }
    }

    putc('\n', stdout);

  fin:
    funlockfile(stdout);

    source.len = 0;
  }
}

union gab_value_pair gab_aexec(struct gab_triple gab,
                               struct gab_exec_argt args) {
  gab.flags |= args.flags;

  union gab_value_pair main = gab_build(gab, (struct gab_parse_argt){
                                                 .name = args.name,
                                                 .source_len = args.source_len,
                                                 .source = args.source,
                                                 .len = args.len,
                                                 .argv = args.sargv,
                                             });

  if (main.status != gab_cvalid || gab.flags & fGAB_BUILD_CHECK)
    return main;

  return gab_arun(gab, (struct gab_run_argt){
                           .main = main.vresult,
                           .len = args.len,
                           .argv = args.argv,
                       });
}

union gab_value_pair gab_exec(struct gab_triple gab,
                              struct gab_exec_argt args) {
  union gab_value_pair fib = gab_aexec(gab, args);

  if (fib.status != gab_cvalid)
    return fib;

  // If we aren't the main-thread, we just return the await.
  if (gab.wkid != 1)
    return gab_fibawait(gab, fib.vresult);

  // Prevent running the below loop recursively.
  if (worker_isrunning(gab, gab.eg->jobs + gab.wkid))
    return gab_fibawait(gab, fib.vresult);

  // If we are on the main thread, we
  // can do some work while we block (If we aren't already).
  for (;;) {
    union gab_value_pair res = gab_tfibawait(gab, fib.vresult, 1);

    if (res.status != gab_ctimeout)
      return res;

    if (!gab_jbisalive(gab, gab.wkid) ||
        !worker_step(gab, gab.eg->jobs + gab.wkid))
      return worker_bail(gab, gab.eg->jobs + gab.wkid),
             gab_tfibawait(gab, fib.vresult, 1);
  }

  assert(false && "UNREACHABLE");
}

gab_value dodef(struct gab_triple gab, gab_value messages, uint64_t len,
                struct gab_def_argt args[static len]) {

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    struct gab_def_argt arg = args[i];

    gab_value specs = gab_recat(messages, arg.message);

    if (specs == gab_cundefined)
      specs = gab_record(gab, 0, 0, nullptr, nullptr);

    gab_value newspecs =
        gab_recput(gab, specs, arg.receiver, arg.specialization);

    messages = gab_recput(gab, messages, arg.message, newspecs);
  }

  return gab_gcunlock(gab), messages;
}

gab_value dodefmacro(struct gab_triple gab, gab_value messages, uint64_t len,
                     struct gab_def_argt args[static len]) {

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    struct gab_def_argt arg = args[i];

    messages = gab_recput(gab, messages, arg.message, arg.specialization);
  }

  return gab_gcunlock(gab), messages;
}

// TODO @feat: Implement macro equivalent
bool gab_ndef(struct gab_triple gab, uint64_t len,
              struct gab_def_argt args[static len]) {
  gab_value messages = atomic_load(&gab.eg->messages);

  for (;;) {
    if (atomic_compare_exchange_weak(&gab.eg->messages, &messages,
                                     dodef(gab, messages, len, args)))
      return atomic_fetch_add(&gab.eg->messages_epoch, 1), true;

    gab_busywait(gab);
  }

  return false;
}

bool gab_ndefmacro(struct gab_triple gab, uint64_t len,
                   struct gab_def_argt args[static len]) {
  gab_value messages = atomic_load(&gab.eg->macros);

  for (;;) {
    if (atomic_compare_exchange_weak(&gab.eg->macros, &messages,
                                     dodefmacro(gab, messages, len, args)))
      return atomic_fetch_add(&gab.eg->macros_epoch, 1), true;

    gab_busywait(gab);
  }

  return false;
}

struct gab_ostring *gab_egstrfind(struct gab_eg *self, uint64_t hash,
                                  uint64_t len, const char *data) {
  if (self->strings.len == 0)
    return nullptr;

  uint64_t index = hash & (self->strings.cap - 1);

  for (;;) {
    d_status status = d_strings_istatus(&self->strings, index);
    struct gab_ostring *key = d_strings_ikey(&self->strings, index);

    switch (status) {
    case D_TOMBSTONE:
      break;
    case D_EMPTY:
      return nullptr;
    case D_FULL:
      if (key->len == len && key->hash == hash && !memcmp(key->data, data, len))
        return key;
    }

    index = (index + 1) & (self->strings.cap - 1);
  }
}

a_gab_value *gab_segmodat(struct gab_eg *eg, const char *name) {
  uint64_t hash = s_char_hash(s_char_cstr(name));

  mtx_lock(&eg->modules_mtx);

  a_gab_value *module = d_gab_modules_read(&eg->modules, hash);

  mtx_unlock(&eg->modules_mtx);

  return module;
}

a_gab_value *gab_segmodput(struct gab_eg *eg, const char *name,
                           a_gab_value *module) {
  uint64_t hash = s_char_hash(s_char_cstr(name));

  mtx_lock(&eg->modules_mtx);

  if (d_gab_modules_read(&eg->modules, hash) != nullptr)
    return mtx_unlock(&eg->modules_mtx), nullptr;

  d_gab_modules_insert(&eg->modules, hash, module);
  return mtx_unlock(&eg->modules_mtx), module;
}

uint64_t gab_egkeep(struct gab_eg *gab, gab_value v) {
  return gab_negkeep(gab, 1, &v);
}

uint64_t gab_negkeep(struct gab_eg *gab, uint64_t len,
                     gab_value values[static len]) {
  mtx_lock(&gab->modules_mtx);

  for (uint64_t i = 0; i < len; i++)
    if (gab_valiso(values[i]))
      v_gab_value_push(&gab->scratch, values[i]);

  mtx_unlock(&gab->modules_mtx);

  return len;
}

int gab_sprintf(char *dest, size_t n, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  int res = gab_vsprintf(dest, n, fmt, va);

  va_end(va);

  return res;
}

int gab_psprintf(char *dest, size_t n, const char *prefix, const char *fmt,
                 ...) {
  va_list va;
  va_start(va, fmt);

  int res = gab_vpsprintf(dest, n, prefix, fmt, va);

  va_end(va);

  return res;
}

int gab_fprintf(FILE *stream, const char *fmt, ...) {
  va_list va;

  for (size_t i = 128;; i <<= 1) {
    va_start(va, fmt);

    char buf[i];
    if (gab_vsprintf(buf, i, fmt, va) >= 0)
      return va_end(va), fputs(buf, stream);

    va_end(va);
  }

  return -1;
}

int gab_npsprintf(char *dest, size_t n, const char *prefix, const char *fmt,
                  uint64_t argc, gab_value *argv) {
  const char *c = fmt;
  char *cursor = dest;
  size_t remaining = n;
  uint64_t i = 0;

  while (*c != '\0') {
    switch (*c) {
    case '$': {
      if (i >= argc)
        return -1;

      gab_value arg = argv[i++];

      int res = gab_psvalinspect(&cursor, &remaining, arg, prefix, 1);

      if (res < 0)
        return res;

      break;
    }
    default:
      if (remaining == 0)
        return -1;

      *cursor++ = *c;
      remaining -= 1;
    }

    c++;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  if (i != argc)
    return -1;

  return n - remaining;
}

int gab_nsprintf(char *dest, size_t n, const char *fmt, uint64_t argc,
                 gab_value *argv) {
  const char *c = fmt;
  char *cursor = dest;
  size_t remaining = n;
  uint64_t i = 0;

  mbstate_t state = {0};

  while (true) {
    size_t width = mbrlen(c, MB_CUR_MAX, &state);

    // Null char encountered, we are done.
    if (width == 0)
      break;

    if (width == (size_t)-1)
      return -EILSEQ;

    if (width == (size_t)-2)
      return -1;

    // Success, encountered a full glyph
    if (width == 1 && *c == '$') {
      if (i >= argc)
        return -1;

      gab_value arg = argv[i++];

      int res = gab_svalinspect(&cursor, &remaining, arg, 1);

      if (res < 0)
        return -1;

    } else {
      if (remaining < width)
        return -1;

      memcpy(cursor, c, width);

      // Advance cursor and remaining by the number of bytes written.
      cursor += width;
      remaining -= width;
    }

    // Advance the source string by number of bytes read.
    c += width;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  if (i != argc)
    return -1;

  return n - remaining;
}

int gab_vpsprintf(char *dest, size_t n, const char *prefix, const char *fmt,
                  va_list varargs) {
  const char *c = fmt;
  char *cursor = dest;
  size_t remaining = n;

  while (*c != '\0') {
    switch (*c) {
    case '@': {
      gab_value arg = va_arg(varargs, gab_value);

      int res = gab_psvalinspect(&cursor, &remaining, arg, "", 1);

      if (res < 0)
        return -1;

      break;
    }
    case '$': {
      gab_value arg = va_arg(varargs, gab_value);

      int res = gab_psvalinspect(&cursor, &remaining, arg, prefix, 1);

      if (res < 0)
        return -1;

      break;
    }
    default:
      if (remaining == 0)
        return -1;

      *cursor++ = *c;
      remaining -= 1;
    }
    c++;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  return n - remaining;
}

int gab_vsprintf(char *dest, size_t n, const char *fmt, va_list varargs) {
  const char *c = fmt;
  char *cursor = dest;
  size_t remaining = n;

  mbstate_t state = {0};

  // This logic has a bug with codepoints. Jenkies
  while (*c != '\0') {

    size_t width = mbrlen(c, MB_CUR_MAX, &state);

    // Success, encountered a full glyph
    if (width == 0)
      break;

    if (width == (size_t)-1)
      return -EILSEQ;

    if (width == (size_t)-2)
      return -1;

    if (width == 1 && *c == '$') {
      gab_value arg = va_arg(varargs, gab_value);

      int res = gab_svalinspect(&cursor, &remaining, arg, 1);

      if (res < 0)
        return -1;

    } else {
      if (remaining < width)
        return -1;

      memcpy(cursor, c, width);

      // Advance cursor and remaining by the number of bytes written.
      cursor += width;
      remaining -= width;
    }

    // Advance the source string by number of bytes read.
    c += width;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  return n - remaining;
}

static const char *gab_status_names[] = {
#define STATUS(name, message) #name,
#include "status_code.h"
#undef STATUS
};

static const char *gab_token_names[] = {
#define TOKEN(message) #message,
#include "token.h"
#undef TOKEN
};

static const char *gab_status_messages[] = {
#define STATUS(name, message) message,
#include "status_code.h"
#undef STATUS
};

int sprint_pretty_err(struct gab_triple gab, char **buf, size_t *len,
                      struct errdetails *args, const char *hint) {
  struct gab_src *src =
      d_gab_src_read(&gab.eg->sources, gab_string(gab, args->src_name));

  const char *tok_name =
      src ? gab_token_names[v_gab_token_val_at(&src->tokens, args->token)]
          : "C";

  const char *src_name = src ? gab_strdata(&src->name) : "C";

  // Include gab@<wkid> here isn't useful really anymore. Can be removed.
  // Maybe it is better to show the fiber?
  if (snprintf_through(buf, len,
                       "[" GAB_GREEN "gab@%i" GAB_RESET
                       "] panicked in " GAB_GREEN "%s" GAB_RESET
                       " near " GAB_YELLOW "%s.\n\n" GAB_RESET,
                       args->wkid, src_name, tok_name) < 0)
    return -1;

  if (args->status)
    if (snprintf_through(buf, len,
                         GAB_RED "E%03i" GAB_RESET "|" GAB_RED " %s" GAB_RESET
                                 "\n",
                         args->status, gab_status_messages[args->status]) < 0)
      return -1;

  if (src) {
    s_char tok_src = v_s_char_val_at(&src->token_srcs, args->token);

    uint64_t line_num = v_uint64_t_val_at(&src->token_lines, args->token);

    s_char line_src = v_s_char_val_at(&src->lines, line_num - 1);

    // Skip preceding whitespace for this line.
    size_t whitespace_skipped = 0;
    while (line_src.data[whitespace_skipped] == ' ' ||
           line_src.data[whitespace_skipped] == '\t') {
      whitespace_skipped++;
      assert(line_src.len > whitespace_skipped);
    }

    line_src.data += whitespace_skipped;
    line_src.len -= whitespace_skipped;

    if (line_num > 1) {
      size_t prev_line_num = line_num - 2;
      s_char prev_line_src = v_s_char_val_at(&src->lines, prev_line_num);
      if (prev_line_src.len > whitespace_skipped)
        if (snprintf_through(buf, len, "\n      %.*s",
                             (int)(prev_line_src.len - whitespace_skipped),
                             prev_line_src.data + whitespace_skipped) < 0) {
          return -1;
        }
    }

    int leftpad = (int)(tok_src.data - line_src.data);
    // int tokpad = (int)tok_src.len - 2;
    int rhs_width = (int)tok_src.len - 1;

    const char *lhs = "^";
    const char *rhs = "^^^^^^^^^^^^^^^^^^^^^^";

    if (snprintf_through(buf, len,
                         "\n" GAB_RED "%.4" PRIu64 "" GAB_RESET "| %.*s"
                         "\n      " GAB_YELLOW "%*s%s%.*s" GAB_RESET "",
                         line_num, (int)line_src.len, line_src.data, leftpad,
                         "", lhs, rhs_width, rhs) < 0) {
      return -1;
    }

    if (line_num < src->lines.len) {
      size_t next_line_num = line_num;
      s_char next_line_src = v_s_char_val_at(&src->lines, next_line_num);

      if (next_line_src.len > whitespace_skipped)
        if (snprintf_through(buf, len, "\n      %.*s",
                             (int)(next_line_src.len - whitespace_skipped),
                             next_line_src.data + whitespace_skipped) < 0) {
          return -1;
        }
    }
  }

  if (hint > 0)
    if (snprintf_through(buf, len, "\n\n%s", hint) < 0)
      return -1;

  return snprintf_through(buf, len, "\n");
};

int sprint_structured_err(struct gab_triple gab, char **buf, size_t *len,
                          struct errdetails *d, const char *hint) {
  snprintf_through(buf, len, "%s:%s:%s:%s", gab_status_names[d->status],
                   d->src_name, d->tok_name, d->msg_name);

  snprintf_through(
      buf, len, ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 "",
      d->row, d->col_begin, d->col_end, d->byte_begin, d->byte_end);

  return snprintf_through(buf, len, "\n");
}

gab_value gab_vspanicf(struct gab_triple gab, va_list va,
                       struct gab_err_argt args) {
  struct errdetails err = {
      .tok_name =
          args.src && args.src->source->len
              ? gab_token_names[v_gab_token_val_at(&args.src->tokens, args.tok)]
              : "C",
  };

  if (args.src && args.src->source->len) {
    err.row = v_uint64_t_val_at(&args.src->token_lines, args.tok);

    s_char line_src = v_s_char_val_at(&args.src->lines, err.row - 1);
    s_char tok_src = v_s_char_val_at(&args.src->token_srcs, args.tok);

    assert(tok_src.data >= line_src.data);

    err.col_begin = tok_src.data - line_src.data;
    err.col_end = tok_src.data + tok_src.len - line_src.data;

    err.byte_begin = tok_src.data - args.src->source->data;
    err.byte_end = tok_src.data + tok_src.len - args.src->source->data;
  }

  err.src_name = args.src ? gab_strdata(&args.src->name) : "C";

  err.status = args.status;

  gab_gclock(gab);

  char hint[cGAB_ERR_SPRINTF_BUF_MAX] = {0};
  if (args.note_fmt) {
    if (gab_vpsprintf(hint, sizeof(hint), "   | ", args.note_fmt, va) < 0)
      ;
    // assert(false);
  }

  // Signaling here causes the record to have no keys.
  gab_value vstatus = gab_message(gab, "status");
  if (vstatus == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vsrc = gab_message(gab, "src");
  if (vsrc == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vtok_offset = gab_message(gab, "tok\\offset");
  if (vtok_offset == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vtok_t = gab_message(gab, "tok\\t");
  if (vtok_t == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vhint = gab_message(gab, "hint");
  if (vhint == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vrow = gab_message(gab, "row");
  if (vrow == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vcol_begin = gab_message(gab, "col\\begin");
  if (vcol_begin == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vcol_end = gab_message(gab, "col\\end");
  if (vcol_end == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vbyte_begin = gab_message(gab, "byte\\begin");
  if (vbyte_begin == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vbyte_end = gab_message(gab, "byte\\end");
  if (vbyte_end == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vthrd = gab_message(gab, "thread");
  if (vthrd == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value rec = gab_recordof(
      gab, vstatus, gab_string(gab, gab_status_names[err.status]), vsrc,
      gab_string(gab, err.src_name), vtok_offset, gab_number(args.tok), vtok_t,
      gab_string(gab, err.tok_name), vhint, gab_string(gab, hint), vrow,
      gab_number(err.row), vcol_begin, gab_number(err.col_begin), vcol_end,
      gab_number(err.col_end), vbyte_begin, gab_number(err.byte_begin),
      vbyte_end, gab_number(err.byte_end), vthrd, gab_number(args.wkid), );

  gab_assert(gab_reclen(rec) == 11,
             "Error record shall be constructed correctly");

  gab_gcunlock(gab);

  return rec;
}

gab_value single_errtos(struct gab_triple gab, gab_value err) {
  gab_value token_type = gab_mrecat(gab, err, "tok\\t");
  if (token_type == gab_cinvalid)
    return gab_cinvalid;

  gab_value srcname = gab_mrecat(gab, err, "src");
  if (srcname == gab_cinvalid)
    return gab_cinvalid;

  gab_value status = gab_mrecat(gab, err, "status");
  if (status == gab_cinvalid)
    return gab_cinvalid;

  gab_value hint = gab_mrecat(gab, err, "hint");
  if (hint == gab_cinvalid)
    return gab_cinvalid;

  gab_value vtoken = gab_mrecat(gab, err, "tok\\offset");
  if (vtoken == gab_cinvalid)
    return gab_cinvalid;

  gab_value vrow = gab_mrecat(gab, err, "row");
  if (vrow == gab_cinvalid)
    return gab_cinvalid;

  gab_value vcol_begin = gab_mrecat(gab, err, "col\\begin");
  if (vcol_begin == gab_cinvalid)
    return gab_cinvalid;

  gab_value vcol_end = gab_mrecat(gab, err, "col\\end");
  if (vcol_end == gab_cinvalid)
    return gab_cinvalid;

  gab_value vbyte_begin = gab_mrecat(gab, err, "byte\\begin");
  if (vbyte_begin == gab_cinvalid)
    return gab_cinvalid;

  gab_value vbyte_end = gab_mrecat(gab, err, "byte\\end");
  if (vbyte_end == gab_cinvalid)
    return gab_cinvalid;

  gab_value vwkid = gab_mrecat(gab, err, "thread");
  if (vwkid == gab_cinvalid)
    return gab_cinvalid;

  gab_assert(gab_valkind(vtoken) == kGAB_NUMBER,
             "tok\\offset shall be a number");
  uint64_t token = gab_valtou(vtoken);

  gab_assert(gab_valkind(vrow) == kGAB_NUMBER, "row shall be a number");
  uint64_t row = gab_valtou(vrow);

  gab_assert(gab_valkind(vcol_begin) == kGAB_NUMBER,
             "col\\begin shall be a number");
  uint64_t col_begin = gab_valtou(vcol_begin);

  gab_assert(gab_valkind(vcol_end) == kGAB_NUMBER,
             "col\\end shall be a number");
  uint64_t col_end = gab_valtou(vcol_end);

  gab_assert(gab_valkind(vbyte_begin) == kGAB_NUMBER,
             "byte\\begin shall be a number");
  uint64_t byte_begin = gab_valtou(vbyte_begin);

  gab_assert(gab_valkind(vbyte_end) == kGAB_NUMBER,
             "byte\\end shall be a number");
  uint64_t byte_end = gab_valtou(vbyte_end);

  gab_assert(gab_valkind(vwkid) == kGAB_NUMBER, "wkid shall be a number");
  uint64_t wkid = gab_valtou(vwkid);

  enum gab_status status_enum = GAB_OK;
  const char *statusname = gab_strdata(&status);
  for (int i = 0; i < LEN_CARRAY(gab_status_names); i++) {
    if (!strcmp(statusname, gab_status_names[i])) {
      status_enum = i;
      break;
    }
  }

  assert(status_enum != GAB_OK);

  struct errdetails e = {
      .token = token,
      .src_name = gab_strdata(&srcname),
      .status = status_enum,
      .tok_name = gab_strdata(&token_type),
      .byte_begin = byte_begin,
      .byte_end = byte_end,
      .col_begin = col_begin,
      .col_end = col_end,
      .row = row,
      .wkid = wkid,
  };

  const char *cstrhint = gab_strdata(&hint);

  int (*print_fn)(struct gab_triple, char **, size_t *, struct errdetails *,
                  const char *) = gab.flags & fGAB_ERR_STRUCTURED
                                      ? sprint_structured_err
                                      : sprint_pretty_err;

  for (size_t i = 128;; i <<= 1) {
    char buf[i];
    size_t n = i;
    char *cursor = buf;
    if (print_fn(gab, &cursor, &n, &e, cstrhint) >= 0) {
      gab_value newestr = gab_string(gab, buf);
      return newestr;
    }
  }

  return gab_nil;
}

const char *gab_errtocs(struct gab_triple gab, gab_value err) {
  assert(gab_valkind(err) == kGAB_RECORD);

  if (gab_valkind(err) != kGAB_RECORD)
    return nullptr;

  if (!gab_recisl(err)) {
    gab_value str;

    do {
      str = single_errtos(gab, err);

      switch (gab_yield(gab)) {
      case sGAB_TERM:
        gab_sigpropagate(gab);
        break;
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        continue;
      }
    } while (str == gab_cinvalid);

    assert(str != gab_nil);
    assert(gab_strlen(str) > 5);
    // Only works because this will never be a shortstr.
    // This is *not* an appropriate way to use
    // gab_strdata however, because if str *is* a shortstr,
    // the pointer would be pointing to our local variable str.
    // And then we would be returning a pointer to a local.
    return gab_strdata(&str);
  }

  int len = gab_reclen(err);

  if (!len)
    return nullptr;

  gab_value total_str = gab_cinvalid;
  do {
    total_str = gab_string(gab, "");
  } while (total_str == gab_cinvalid);

  assert(total_str != gab_cinvalid);
  if (total_str == gab_cinvalid)
    return nullptr;

  for (int i = len - 1; i >= 0; --i) {
    gab_value next_err = gab_uvrecat(err, i);
    gab_assert(gab_reclen(next_err) > 1, "WRONG RECLEN");
    assert(next_err != gab_nil);

    gab_value next_str;

    do {
      next_str = single_errtos(gab, next_err);

      switch (gab_yield(gab)) {
      case sGAB_TERM:
        gab_sigpropagate(gab);
        break;
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        continue;
      }
    } while (next_str == gab_cinvalid);

    assert(next_str != gab_nil);

    do {
      total_str = gab_strcat(gab, total_str, next_str);

      switch (gab_yield(gab)) {
      case sGAB_TERM:
        gab_sigpropagate(gab);
        break;
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        continue;
      }
    } while (total_str == gab_cinvalid);
    // if (total_str == gab_cinvalid)
    //   return nullptr;
  }

  gab_assert(gab_strlen(total_str) > 5,
             "Returned Str Length shall be greater than 5, so as not to "
             "invalidate pointer.");
  return gab_strdata(&total_str);
}

#define MODULE_SYMBOL "gab_lib"

a_char *match_resource(const char **roots, const struct gab_resource *res,
                       const char *package, const char *module,
                       const char **root) {
  for (int i = 0; roots[i] != nullptr; i++) {
    uint64_t r_len = strlen(roots[i]);
    uint64_t p_len = strlen(res->prefix);
    uint64_t s_len = strlen(res->suffix);
    uint64_t pkg_len = strlen(package);

    uint64_t mod_len = module ? strlen(module) : 0;

    /*
     * What is the best/most correct way to combine the package and module here?
     */
    uint64_t total_len = r_len + p_len + pkg_len + 1 + mod_len + s_len + 1;

    char buffer[total_len];

    memcpy(buffer, roots[i], r_len);

    memcpy(buffer + r_len, package, pkg_len);
    buffer[r_len + pkg_len++] = '/';

    memcpy(buffer + r_len + pkg_len, res->prefix, p_len);

    if (module) {
      memcpy(buffer + r_len + pkg_len + p_len, module, mod_len);
    }

    memcpy(buffer + r_len + pkg_len + p_len + mod_len, res->suffix, s_len + 1);

    assert(res->exister != nullptr);
    if (res->exister(buffer)) {

      if (root)
        *root = roots[i];

      return a_char_create(buffer, total_len);
    }
  }

  return nullptr;
}

/*
 *
 * Resolve MODULE within the given PACKAGE, starting at ROOTS and using
 * RESOURCES.
 *
 * For each of the roots, check if PACKAGE exists.
 *  - A package is folder or file, which exists *in* the root.
 *  - If MODULE is requested, try to resolve MODULE within PACKAGE via
 * resources.
 *  - If MODULE isn't requested, just try to resolve PACKAGE.
 *
 */

struct gab_module_res gab_mresolve(const char **roots,
                                   const struct gab_resource *resources,
                                   const char *package, const char *module) {

  char *colon = strchr(package, ':');
  if (colon) {
    // In this case, the package name implies a module.
    if (module) {
      return (struct gab_module_res){0};
    }

    module = colon + 1;

    *colon = '\0';
  }

  for (int i = 0; resources[i].prefix != nullptr; i++) {
    const struct gab_resource *res = resources + i;
    const char *root = nullptr;

    a_char *module_path = match_resource(roots, res, package, module, &root);

    if (module_path) {
      return (struct gab_module_res){
          .path = module_path,
          .resource = res,
          .root_path = root,
          // Skip the root. This should resolve to the full package + module
          // path.
          .package_path = module_path->data + strlen(root),
          // Skip the root and package. This should resolve to the module path,
          // relative to the package.
          .module_path = module_path->data + strlen(root) + strlen(package) + 1,
      };
    }
  }

  return (struct gab_module_res){0};
}

struct gab_module_res gab_resolve(struct gab_triple gab, const char *package,
                                  const char *module) {
  return gab_mresolve(gab.eg->resroots, gab.eg->res, package, module);
}

union gab_value_pair gab_use(struct gab_triple gab, struct gab_use_argt args) {
  gab.flags |= args.flags;

  const char *package = args.spackage_name;
  if (args.vpackage_name) {
    package = gab_strdata(&args.vpackage_name);
  }

  const char *module = args.smodule_name;
  if (args.vmodule_name) {
    module = gab_strdata(&args.vmodule_name);
  }

  if (gab_valkind(args.env) == kGAB_RECORD) {
    args.len = gab_reclen(args.env);
  }

  gab_assert(args.len != 0, "Args must not be zero. This is usually a bug.");

  const char *env_sargv[args.len];
  gab_value env_vsargv[args.len];
  gab_value env_vargv[args.len];

  if (gab_valkind(args.env) == kGAB_RECORD) {
    for (uint64_t i = 0; i < args.len; i++) {
      env_vsargv[i] = gab_ukrecat(args.env, i);
      assert(gab_valkind(env_vsargv[i]) == kGAB_BINARY);
      env_sargv[i] = gab_strdata(env_vsargv + i);
      env_vargv[i] = gab_uvrecat(args.env, i);
    }

    args.sargv = env_sargv;
    args.argv = env_vargv;
  }

  struct gab_module_res mod = gab_resolve(gab, package, module);

  if (mod.resource) {
    if (!(gab.flags & fGAB_USE_RELOAD)) {
      a_gab_value *cached = gab_segmodat(gab.eg, mod.path->data);

      if (cached != nullptr) {
        /* Skip the first argument, which is the module's data */

        return (union gab_value_pair){
            .status = gab_cvalid,
            .aresult = cached,
        };
      }
    }

    assert(mod.resource->loader != nullptr);
    union gab_value_pair result = mod.resource->loader(
        gab, mod.path->data, args.len, args.sargv, args.argv);

    if (result.status != gab_cvalid)
      return result;

    if (result.aresult->data[0] != gab_ok)
      return result;

    gab_segmodput(gab.eg, mod.path->data, result.aresult);

    return a_char_destroy(mod.path), result;
  }

  if (module)
    return gab_panicf(gab, "Module @:@ could not be found",
                      gab_string(gab, package), gab_string(gab, module));
  else
    return gab_panicf(gab, "Package @ could not be found",
                      gab_string(gab, package));
}

union gab_value_pair gab_run(struct gab_triple gab, struct gab_run_argt args) {
  union gab_value_pair fb = gab_arun(gab, args);

  if (fb.status != gab_cvalid)
    return fb;

  return gab_fibawait(gab, fb.vresult);
}

union gab_value_pair gab_arun(struct gab_triple gab, struct gab_run_argt args) {
  return gab_tarun(gab, -1, args);
}

union gab_value_pair gab_tarun(struct gab_triple gab, size_t tries,
                               struct gab_run_argt args) {
  gab.flags |= args.flags;

  if (gab.flags & fGAB_BUILD_CHECK)
    return (union gab_value_pair){.status = gab_cinvalid};

  gab_value fb = gab_fiber(gab, (struct gab_fiber_argt){
                                    .message = gab_message(gab, mGAB_CALL),
                                    .receiver = args.main,
                                    .flags = gab.flags,
                                    .argv = args.argv,
                                    .argc = args.len,
                                });

  if (fb == gab_cinvalid)
    return (union gab_value_pair){{gab_cinvalid}};

  gab_iref(gab, fb);
  gab_egkeep(gab.eg, fb);

  // TODO @runtime @perf: Push to local queue instead of always deferring
  // globally.

  // If we're *in* a valid worker we can push to the local queue.
  //   if (gab.wkid) {
  //     q_gab_value *q = &gab.eg->jobs[gab.wkid].queue;
  // #if cGAB_LOG_EG
  //     fprintf(stdout, "[WORKER %i] localqueue ", gab.wkid);
  //     gab_fprintf(stdout, "$\n", fb);
  // #endif
  //     if (q_gab_value_push(q, fb))
  //       return (union gab_value_pair){{gab_cvalid, fb}};
  //   }

#if cGAB_LOG_EG
  gab_fprintf(stderr, "[WORKER $] chnput $\n", gab_number(gab.wkid), fb);
#endif

  // Somehow check if the put will block, and create a job in that case.
  // Should check to see if the channel has takers waiting already.

  // TODO @cgab @runtime: When spawning a worker thread, try to donate all
  // queued fibers which have *never* been run. This is safe with our GC
  // strategy Fibers should not change type *back* to kGAB_FIBER after yielding.
  // They should remain kGAB_FIBERRUNNING, so that we know if a fiber has *ever*
  // been run on a thread. In order for our GC to be sound, VM Stacks *cannot*
  // migrate from thread to thread (After they may have been seen by the gc (ie,
  // run). We should also *skip* incrementing/decrementing stacks for Fibers
  // which have never been run in GC.

  if (!gab_wkspawn(gab, fb))
    if (gab_tchnput(gab, gab.eg->work_channel, fb, tries) == gab_ctimeout)
      return (union gab_value_pair){{gab_ctimeout, fb}};

  return (union gab_value_pair){{gab_cvalid, fb}};
}

union gab_value_pair gab_send(struct gab_triple gab,
                              struct gab_send_argt args) {
  union gab_value_pair fb = gab_asend(gab, args);

  if (fb.status != gab_cvalid)
    return fb;

  union gab_value_pair res = gab_fibawait(gab, fb.vresult);

  if (res.status != gab_cvalid)
    return res;

  gab_dref(gab, fb.vresult);

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = res.aresult,
  };
};

union gab_value_pair gab_asend(struct gab_triple gab,
                               struct gab_send_argt args) {
  gab.flags |= args.flags;

  gab_value fb = gab_fiber(gab, (struct gab_fiber_argt){
                                    .message = args.message,
                                    .receiver = args.receiver,
                                    .argv = args.argv,
                                    .argc = args.len,
                                    .flags = gab.flags,
                                });

  if (fb == gab_cinvalid)
    return (union gab_value_pair){{gab_cinvalid}};

  gab_iref(gab, fb);
  gab_egkeep(gab.eg, fb);

  // TODO @cgab @bug: These chnputs block, which is problematic.
  // I should really maybe have a queue for this.
  // These potentially block callers annoyingly long
  if (args.pin) {
    if (args.pin > gab.eg->len)
      return (union gab_value_pair){{gab_cinvalid}};

    if (!gab_jbisalive(gab, args.pin))
      return (union gab_value_pair){{gab_cinvalid}};

    if (gab.wkid == args.pin)
      q_gab_value_push(&gab.eg->jobs[args.pin].queue, fb);
    else
      gab_chnput(gab, gab.eg->jobs[args.pin].work_channel, fb);

  } else if (!gab_wkspawn(gab, fb)) {
    gab_chnput(gab, gab.eg->work_channel, fb);
  }

  return (union gab_value_pair){{gab_cvalid, fb}};
};

bool gab_sigterm(struct gab_triple gab) {
  while (!gab_signal(gab, sGAB_TERM, 1))
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return true;
    default:
      break;
    };

  return true;
}

bool gab_asigcoll(struct gab_triple gab) {
  return gab_signal(gab, sGAB_COLL, 1);
}

bool gab_sigcoll(struct gab_triple gab) {
  while (!gab_asigcoll(gab))
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      return true;
    case sGAB_TERM:
      return false;
    default:
      break;
    };

  return true;
}

// TODO @feat: Include *macros* as the first lookup here.
// TODO @bug: Macros can resolve at runtime. No bueno.
// I think that should be sufficient for executing macros.
struct gab_impl_rest gab_impl(struct gab_triple gab, gab_value message,
                              gab_value receiver) {
  gab_value messages = atomic_load(&gab.eg->macros);
  gab_value macro = gab_recat(messages, message);

  if (macro != gab_cundefined)
    return (struct gab_impl_rest){
        .messages = messages,
        .type = gab_cundefined,
        .as.spec = macro,
        .status = kGAB_IMPL_MACRO,
    };

  messages = gab_thisfibmsg(gab);
  gab_value specs = gab_recat(messages, message);

  // There are no specs for this message.
  // *maybe* this is a rec and property situation.
  // So jump straight there.
  if (specs == gab_cundefined)
    goto property;

  gab_value spec = gab_cundefined;
  gab_value type = receiver;

  /* Check if the receiver has a supertype, and if that supertype implments
   * the message. ie <gab.shape 0 1>*/
  if (gab_valhast(receiver)) {
    type = gab_valtype(gab, receiver);
    spec = gab_recat(specs, type);
    if (spec != gab_cundefined)
      return (struct gab_impl_rest){
          .messages = messages,
          .type = type,
          .as.spec = spec,
          kGAB_IMPL_TYPE,
      };
  }

  /* Check for the kind of the receiver. ie 'gab\record' */
  type = gab_type(gab, gab_valkind(receiver));
  spec = gab_recat(specs, type);
  if (spec != gab_cundefined)
    return (struct gab_impl_rest){
        .messages = messages,
        .type = type,
        .as.spec = spec,
        kGAB_IMPL_KIND,
    };

  /* Check for a default, generic implementation */
  /* Previously, this had a higher priority than
   * record properties - I don't remember why I made that change.
   *
   * Ahh, I remember the issue. The `Messages.specializations` record
   * is impossible to do anything with, because it is a record with a key
   * for every message in the system.
   */
  type = gab_cundefined;
  spec = gab_recat(specs, type);
  if (spec != gab_cundefined)
    return (struct gab_impl_rest){
        .messages = messages,
        .type = type,
        .as.spec = spec,
        kGAB_IMPL_GENERAL,
    };

  /* Check if the receiver is a record and has a matching property */
property:
  if (gab_valkind(receiver) == kGAB_RECORD) {
    type = gab_recshp(receiver);
    if (gab_rechas(receiver, message))
      return (struct gab_impl_rest){
          .messages = messages,
          .type = type,
          .as.offset = gab_recfind(receiver, message),
          kGAB_IMPL_PROPERTY,
      };
  }

  return (struct gab_impl_rest){.messages = messages, .status = kGAB_IMPL_NONE};
}

GAB_API gab_value gab_type(struct gab_triple gab, enum gab_kind k) {
  assert(k < kGAB_NKINDS);
  return gab.eg->types[k];
}

GAB_API struct gab_gc *gab_gc(struct gab_triple gab) { return &gab.eg->gc; }

GAB_API gab_value gab_thisfiber(struct gab_triple gab) {
  return q_gab_value_peek(&gab.eg->jobs[gab.wkid].queue);
}

GAB_API gab_value gab_thisfibmsg(struct gab_triple gab) {
  return atomic_load(&gab.eg->messages);
  /*gab_value fiber = gab_thisfiber(gab);*/
  /**/
  /*if (fiber == gab_cinvalid)*/
  /*  return gab_atmat    (gab, gab.eg->messages);*/
  /**/
  /*struct gab_ofiber *f =
        GAB_VAL_TO_FIBER(fiber);*/
  /*return gab_atmat(gab, f->messages);*/
}

GAB_API inline bool gab_sigwaiting(struct gab_triple gab) {
  struct gab_sig sig = atomic_load_explicit(&gab.eg->sig, memory_order_acquire);
  return sig.schedule == gab.wkid;
}

GAB_API inline bool gab_signaling(struct gab_triple gab) {
  /*printf("SCHEDULE: %i, SIGNALING: %d\n", gab.eg->sig.schedule,
   * gab.eg->sig.schedule >= 0);*/
  struct gab_sig sig = atomic_load_explicit(&gab.eg->sig, memory_order_acquire);
  return sig.signal;
}

GAB_API inline bool gab_signext(struct gab_triple gab, int wkid) {
  for (;;) {
    gab_busywait(gab);

    struct gab_sig sig = atomic_load(&gab.eg->sig);

    if (!sig.mask)
      return true;

#if cGAB_LOG_EG
    fprintf(stderr, "[WORKER %i] TRY NEXT %i: against %b\n", gab.wkid, wkid,
            sig.mask);
#endif

    assert(sig.signal > 0);

    // Wrap around the number of jobs. Since
    // The 0th job is the GC job, we will wrap around
    // and begin the gc last.
    if (wkid >= gab.eg->len) {
      struct gab_sig next = {
          .mask = sig.mask,
          .schedule = 0,
          .signal = sig.signal,
      };

      assert(next.signal != sGAB_IGN);

      // cnd_signal(&gab.eg->gc_cnd);

      if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
        return true;
      else
        continue;
    }

    // If the worker we're signalling for isn't alive,
    // try to skip it.
    if (!(sig.mask & (1 << wkid))) {
      uint32_t shifted = sig.mask >> wkid;
      uint32_t nxt_open = shifted ? __builtin_ctzl(shifted) : 0;
      uint32_t n = nxt_open ? wkid + nxt_open : 0;

      assert(sig.mask & (1 << n));

      struct gab_sig next = {
          .mask = sig.mask,
          .schedule = n,
          .signal = sig.signal,
      };

      uint32_t last_job = n ? n : gab.eg->len;

#if cGAB_LOG_GC
      fprintf(stderr, "[WORKER %i] (%b) SKIPPING %u to %u\n", gab.wkid,
              sig.mask, wkid, last_job);
#endif

      // Ugly way of incrementing epoch for not-alive jobs.
      if (sig.signal == sGAB_COLL)
        for (uint32_t i = wkid; i < last_job; i++) {
          gab_assert(!(sig.mask & (1 << i)),
                     "Shall not skip a worker which is alive.");
#if cGAB_LOG_GC
          fprintf(stderr, "[WORKER %i] EPOCHINC via SKIP\n", i);
#endif
          gab.eg->jobs[i].epoch++;
        }

      assert(next.signal != sGAB_IGN);

      if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
        return true;
      else
        continue;
    }

    if (sig.schedule < (int8_t)wkid) {
      struct gab_sig next = {
          .mask = sig.mask,
          .schedule = wkid,
          .signal = sig.signal,
      };

      assert(next.signal != sGAB_IGN);
      if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
        return true;
      else
        continue;
    }

    assert(sig.signal != sGAB_IGN);
    if (sig.schedule == wkid)
      return true;
    else
      continue;
  }
}

GAB_API inline bool gab_sigclear(struct gab_triple gab) {
  for (;;) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    struct gab_sig exp = (struct gab_sig){sig.mask, 0, sig.signal};
    struct gab_sig next = (struct gab_sig){sig.mask, -1, sGAB_IGN};
    if (atomic_compare_exchange_weak(&gab.eg->sig, &exp, next)) {
#if cGAB_LOG_EG
      fprintf(stderr, "[WORKER %i] CLEAR %i\n", gab.wkid, sig.signal);
#endif
      return true;
    }
  }
}

GAB_API inline bool gab_signal(struct gab_triple gab, enum gab_signal s,
                               int wkid) {
  assert(wkid < gab.eg->len);
  assert(wkid > 0);

  for (;;) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    struct gab_sig none = {sig.mask, -1, sGAB_IGN};

    if (sig.signal == s)
      return true;

    if (sig.schedule == gab.wkid) {
      switch (sig.signal) {
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      case sGAB_TERM:
        return false;
      default:
        break;
      }
    }

    if (atomic_compare_exchange_weak(&gab.eg->sig, &none,
                                     ((struct gab_sig){sig.mask, -2, s}))) {
#if cGAB_LOG_EG
      fprintf(stderr, "[WORKER %i] SIGNAL %i TO %b\n", gab.wkid, s, sig.mask);
#endif

      /*
       * Depending on the signal we have successfully begun,
       * we may need to do some different work.
       *
       * For collections, we need to signal a wakeup (and therefore lock)
       * of the gc worker and gc_mtx. Then we wait for an ack from that thread.
       *
       * For other signals, we can just begin signalling - no setup work is
       * required.
       */
      // switch (s) {
      // case sGAB_COLL:
      mtx_lock(&gab.eg->gc_mtx);
      cnd_signal(&gab.eg->gc_cnd);
      mtx_unlock(&gab.eg->gc_mtx);

      for (;;) {
        struct gab_sig sig = atomic_load(&gab.eg->sig);

        /* acknowledgment received */
        if (sig.schedule == -1)
          break;
      }
      return gab_signext(gab, wkid);
      // default:
      //   atomic_store(&gab.eg->sig, ((struct gab_sig){sig.mask, -1, s}));
      //   return gab_signext(gab, wkid);
      // }
    }
  }
};

// -- GC --

static inline int32_t epochget(struct gab_triple gab) {
  return gab.eg->jobs[gab.wkid].epoch % GAB_GCNEPOCHS;
}

static inline int32_t epochgetlast(struct gab_triple gab) {
  return (gab.eg->jobs[gab.wkid].epoch - 1) % GAB_GCNEPOCHS;
}

static inline void epochinc(struct gab_triple gab) {
#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] EPOCHINC\t%i\n", gab.wkid, epochget(gab));
#endif
  gab.eg->jobs[gab.wkid].epoch++;
}

static inline struct gab_obj **bufdata(struct gab_triple gab, uint8_t b,
                                       uint8_t wkid, uint8_t epoch) {
  assert(epoch < GAB_GCNEPOCHS);
  assert(b < kGAB_NBUF);
  assert(wkid < gab.eg->len);
  return gab.eg->jobs[wkid].buffers[b][epoch].data;
}

static inline uint64_t buflen(struct gab_triple gab, uint8_t b, uint8_t wkid,
                              uint8_t epoch) {
  assert(epoch < GAB_GCNEPOCHS);
  assert(b < kGAB_NBUF);
  assert(wkid < gab.eg->len);
  return gab.eg->jobs[wkid].buffers[b][epoch].len;
}

void gab_gcloglen(struct gab_triple gab) {
  for (int i = 0; i < gab.eg->len; i++) {
    for (int j = 0; j < kGAB_NBUF; j++) {
      for (int k = 0; k < GAB_GCNEPOCHS; k++) {
        uint64_t len = buflen(gab, j, i, k);
        if (len)
          fprintf(stderr, "[WORKER %i] BUF %i(%i) [%lu]\n", i, j, k, len);
      }
    }
  }
}

void gab_gcassertdone(struct gab_triple gab) {
  for (int i = 0; i < gab.eg->len; i++) {
    for (int j = 0; j < kGAB_NBUF; j++) {
      for (int k = 0; k < GAB_GCNEPOCHS; k++) {
        assert(buflen(gab, j, i, k) == 0);
      }
    }
  }
}

static inline void bufpush(struct gab_triple gab, uint8_t b, uint8_t wkid,
                           uint8_t epoch, struct gab_obj *o) {
  assert(epoch < GAB_GCNEPOCHS);
  assert(b < kGAB_NBUF);
  assert(wkid < gab.eg->len);
  uint64_t len = buflen(gab, b, wkid, epoch);
  assert(len < cGAB_GC_MOD_BUFF_MAX);

  struct gab_obj **buf = bufdata(gab, b, wkid, epoch);
  buf[len] = o;
  gab.eg->jobs[wkid].buffers[b][epoch].len = len + 1;
}

static inline void bufclear(struct gab_triple gab, uint8_t b, uint8_t wkid,
                            uint8_t epoch) {
  assert(epoch < GAB_GCNEPOCHS);
  assert(b < kGAB_NBUF);
  assert(wkid < gab.eg->len);
  gab.eg->jobs[wkid].buffers[b][epoch].len = 0;
}

static inline uint64_t do_increment(struct gab_gc *gc, struct gab_obj *obj) {
  if (__gab_unlikely(obj->references == INT8_MAX)) {
    uint64_t rc = d_gab_obj_read(&gc->overflow_rc, obj);

    d_gab_obj_insert(&gc->overflow_rc, obj, rc + 1);

    return rc + 1;
  }

  return obj->references++;
}

static inline uint64_t do_decrement(struct gab_gc *gc, struct gab_obj *obj) {
  if (__gab_unlikely(obj->references == INT8_MAX)) {
    uint64_t rc = d_gab_obj_read(&gc->overflow_rc, obj);

    if (__gab_unlikely(rc == UINT8_MAX)) {
      d_gab_obj_remove(&gc->overflow_rc, obj);
      return obj->references--;
    }

    d_gab_obj_insert(&gc->overflow_rc, obj, rc - 1);
    return rc - 1;
  }

  gab_assert(obj->references != 0,
             "Shall not underflow reference count of object with kind %d.",
             obj->kind);
  return obj->references--;
}

#if cGAB_LOG_GC
#define queue_decrement(gab, obj)                                              \
  (__queue_decrement(gab, obj, __FUNCTION__, __LINE__))

void __queue_decrement(struct gab_triple gab, struct gab_obj *obj,
                       const char *func, int line) {
#else
void queue_decrement(struct gab_triple gab, struct gab_obj *obj) {
#endif
  int32_t e = epochget(gab);

  while (buflen(gab, kGAB_BUF_DEC, gab.wkid, e) >= cGAB_GC_MOD_BUFF_MAX) {
    // Try to signal a collection
    gab_asigcoll(gab);

    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      // In the case where a decrement is missed to this value
      // because we must handle the terminate signal:
      // Simply store this value in the engine's scratch buffer.
      // It will be decremented as the engine is cleaned up.
      gab_egkeep(gab.eg, __gab_obj(obj));
      return;
    default:
      break;
    }

    e = epochget(gab);
  }

  bufpush(gab, kGAB_BUF_DEC, gab.wkid, e, obj);

#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] QDEC\t%i\t%p\t%i\t%s:%i\n", gab.wkid,
          epochget(gab), obj, obj->references, func, line);
#endif
}

void queue_increment(struct gab_triple gab, struct gab_obj *obj) {
  int32_t e = epochget(gab);

  while (buflen(gab, kGAB_BUF_INC, gab.wkid, e) >= cGAB_GC_MOD_BUFF_MAX) {
    // Try to signal a collection
    gab_asigcoll(gab);

    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      // In the case where an increment is missed to this value
      // because we must handle the terminate signal:
      // Immediately perform an increment. This is safe as it can't result
      // in destroying the object.
      // Give the object to the scratch buffer for resolving later.
      do_increment(&gab.eg->gc, obj);
      gab_egkeep(gab.eg, __gab_obj(obj));
      return;
    default:
      break;
    }

    e = epochget(gab);
  }

  bufpush(gab, kGAB_BUF_INC, gab.wkid, e, obj);

#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] QINC\t%i\t%p\t%d\n", gab.wkid, epochget(gab),
          obj, obj->references);
#endif
}

void queue_destroy(struct gab_triple gab, struct gab_obj *obj) {
  if (GAB_OBJ_IS_BUFFERED(obj))
    return;

  GAB_OBJ_BUFFERED(obj);

  v_gab_obj_push(&gab.eg->gc.dead, obj);

  assert(obj->references == 0);

#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] QDEAD\t%i\t%p\t%d\n", gab.wkid, epochget(gab),
          obj, obj->references);
#endif
}

static inline void for_buf_do(uint8_t b, uint8_t wkid, uint8_t epoch,
                              void (*fnc)(struct gab_triple gab,
                                          struct gab_obj *obj),
                              struct gab_triple gab) {
  struct gab_obj **buf = bufdata(gab, b, wkid, epoch);
  uint64_t len = buflen(gab, b, wkid, epoch);
  assert(len <= cGAB_GC_MOD_BUFF_MAX);

#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] FORDO\t%i\t(%lu / %i)\n", wkid, epoch, len,
          cGAB_GC_MOD_BUFF_MAX);
#endif

  for (uint64_t i = 0; i < len; i++) {
    struct gab_obj *obj = buf[i];

#if cGAB_LOG_GC
    if (GAB_OBJ_IS_FREED(obj)) {
      fprintf(stderr, "UAF\t%p\n", obj);
      exit(1);
    }
#endif

    fnc(gab, obj);
  }

  // Sanity check that buffer hasn't been modified while operating over buffer
#if cGAB_LOG_GC
  if (len != buflen(gab, b, wkid, epoch)) {
    fprintf(stderr, "INVALID BUFMOD: %d, %i, %i, %lu vs %li\n", b, wkid, epoch,
            len, buflen(gab, b, wkid, epoch));
    exit(1);
  }
#endif
  assert(len == buflen(gab, b, wkid, epoch));
}

static inline void for_child_do(struct gab_obj *obj,
                                void (*fnc)(struct gab_triple gab,
                                            struct gab_obj *obj),
                                struct gab_triple gab) {
#if cGAB_LOG_GC
  fprintf(stderr, "RECURSE\t%i\t%p\t%i\n", epochget(gab), obj, obj->references);
#endif
  switch (obj->kind) {
  default:
    break;

  case kGAB_NATIVE: {
    struct gab_onative *ntv = (struct gab_onative *)obj;

    if (gab_valiso(ntv->name))
      fnc(gab, gab_valtoo(ntv->name));

    break;
  }

  case kGAB_PROTOTYPE: {
    struct gab_oprototype *prt = (struct gab_oprototype *)obj;

    assert(gab_valiso(prt->env));
    fnc(gab, gab_valtoo(prt->env));

    break;
  }

  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE:
  case kGAB_FIBER: {
    struct gab_ofiber *fib = (struct gab_ofiber *)obj;

    for (uint64_t i = 0; i < fib->len; i++) {
      gab_value o = fib->data[i];

      if (gab_valiso(o))
        fnc(gab, gab_valtoo(o));
    }

    break;
  }

  case kGAB_BOX: {
    struct gab_obox *box = (struct gab_obox *)obj;

    if (gab_valiso(box->type))
      fnc(gab, gab_valtoo(box->type));

    break;
  }

  case (kGAB_BLOCK): {
    struct gab_oblock *b = (struct gab_oblock *)obj;

    for (int i = 0; i < b->nupvalues; i++) {
      if (gab_valiso(b->upvalues[i]))
        fnc(gab, gab_valtoo(b->upvalues[i]));
    }

    break;
  }

  case kGAB_SHAPE:
  case kGAB_SHAPELIST: {
    struct gab_oshape *s = (struct gab_oshape *)obj;

    for (uint64_t i = 0; i < s->len; i++) {
      gab_value v = s->keys[i];
      if (gab_valiso(v))
        fnc(gab, gab_valtoo(v));
    }

    /*for (uint64_t i = 0; i < s->transitions.len; i++) {*/
    /*  gab_value v = v_gab_value_val_at(&s->transitions, i);*/
    /*  if (gab_valiso(v))*/
    /*    fnc(gab, gab_valtoo(v));*/
    /*}*/

    break;
  }

  case kGAB_RECORD: {
    struct gab_orec *rec = (struct gab_orec *)obj;
    uint64_t len = (rec->len);

    for (uint64_t i = 0; i < len; i++)
      if (gab_valiso(rec->data[i]))
        fnc(gab, gab_valtoo(rec->data[i]));

    break;
  }

  case kGAB_RECORDNODE: {
    struct gab_orecnode *rec = (struct gab_orecnode *)obj;
    uint64_t len = rec->len;

    for (uint64_t i = 0; i < len; i++)
      if (gab_valiso(rec->data[i]))
        fnc(gab, gab_valtoo(rec->data[i]));

    break;
  }
  }
}

static inline void dec_obj_ref(struct gab_triple gab, struct gab_obj *obj);

#if cGAB_LOG_GC
#define destroy(gab, obj) _destroy(gab, obj, __FUNCTION__, __LINE__)
static inline void _destroy(struct gab_triple gab, struct gab_obj *obj,
                            const char *func, int line) {
#else
static inline void destroy(struct gab_triple gab, struct gab_obj *obj) {
#endif

#if cGAB_LOG_GC
  if (GAB_OBJ_IS_FREED(obj)) {
    fprintf(stderr, "[WORKER %i] DFREE\t%p\t%s:%i\n", gab.wkid, obj, func,
            line);
    exit(1);
  } else {
    fprintf(stderr, "[WORKER %i] FREE\t%i\t%p\t%i\t%s:%d\n", gab.wkid,
            epochget(gab), obj, obj->references, func, line);
  }
  GAB_OBJ_FREED(obj);
  gab_objdestroy(gab, obj);
  gab_egalloc(gab, obj, 0);
#else
  gab_assert(obj->references == 0,
             "Shall only destroy objects with 0 references, not %i on kind %d",
             obj->references, obj->kind);
  gab_objdestroy(gab, obj);
  gab_egalloc(gab, obj, 0);

#endif
}

static inline void dec_obj_ref(struct gab_triple gab, struct gab_obj *obj) {
#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] DEC\t%i\t%p\t%d\n", gab.wkid, epochget(gab), obj,
          obj->references - 1);
#endif

  do_decrement(&gab.eg->gc, obj);

  if (obj->references == 0) {
    if (!GAB_OBJ_IS_NEW(obj))
      for_child_do(obj, dec_obj_ref, gab);

    queue_destroy(gab, obj);
  }
}

static inline void inc_obj_ref(struct gab_triple gab, struct gab_obj *obj) {
#if cGAB_LOG_GC
  fprintf(stderr, "INC\t%i\t%p\t%d\n", epochget(gab), obj, obj->references + 1);
#endif

  do_increment(&gab.eg->gc, obj);

  if (GAB_OBJ_IS_NEW(obj)) {
#if cGAB_LOG_GC
    fprintf(stderr, "NEW\t%i\t%p\n", epochget(gab), obj);
#endif
    GAB_OBJ_NOT_NEW(obj);
    for_child_do(obj, inc_obj_ref, gab);
  }
}

#if cGAB_LOG_GC
void __gab_niref(struct gab_triple gab, uint64_t stride, uint64_t len,
                 gab_value *values, const char *func, int line) {
#else
void gab_niref(struct gab_triple gab, uint64_t stride, uint64_t len,
               gab_value *values) {
#endif
  for (uint64_t i = 0; i < len; i++) {
    gab_value value = values[i * stride];

#if cGAB_LOG_GC
    __gab_iref(gab, value, func, line);
#else
    gab_iref(gab, value);
#endif
  }
}

#if cGAB_LOG_GC
void __gab_ndref(struct gab_triple gab, uint64_t stride, uint64_t len,
                 gab_value *values, const char *func, int line) {
#else
void gab_ndref(struct gab_triple gab, uint64_t stride, uint64_t len,
               gab_value *values) {
#endif

  for (uint64_t i = 0; i < len; i++) {
    gab_value value = values[i * stride];

#if cGAB_LOG_GC
    __gab_dref(gab, value, func, line);
#else
    gab_dref(gab, value);
#endif
  }
}

#if cGAB_LOG_GC
gab_value __gab_iref(struct gab_triple gab, gab_value value, const char *func,
                     int32_t line) {
#else
gab_value gab_iref(struct gab_triple gab, gab_value value) {
#endif
  /*
   * If the value is not a heap object, then do nothing
   */
  if (!gab_valiso(value))
    return value;

  struct gab_obj *obj = gab_valtoo(value);

#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] IREF\t%i\t%p\t%d\t%s:%i\n", gab.wkid,
          epochget(gab), obj, obj->references, func, line);
#endif

  queue_increment(gab, obj);

#if cGAB_DEBUG_GC
  gab_sigcoll(gab);
#endif

  return value;
}

#if cGAB_LOG_GC
gab_value __gab_dref(struct gab_triple gab, gab_value value, const char *func,
                     int32_t line) {
#else
gab_value gab_dref(struct gab_triple gab, gab_value value) {
#endif
  /*
   * If the value is not a heap object, then do nothing
   */
  if (!gab_valiso(value))
    return value;

  struct gab_obj *obj = gab_valtoo(value);

#if cGAB_DEBUG_GC
  gab_sigcoll(gab);
#endif

#if cGAB_LOG_GC
  if (GAB_OBJ_IS_NEW(obj)) {
    fprintf(stderr, "[WORKER %i] NEWDREF\t%i\t%p\t%d\t%s:%i\n", gab.wkid,
            epochget(gab), obj, obj->references, func, line);
  } else {
    fprintf(stderr, "[WORKER %i] DREF\t%i\t%p\t%d\t%s:%i\n", gab.wkid,
            epochget(gab), obj, obj->references, func, line);
  }
#endif

#if cGAB_LOG_GC
  __queue_decrement(gab, obj, func, line);
#else
  queue_decrement(gab, obj);
#endif

  return value;
}

void gab_gccreate(struct gab_triple gab) {
  d_gab_obj_create(&gab.eg->gc.overflow_rc, 8);
  v_gab_obj_create(&gab.eg->gc.dead, 8);

  for (int i = 0; i < gab.eg->len; i++) {
    for (int b = 0; b < kGAB_NBUF; b++) {
      for (int e = 0; e < GAB_GCNEPOCHS; e++) {
        bufclear(gab, b, i, e);
      }
    }
  }
};

void gab_gcdestroy(struct gab_triple gab) {
  d_gab_obj_destroy(&gab.eg->gc.overflow_rc);
  v_gab_obj_destroy(&gab.eg->gc.dead);
  gab_jbunalive(gab, 0);
}

static inline void collect_dead(struct gab_triple gab) {
  while (gab.eg->gc.dead.len)
    destroy(gab, v_gab_obj_pop(&gab.eg->gc.dead));
}

void gab_gclock(struct gab_triple gab) {
  struct gab_job *wk = gab.eg->jobs + gab.wkid;
  assert(wk->locked < UINT32_MAX);
  wk->locked += 1;
}

/*
 * There was a bug where objects were beging freed *while they were in the locke
 * queue*. This was resolved by marking locked objects as *buffered* until they
 * are unlocked.
 */
void gab_gcunlock(struct gab_triple gab) {
  struct gab_job *wk = gab.eg->jobs + gab.wkid;
  assert(wk->locked > 0);
  wk->locked -= 1;

  if (!wk->locked) {
    for (uint64_t i = 0; i < wk->lock_keep.len; i++)
      GAB_OBJ_NOT_BUFFERED(gab_valtoo(v_gab_value_val_at(&wk->lock_keep, i)));

    gab_ndref(gab, 1, wk->lock_keep.len, wk->lock_keep.data);

    wk->lock_keep.len = 0;
  }
}

void processincrements(struct gab_triple gab, int32_t epoch) {
#if cGAB_LOG_GC
  fprintf(stderr, "IEPOCH\t%i\n", epoch);
#endif

  for (uint8_t wkid = 0; wkid < gab.eg->len; wkid++) {
    // For the stack and increment buffers, increment the object
    for_buf_do(kGAB_BUF_STK, wkid, epoch, inc_obj_ref, gab);
    for_buf_do(kGAB_BUF_INC, wkid, epoch, inc_obj_ref, gab);
    // Reset the length of the inc buffer for this worker
    // Leave the stack buffer to be cleared in next epoch by decrement.
    bufclear(gab, kGAB_BUF_INC, wkid, epoch);
  }
#if cGAB_LOG_GC
  fprintf(stderr, "IEPOCH!\t%i\n", epoch);
#endif
}

void processdecrements(struct gab_triple gab, int32_t epoch) {
#if cGAB_LOG_GC
  fprintf(stderr, "DEPOCH\t%i\n", epoch);
#endif

  for (uint8_t wkid = 0; wkid < gab.eg->len; wkid++) {
    // For the stack and increment buffers, decrement the object
    for_buf_do(kGAB_BUF_STK, wkid, epoch, dec_obj_ref, gab);
    for_buf_do(kGAB_BUF_DEC, wkid, epoch, dec_obj_ref, gab);
    // Reset the length of the dec buffer for this worker
    bufclear(gab, kGAB_BUF_STK, wkid, epoch);
    bufclear(gab, kGAB_BUF_DEC, wkid, epoch);
  }

#if cGAB_LOG_GC
  fprintf(stderr, "DEPOCH!\t%i\n", epoch);
#endif
}

void processepoch(struct gab_triple gab, int32_t e) {
  struct gab_job *wk = &gab.eg->jobs[gab.wkid];

#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] PEPOCH\t%i\n", gab.wkid, e);
#endif

  if (q_gab_value_is_empty(&wk->queue))
    goto fin;

#if cGAB_LOG_GC
  fprintf(stderr, "QUEUENOTEMPTY\t%lu\t%lu\t%lu\n", wk->queue.head,
          wk->queue.tail, wk->queue.size);
#endif

  for (size_t idx = wk->queue.head; idx != wk->queue.tail; idx++) {
    gab_value fiber = wk->queue.data[idx & (wk->queue.cap - 1)];

#if cGAB_LOG_GC
    fprintf(stderr, "PFIBER\t%i\t%i\t%lu\n", e, gab.wkid, idx);
#endif

    assert(gab_valkind(fiber) == kGAB_FIBER ||
           gab_valkind(fiber) == kGAB_FIBERRUNNING);

    struct gab_ofiber *fb = GAB_VAL_TO_FIBER(fiber);

    struct gab_vm *vm = &fb->vm;

    assert(vm->sp >= vm->sb);
    uint64_t stack_size = vm->sp - vm->sb;

    assert(stack_size < cGAB_STACK_MAX);
    assert(stack_size + wk->lock_keep.len + 2 < cGAB_GC_MOD_BUFF_MAX);

    bufpush(gab, kGAB_BUF_STK, gab.wkid, e, gab_valtoo(fiber));

    for (uint64_t i = 0; i < stack_size; i++) {
      if (gab_valiso(vm->sb[i])) {
        struct gab_obj *o = gab_valtoo(vm->sb[i]);
#if cGAB_LOG_GC
        fprintf(stderr, "SAVESTK\t%i\t%p\t%d\n", epochget(gab), (void *)o,
                o->kind);
#endif
        bufpush(gab, kGAB_BUF_STK, gab.wkid, e, o);
      }
    }
  }

fin:
  epochinc(gab);
#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] PEPOCH!\t%i\n", gab.wkid, epochget(gab));
#endif
}

void assert_workers_have_epoch(struct gab_triple gab, int32_t e) {
  for (uint64_t i = 1; i < gab.eg->len; i++) {
    int32_t this_e = epochget((struct gab_triple){gab.eg, .wkid = i});
    gab_assert(this_e == e, "Expected worker %i to have epoch %i. Saw: %i.\n",
               i, e, this_e);
  }
}

#if cGAB_LOG_GC
void __gab_gcepochnext(struct gab_triple gab, const char *func, int line) {

  fprintf(stderr, "EPOCH\t%i\t%i\t%s:%i\n", epochget(gab), gab.wkid, func,
          line);
#else
void gab_gcepochnext(struct gab_triple gab) {
#endif
  if (gab.wkid > 0)
    processepoch(gab, epochget(gab));
}

void gab_gcdocollect(struct gab_triple gab) {
  assert(gab.wkid == 0);

  int32_t epoch = epochget(gab);
  int32_t last = epochgetlast(gab);

  assert(epoch != last);

  processepoch(gab, epoch);

  /**
   * Get this once. As collection is asynchronous,
   * the engine messages records is liable to change
   * as we're collecting. Just save the snapshot
   * of it now.
   */
  gab.eg->gc.msg[epoch] = atomic_load(&gab.eg->messages);
  gab.eg->gc.mac[epoch] = atomic_load(&gab.eg->macros);

  gab_value messages = gab.eg->gc.msg[epoch];
  gab_value macros = gab.eg->gc.mac[epoch];

  gab_value last_messages = gab.eg->gc.msg[last];
  gab_value last_macros = gab.eg->gc.mac[last];

#if cGAB_LOG_GC
  fprintf(stderr, "CEPOCH %i (last: %i, raw: %i)\n", epoch, last,
          gab.eg->jobs[gab.wkid].epoch);
#endif

  int32_t expected_e = (gab.eg->jobs[gab.wkid].epoch) % 3;
  assert_workers_have_epoch(gab, expected_e);

  if (gab_valiso(messages))
    inc_obj_ref(gab, gab_valtoo(messages));

  if (gab_valiso(macros))
    inc_obj_ref(gab, gab_valtoo(macros));

  processincrements(gab, epoch);

  if (gab_valiso(last_messages))
    dec_obj_ref(gab, gab_valtoo(last_messages));

  if (gab_valiso(last_macros))
    dec_obj_ref(gab, gab_valtoo(last_macros));

  processdecrements(gab, last);

  collect_dead(gab);

#if cGAB_LOG_GC
  fprintf(stderr, "CEPOCH! %i\n", epoch);
#endif

  expected_e = (gab.eg->jobs[gab.wkid].epoch) % 3;
  assert_workers_have_epoch(gab, expected_e);
}

// -- lexing --

bool can_start_operator(uint8_t c) {
  switch (c) {
  case '!':
  case '$':
  case '%':
  case '^':
  case '*':
  case '/':
  case '+':
  case '-':
  case '&':
  case '|':
  case '=':
  case '<':
  case '>':
  case '?':
  case '~':
  case '@':
    return true;
  default:
    return false;
  }
}

bool can_continue_operator(uint8_t c) {
  switch (c) {
  default:
    return can_start_operator(c);
  }
}

bool can_start_symbol(uint8_t c) { return isalpha(c) || c == '_'; }

bool can_continue_symbol(uint8_t c) {
  return can_start_symbol(c) || isdigit(c) || c == '\\';
}

bool can_continue_hex(uint8_t c) {
  if (isdigit(c))
    return true;

  if (c >= 'a' && c <= 'f')
    return true;

  if (c >= 'A' && c <= 'F')
    return true;

  return false;
}

bool is_comment(uint8_t c) { return c == '#'; }

typedef struct gab_lx {
  char *cursor;
  char *row_start;
  uint64_t row;
  uint64_t col;

  uint8_t status;

  struct gab_src *source;

  s_char current_row_comment;
  s_char current_row_src;
  s_char current_token_src;
} gab_lx;

static void advance(gab_lx *self) {
  self->cursor++;
  self->col++;
  self->current_token_src.len++;
  self->current_row_src.len++;
}

static void start_row(gab_lx *self) {
  self->current_row_comment = (s_char){0};
  self->current_row_src.data = self->cursor;
  self->current_row_src.len = 0;
  self->col = 0;
  self->row++;
}

static void start_token(gab_lx *self) {
  self->current_token_src.data = self->cursor;
  self->current_token_src.len = 0;
}

static void finish_row(gab_lx *self) {
  if (self->current_row_src.len &&
      self->current_row_src.data[self->current_row_src.len - 1] == '\n')
    self->current_row_src.len--;

  v_s_char_push(&self->source->lines, self->current_row_src);

  start_row(self);
}

void gab_lexcreate(gab_lx *self, struct gab_src *src) {
  memset(self, 0, sizeof(gab_lx));

  self->source = src;
  self->cursor = src->source->data;
  self->row_start = src->source->data;

  v_gab_value_push(&src->constants, gab_nil);
  v_gab_value_push(&src->constants, gab_false);
  v_gab_value_push(&src->constants, gab_true);
  v_gab_value_push(&src->constants, gab_ok);
  v_gab_value_push(&src->constants, gab_err);
  v_gab_value_push(&src->constants, gab_none);

  d_uint64_t_create(&src->node_begin_toks, 64);
  d_uint64_t_create(&src->node_end_toks, 64);

  start_row(self);
}

static inline int peek(gab_lx *self) { return *self->cursor; }

static inline int peek_next(gab_lx *self) { return *(self->cursor + 1); }

static inline gab_token lexer_error(gab_lx *self, enum gab_status s) {
  self->status = s;
  return TOKEN_ERROR;
}

typedef struct keyword {
  const char *literal;
  gab_token token;
} keyword;

const keyword keywords[] = {
    {

        "do",
        TOKEN_DO,
    },
    {
        "end",
        TOKEN_END,
    },
};

gab_token string(gab_lx *self) {
  uint8_t start = peek(self);
  uint8_t stop = start == '"' ? '"' : '\'';

  do {
    advance(self);

    if (peek(self) == '\0')
      return lexer_error(self, GAB_MALFORMED_STRING);

    if (start != '"')
      if (peek(self) == '\n')
        return lexer_error(self, GAB_MALFORMED_STRING);

  } while (peek(self) != stop);

  advance(self);
  return start == '"' ? TOKEN_DOUBLESTRING : TOKEN_SINGLESTRING;
}

gab_token operator(gab_lx *self) {
  while (can_continue_operator(peek(self)))
    advance(self);

  if (peek(self) == ':')
    return advance(self), TOKEN_MESSAGE;

  return TOKEN_OPERATOR;
}

gab_token symbol(gab_lx *self) {
  while (can_continue_symbol(peek(self)))
    advance(self);

  if (peek(self) == ':')
    return advance(self), TOKEN_MESSAGE;

  for (int i = 0; i < sizeof(keywords) / sizeof(keyword); i++) {
    keyword k = keywords[i];
    s_char lit = s_char_create(k.literal, strlen(k.literal));
    if (s_char_match(self->current_token_src, lit)) {
      return k.token;
    }
  }

  return TOKEN_SYMBOL;
}

gab_token integer(gab_lx *self) {
  while (isdigit(peek(self)))
    advance(self);

  return TOKEN_NUMBER;
}

bool isexponent(char c) { return isdigit(c) || c == '+' || c == '-'; }

gab_token decimal(gab_lx *self) {
  if (integer(self) == TOKEN_ERROR)
    return TOKEN_ERROR;

  // Decimal Exponent
  if (peek(self) == 'e' && isexponent(peek_next(self)))
    return advance(self), advance(self), integer(self);

  return TOKEN_NUMBER;
}

gab_token hex(gab_lx *self) {
  while (can_continue_hex(peek(self)))
    advance(self);

  // Binary Exponent
  if (peek(self) == 'p' && isexponent(peek_next(self)))
    return advance(self), advance(self), integer(self);

  return TOKEN_NUMBER;
}

gab_token number(gab_lx *self) {
  if (peek(self) == '0' && peek_next(self) == 'x')
    return advance(self), advance(self), hex(self);

  if (integer(self) == TOKEN_ERROR)
    return TOKEN_ERROR;

  if (peek(self) == '.' && isdigit(peek_next(self)))
    return advance(self), advance(self), decimal(self);

  // Decimal exponent
  if (peek(self) == 'e' && isexponent(peek_next(self)))
    return advance(self), advance(self), integer(self);

  return TOKEN_NUMBER;
}

gab_token other(gab_lx *self) {
  switch (peek(self)) {
  case ';':
    advance(self);
    return TOKEN_NEWLINE;
  case ',':
    advance(self);
    return TOKEN_NEWLINE;
  case '(':
    advance(self);
    return TOKEN_LPAREN;
  case ')':
    advance(self);
    return TOKEN_RPAREN;
  case '[':
    advance(self);
    return TOKEN_LBRACE;
  case ']':
    advance(self);
    return TOKEN_RBRACE;
  case '{':
    advance(self);
    return TOKEN_LBRACK;
  case '}':
    advance(self);
    return TOKEN_RBRACK;
  case ':':
    advance(self);

    if (peek(self) == ':')
      return advance(self), TOKEN_COLONCOLON;

    if (peek(self) == '=')
      return advance(self), TOKEN_COLONEQUAL;

    return TOKEN_MESSAGE;
  case '\\':
    advance(self);

    if (peek(self) == '{') {
      advance(self);
      return TOKEN_SLBRACK;
    }

    advance(self);
    return lexer_error(self, GAB_MALFORMED_TOKEN);
  case '.':
    advance(self);

    if (can_start_operator(peek(self))) {
      advance(self);

      enum gab_token t = operator(self);

      if (t == TOKEN_OPERATOR)
        return TOKEN_SEND;

      return lexer_error(self, GAB_MALFORMED_TOKEN);
    }

    if (can_start_symbol(peek(self))) {
      advance(self);

      enum gab_token t = symbol(self);

      if (t == TOKEN_SYMBOL)
        return TOKEN_SEND;

      return lexer_error(self, GAB_MALFORMED_TOKEN);
    }

    if (isdigit(peek(self)))
      return integer(self);

    return TOKEN_SEND;

  default:
    if (can_start_operator(peek(self)))
      return operator(self);

    advance(self);
    return lexer_error(self, GAB_MALFORMED_TOKEN);
  }
}

static inline void parse_comment(gab_lx *self) {
  while (peek(self) != '\n') {
    advance(self);

    if (peek_next(self) == '\0' || peek_next(self) == EOF)
      break;
  }
}

gab_token gab_lexnext(gab_lx *self) {
  if (self->cursor - self->source->source->data >= self->source->source->len)
    goto eof;

  while (isblank(peek(self)) || is_comment(peek(self))) {
    if (is_comment(peek(self)))
      parse_comment(self);

    if (isblank(peek(self)))
      advance(self);
  }

  assert(self->cursor - self->source->source->data < self->source->source->len);

  gab_token tok;
  start_token(self);

  if (peek(self) == '\0' || peek(self) == EOF) {
  eof:
    tok = TOKEN_EOF;
    v_gab_token_push(&self->source->tokens, tok);
    v_s_char_push(&self->source->token_srcs, self->current_token_src);
    v_uint64_t_push(&self->source->token_lines, self->row);

    finish_row(self);

    return tok;
  }

  if (peek(self) == '\n') {
    advance(self);
    tok = TOKEN_NEWLINE;

    v_gab_token_push(&self->source->tokens, tok);
    v_s_char_push(&self->source->token_srcs, self->current_token_src);
    v_uint64_t_push(&self->source->token_lines, self->row);

    finish_row(self);

    return tok;
  }

  if (can_start_symbol(peek(self))) {
    tok = symbol(self);
    goto fin;
  }

  if (peek(self) == '-' && isdigit(peek_next(self))) {
    advance(self);
    tok = number(self);
    goto fin;
  }

  if (isdigit(peek(self))) {
    tok = number(self);
    goto fin;
  }

  if (peek(self) == '"') {
    tok = string(self);
    goto fin;
  }

  if (peek(self) == '\'') {
    tok = string(self);
    goto fin;
  }

  tok = other(self);

fin:
  v_gab_token_push(&self->source->tokens, tok);
  v_s_char_push(&self->source->token_srcs, self->current_token_src);
  v_uint64_t_push(&self->source->token_lines, self->row);

  return tok;
}

void gab_srcdestroy(struct gab_src *self) {
  a_char_destroy(self->source);

  v_s_char_destroy(&self->lines);

  v_gab_token_destroy(&self->tokens);
  v_s_char_destroy(&self->token_srcs);
  v_uint64_t_destroy(&self->token_lines);

  v_gab_value_destroy(&self->constants);

  v_uint8_t_destroy(&self->bytecode);
  v_uint64_t_destroy(&self->bytecode_toks);
  d_uint64_t_destroy(&self->node_begin_toks);
  d_uint64_t_destroy(&self->node_end_toks);

  for (uint64_t i = 0; i < self->len; i++) {
    if (self->thread_bytecode[i].constants)
      free(self->thread_bytecode[i].constants);
    if (self->thread_bytecode[i].bytecode)
      free(self->thread_bytecode[i].bytecode);
  }

  free(self);
}

struct gab_src *gab_src(struct gab_triple gab, gab_value name,
                        const char *source, uint64_t len) {
  mtx_lock(&gab.eg->sources_mtx);

  if (d_gab_src_exists(&gab.eg->sources, name)) {
    if (gab.flags & fGAB_USE_RELOAD) {
      // We should really free some resources here.
      // Eh, there are a lot of pointers dangling into this.
      // Probably best to just save it somewhere else.
    } else {
      struct gab_src *src = d_gab_src_read(&gab.eg->sources, name);

      mtx_unlock(&gab.eg->sources_mtx);

      return src;
    }
  }

  uint64_t sz =
      sizeof(struct gab_src) + (gab.eg->len) * sizeof(struct src_bytecode);

  struct gab_src *src = malloc(sz);
  memset(src, 0, sz);

  src->len = gab.eg->len;
  src->source = a_char_create(source, len);
  src->name = name;

  gab_egkeep(gab.eg, gab_iref(gab, name));

  if (!len)
    goto fin;

  gab_lx lex;
  gab_lexcreate(&lex, src);

  for (;;) {
    gab_token t = gab_lexnext(&lex);

    if (t == TOKEN_EOF)
      break;
  }

fin:
  d_gab_src_insert(&gab.eg->sources, name, src);

  mtx_unlock(&gab.eg->sources_mtx);

  return src;
}

uint64_t gab_srcappend(struct gab_src *self, uint64_t len,
                       uint8_t bc[static len], uint64_t toks[static len]) {
  v_uint8_t_cap(&self->bytecode, self->bytecode.len + len);
  v_uint64_t_cap(&self->bytecode_toks, self->bytecode_toks.len + len);

  for (uint64_t i = 0; i < len; i++) {
    v_uint8_t_push(&self->bytecode, bc[i]);
    v_uint64_t_push(&self->bytecode_toks, toks[i]);
  }

  assert(self->bytecode.len == self->bytecode_toks.len);

  return self->bytecode.len;
}

gab_value gab_srcname(struct gab_src *src) { return src->name; }

uint64_t gab_srcline(struct gab_src *src, uint64_t bytecode_offset) {
  if (!src->source->len)
    return 0;

  uint64_t tok = v_uint64_t_val_at(&src->bytecode_toks, bytecode_offset);
  return v_uint64_t_val_at(&src->token_lines, tok);
}

uint64_t gab_tsrcline(struct gab_src *src, uint64_t tok_offset) {
  if (!src->source->len)
    return 0;

  return v_uint64_t_val_at(&src->token_lines, tok_offset);
}

#undef CURSOR
#undef NEXT_CURSOR
#undef ADVANCE

// -- object --

#define GAB_CREATE_OBJ(obj_type, kind)                                         \
  ((struct obj_type *)gab_obj_create(gab, sizeof(struct obj_type), kind))

#define GAB_CREATE_FLEX_OBJ(obj_type, flex_type, flex_count, kind)             \
  ((struct obj_type *)gab_obj_create(                                          \
      gab, sizeof(struct obj_type) + sizeof(flex_type) * (flex_count),         \
      (kind)))

struct gab_obj *gab_obj_create(struct gab_triple gab, uint64_t sz,
                               enum gab_kind k) {
  struct gab_obj *self = gab_egalloc(gab, nullptr, sz);

  self->kind = k;
  self->references = 1;
  self->flags = fGAB_OBJ_NEW;

#if cGAB_LOG_GC
  fprintf(stderr, "[WORKER %i] CREATE\t%p\t%lu\t%d\n", gab.wkid, (void *)self,
          sz, k);
#endif

  struct gab_job *wk = gab.eg->jobs + gab.wkid;
  if (wk->locked) {
    v_gab_value_push(&wk->lock_keep, __gab_obj(self));
    GAB_OBJ_BUFFERED(self);
#if cGAB_LOG_GC
    fprintf(stderr, "[WORKER %i] QLOCK\t%p\n", gab.wkid, (void *)self);
#endif
  } else {
    gab_dref(gab, __gab_obj(self));
  }

  return self;
}

uint64_t gab_objsize(struct gab_obj *obj) {
  switch (obj->kind) {
  case kGAB_CHANNEL:
    return sizeof(struct gab_ochannel);
  case kGAB_BOX: {
    struct gab_obox *o = (struct gab_obox *)obj;
    return sizeof(struct gab_obox) + o->len * sizeof(char);
  }
  case kGAB_RECORDNODE: {
    struct gab_orecnode *o = (struct gab_orecnode *)obj;
    return sizeof(struct gab_orecnode) + o->len * sizeof(gab_value);
  }
  case kGAB_RECORD: {
    struct gab_orec *o = (struct gab_orec *)obj;
    return sizeof(struct gab_orec) + o->len * sizeof(gab_value);
  }
  case kGAB_BLOCK: {
    struct gab_oblock *o = (struct gab_oblock *)obj;
    return sizeof(struct gab_oblock) + o->nupvalues * sizeof(gab_value);
  }
  case kGAB_PROTOTYPE: {
    struct gab_oprototype *o = (struct gab_oprototype *)obj;
    return sizeof(struct gab_oprototype) + o->nupvalues * sizeof(char);
  }
  case kGAB_SHAPE:
  case kGAB_SHAPELIST: {
    struct gab_oshape *o = (struct gab_oshape *)obj;
    return sizeof(struct gab_oshape) + o->len * sizeof(gab_value);
  }
  case kGAB_STRING: {
    struct gab_ostring *o = (struct gab_ostring *)obj;
    return sizeof(struct gab_ostring) + (o->len + 1) * sizeof(char);
  }
  case kGAB_FIBER:
    return sizeof(struct gab_ofiber);
  case kGAB_NATIVE:
    return sizeof(struct gab_onative);
  default:
    break;
  }
  assert(false && "unreachable");
  return 0;
}

int sshape_dumpkeys(char **dest, size_t *n, gab_value shape, int depth) {
  struct gab_oshape *shp = GAB_VAL_TO_SHAPE(shape);
  uint64_t len = shp->len;

  if (len == 0)
    return 0;

  if (len > 16 && depth >= 0)
    return snprintf_through(dest, n, "... ");

  if (snprintf_through(dest, n, " ") < 0)
    return -1;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_svalinspect(dest, n, shp->keys[i], depth - 1) < 0)
      return -1;

    if (i + 1 < len)
      if (snprintf_through(dest, n, " ") < 0)
        return -1;
  }

  return snprintf_through(dest, n, " ");
}
int srec_dumpvalues(char **dest, size_t *n, gab_value rec, int depth) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  uint64_t len = gab_reclen(rec);

  if (len == 0)
    return 0;

  if (len > 16 && depth >= 0)
    return snprintf_through(dest, n, " ... ");

  if (snprintf_through(dest, n, " ") < 0)
    return -1;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_svalinspect(dest, n, gab_uvrecat(rec, i), depth - 1) < 0)
      return -1;

    if (i + 1 < len)
      if (snprintf_through(dest, n, ", ") < 0)
        return -1;
  }

  return snprintf_through(dest, n, " ");
}

int srec_dumpproperties(char **dest, size_t *n, gab_value rec, int depth) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  uint64_t len = gab_reclen(rec);

  if (len == 0)
    return 0;

  if (len > 16 && depth >= 0)
    return snprintf_through(dest, n, " ... ");

  if (snprintf_through(dest, n, " ") < 0)
    return -1;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_svalinspect(dest, n, gab_ukrecat(rec, i), depth - 1) < 0)
      return -1;

    if (snprintf_through(dest, n, " ") < 0)
      return -1;

    if (gab_svalinspect(dest, n, gab_uvrecat(rec, i), depth - 1) < 0)
      return -1;

    if (i + 1 < len)
      if (snprintf_through(dest, n, ", ") < 0)
        return -1;
  }

  return snprintf_through(dest, n, " ");
}

int sinspectval(char **dest, size_t *n, gab_value self, int depth) {
  switch (gab_valkind(self)) {
  case kGAB_PRIMITIVE: {
    switch (self) {
    case gab_cundefined:
      return snprintf_through(dest, n, "cundefined");
    case gab_cinvalid:
      return snprintf_through(dest, n, "cinvalid");
    case gab_ctimeout:
      return snprintf_through(dest, n, "ctimeout");
    case gab_cvalid:
      return snprintf_through(dest, n, "cvalid");
    default:
      return snprintf_through(dest, n, "<" tGAB_PRIMITIVE " %s>",
                              gab_opcode_names[gab_valtop(self)]);
    }
  }
  case kGAB_NUMBER:
    return snprintf_through(dest, n, "%lg", gab_valtof(self));
  case kGAB_STRING:
    return snprintf_through(dest, n, "%s", gab_strdata(&self));
  case kGAB_BINARY: {
    const char *s = gab_strdata(&self);

    if (snprintf_through(dest, n, "<" tGAB_BINARY " 0x") < 0)
      return -1;

    uint64_t len = gab_strlen(self);

    if (len < cGAB_BINARY_LEN_CUTOFF) {
      while (len--)
        if (snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;
    } else {
      uint64_t preview = cGAB_BINARY_LEN_CUTOFF;
      while (preview--)
        if (snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;

      if (snprintf_through(dest, n, "...") < 0)
        return -1;
    }

    if (snprintf_through(dest, n, ">") < 0)
      return -1;

    return 0;
  }
  case kGAB_MESSAGE:
    return snprintf_through(dest, n, "%s:", gab_strdata(&self));
  case kGAB_SHAPE:
  case kGAB_SHAPELIST:
    return snprintf_through(dest, n, "<" tGAB_SHAPE " ") +
           sshape_dumpkeys(dest, n, self, depth) +
           snprintf_through(dest, n, ">");
  case kGAB_CHANNEL:
    return snprintf_through(dest, n, "<" tGAB_CHANNEL " %p>",
                            GAB_VAL_TO_CHANNEL(self));
  case kGAB_CHANNELCLOSED:
    return snprintf_through(dest, n, "<" tGAB_CHANNEL " %p>",
                            GAB_VAL_TO_CHANNEL(self));
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE: {
    struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(self);

    return snprintf_through(dest, n, "<" tGAB_FIBER " %p ", fiber) +
           sinspectval(dest, n, fiber->data[0], 0) +
           snprintf_through(dest, n, " ") +
           sinspectval(dest, n, fiber->data[1], 0) +
           snprintf_through(dest, n, ">");
  }
  case kGAB_RECORD: {
    if (gab_valkind(gab_recshp(self)) == kGAB_SHAPELIST)
      return snprintf_through(dest, n, "[") +
             srec_dumpvalues(dest, n, self, depth) +
             snprintf_through(dest, n, "]");
    else
      return snprintf_through(dest, n, "{") +
             srec_dumpproperties(dest, n, self, depth) +
             snprintf_through(dest, n, "}");
  }
  case kGAB_RECORDNODE:
    return srec_dumpproperties(dest, n, self, depth);
  case kGAB_BOX: {
    struct gab_obox *con = GAB_VAL_TO_BOX(self);
    return snprintf_through(dest, n, "<" tGAB_BOX " ") +
           gab_svalinspect(dest, n, con->type, depth) +
           snprintf_through(dest, n, ">");
  }
  case kGAB_BLOCK: {
    struct gab_oblock *blk = GAB_VAL_TO_BLOCK(self);
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);
    uint64_t line = gab_srcline(p->src, p->offset);
    return snprintf_through(dest, n, "<" tGAB_BLOCK " ") +
           gab_svalinspect(dest, n, gab_srcname(p->src), depth) +
           snprintf_through(dest, n, ":%lu>", line);
  }
  case kGAB_NATIVE: {
    struct gab_onative *native = GAB_VAL_TO_NATIVE(self);
    return snprintf_through(dest, n, "<" tGAB_NATIVE " ") +
           gab_svalinspect(dest, n, native->name, depth) +
           snprintf_through(dest, n, ">");
  }
  case kGAB_PROTOTYPE: {
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(self);
    uint64_t line = gab_srcline(p->src, p->offset);
    return snprintf_through(dest, n, "<" tGAB_PROTOTYPE " ") +
           gab_svalinspect(dest, n, gab_srcname(p->src), depth) +
           snprintf_through(dest, n, ":%lu>", line);
  }
  default:
    gab_assert(false, "Inspecting unrecognized object kind %d in %p\n",
               gab_valkind(self), gab_valtoo(self));
    return 0;
  }
}

int gab_svalinspect(char **dest, size_t *n, gab_value value, int depth) {
  return sinspectval(dest, n, value, depth);
}

int gab_fvalinspect(FILE *stream, gab_value self, int depth) {
  for (size_t i = 128;; i <<= 1) {
    size_t n = i;
    char buf[n];
    char *cursor = buf;
    if (gab_svalinspect(&cursor, &n, self, depth) >= 0) {
      fprintf(stream, "%s", buf);
      return 1;
    };
  }

  return 0;
}

void gab_objdestroy(struct gab_triple gab, struct gab_obj *self) {
  switch (self->kind) {
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE: {
    struct gab_ofiber *fib = (struct gab_ofiber *)self;

    /*if (fib->res_values != nullptr)*/
    /*  a_gab_value_destroy(fib->res_values);*/

    v_uint8_t_destroy(&fib->allocator);
    break;
  };
  case kGAB_SHAPE:
  case kGAB_SHAPELIST: {
    struct gab_oshape *shp = (struct gab_oshape *)self;
    v_gab_value_destroy(&shp->transitions);
    break;
  }
  case kGAB_BOX: {
    struct gab_obox *box = (struct gab_obox *)self;
    if (box->do_destroy)
      box->do_destroy(gab, box->len, box->data);
    break;
  }
  case kGAB_STRING:
    assert(mtx_trylock(&gab.eg->gc_mtx) == thrd_busy);
    /*
     * ASYNC ISSUE: Because collections happen asynchronously (and the strings
     * intern table *doesn't hold references) Strings that are queued for
     * removal *can* be re-used *right* before they are deleted. This requires a
     * better, long-term solution.
     *
     * TO RESOLVE THIS ISSUE: Strings are incremented as they are created.
     * This means strings are never deallocated (not an ideal solution)
     *
     *             string reused
     *             |           |
     * worker |--@-$-*---*---*-$------
     *           |
     *         string created, dec queued
     *
     *     gc |-------*----*-----*----
     *                           |
     *                          string destroyed
     *
     *   Epochs are marked with '*'
     *
     *   The string is reused, but wasn't touched during any epoch on the
     * worker. Therefor the gc tries to free it, and the latest reuse on the
     * worker is a UAF.
     *
     *   A Big String Refactor could look like the following:
     *     - Allocate string data separately from string objects.
     *     - ARC the string data, in real time. All the re-uses will inc and dec
     * the str data.
     *     - When the str data ARC reaches zero, we can free it.
     *     - String objects store slices into the ARC arena.
     *
     *   I think this is possible by storing an atomic int on the str itself.
     *     - When a str is returned (interned or not), we increment the ARC.
     *     - When a str is destroyed (decrement the ARC).
     *     - If the ARC is zero, we can deallocate the ostring and remove it
     * from the dict.
     *     - It also doesn't really make sense, as I think about it.
     *
     */
    d_strings_remove(&gab.eg->strings, (struct gab_ostring *)self);
    break;
  default:
    break;
  }
}

gab_value gab_shorstr(uint64_t len, const char *data) {
  assert(len <= 5);

  gab_value v = 0;
  v |= (__GAB_QNAN | (uint64_t)kGAB_STRING << __GAB_TAGOFFSET |
        (((uint64_t)5 - len) << 40));

  for (uint64_t i = 0; i < len; i++) {
    v |= (uint64_t)(0xff & data[i]) << (i * 8);
  }

  return v;
}

gab_value __gab_shortstrcat(gab_value _a, gab_value _b) {
  assert(gab_valkind(_a) == kGAB_STRING || gab_valkind(_a) == kGAB_BINARY);
  assert(gab_valkind(_b) == kGAB_STRING || gab_valkind(_a) == kGAB_BINARY);

  uint64_t alen = gab_strlen(_a);
  uint64_t blen = gab_strlen(_b);

  assert(alen + blen <= 5);

  uint8_t len = alen + blen;

  gab_value v = 0;
  v |= (__GAB_QNAN | (uint64_t)kGAB_STRING << __GAB_TAGOFFSET |
        (((uint64_t)5 - len) << 40));

  for (uint64_t i = 0; i < alen; i++) {
    v |= (uint64_t)(0xff & gab_strdata(&_a)[i]) << (i * 8);
  }

  for (uint64_t i = 0; i < blen; i++) {
    v |= (uint64_t)(0xff & gab_strdata(&_b)[i]) << ((i + alen) * 8);
  }

  assert(gab_valkind(v) == kGAB_STRING);

  return v;
}

gab_value nstring(struct gab_triple gab, uint64_t hash, uint64_t len,
                  const char *data) {
  s_char str = s_char_create(data, len);

  struct gab_ostring *self =
      GAB_CREATE_FLEX_OBJ(gab_ostring, char, str.len + 1, kGAB_STRING);

  memcpy(self->data, str.data, str.len);
  self->len = str.len;
  self->hash = hash;

  mbstate_t state = {0};
  const char *cursor = self->data;
  self->mb_len = mbsrtowcs(NULL, &cursor, 0, &state);

  return __gab_obj(self);
}

gab_value gab_tnstring(struct gab_triple gab, uint64_t len, const char *data) {
  if (len <= 5)
    return gab_shorstr(len, data);

#if cGAB_STRING_HASHLEN > 0
  uint64_t hash =
      hash_bytes(len < cGAB_STRING_HASHLEN ? len : cGAB_STRING_HASHLEN,
                 (unsigned char *)data);
#else
  uint64_t hash = hash_bytes(len, (unsigned char *)data);
#endif

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  struct gab_ostring *interned = gab_egstrfind(gab.eg, hash, len, data);

  mtx_unlock(&gab.eg->gc_mtx);

  if (interned)
    return __gab_obj(interned);

  /*
   * We can't hold the strings_mtx lock here in the call
   * to nstring, because the creation of this object
   * might signal a collection. In that case, the gc needs to hold
   * the strings_mtx for the duration of the collection.
   */
  gab_value s = nstring(gab, hash, len, data);

  /*
   * TODO @cgab @bug: Inbetween the two lock holds here, another thread
   * *could* insert the string we want into the dict.
   * In that case, we'd stomp over the old value
   * and leak its memory.
   */

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  d_strings_insert(&gab.eg->strings, GAB_VAL_TO_STRING(s), 0);

  mtx_unlock(&gab.eg->gc_mtx);

  return s;
}

/*
 * TODO @cgab @runtime @bug: can be interrupted by TERM, which can break a lot
 * of things.
 */
gab_value gab_nstring(struct gab_triple gab, uint64_t len, const char *data) {
  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_IGN:
      break;
    case sGAB_TERM:
      // break;
      return gab_cinvalid;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    }

    gab_value str = gab_tnstring(gab, len, data);
    if (str == gab_cinvalid)
      return str;

    if (str == gab_ctimeout)
      continue;

    assert(gab_valkind(str) == kGAB_STRING);
    return str;
  }
};

gab_value gab_strcat(struct gab_triple gab, gab_value _a, gab_value _b) {
  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_IGN:
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    }

    gab_value str = gab_tstrcat(gab, _a, _b);
    if (str == gab_cinvalid)
      return str;

    if (str == gab_ctimeout)
      continue;

    assert(gab_valkind(str) == kGAB_STRING);
    return str;
  }
}

GAB_API inline const char *gab_strdata(gab_value *str) {
  assert(gab_valkind(*str) == kGAB_STRING ||
         gab_valkind(*str) == kGAB_MESSAGE || gab_valkind(*str) == kGAB_BINARY);

  if (gab_valiso(*str))
    return GAB_VAL_TO_STRING(*str)->data;

  return ((const char *)str);
}

GAB_API inline uint64_t gab_strlen(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING || gab_valkind(str) == kGAB_MESSAGE ||
         gab_valkind(str) == kGAB_BINARY);

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->len;

  return 5 - ((str >> 40) & 0xFF);
};

GAB_API inline uint64_t gab_strmblen(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING || gab_valkind(str) == kGAB_BINARY ||
         gab_valkind(str) == kGAB_MESSAGE);

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->mb_len;

  // This is a small string. No space to store mb_len, so just recompute.
  mbstate_t state = {0};
  const char *cursor = gab_strdata(&str);
  return mbsrtowcs(NULL, &cursor, 0, &state);
};

GAB_API inline uint64_t gab_strhash(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING);

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->hash;

  // TODO @cgab @bug: Propertly hash the contents of short strings.
  return str;
}

GAB_API inline int gab_binat(gab_value str, size_t idx) {
  assert(gab_valkind(str) == kGAB_BINARY);

  size_t len = gab_strlen(str);

  if (idx >= len)
    return -1;

  return gab_strdata(&str)[idx];
}

/*
  Given two strings, create a third which is the concatenation a+b
*/
gab_value gab_tstrcat(struct gab_triple gab, gab_value _a, gab_value _b) {
  assert(gab_valkind(_a) == kGAB_STRING);
  assert(gab_valkind(_b) == kGAB_STRING);

  uint64_t alen = gab_strlen(_a);
  uint64_t blen = gab_strlen(_b);

  if (alen == 0)
    return _b;

  if (blen == 0)
    return _a;

  uint64_t len = alen + blen;

  if (len <= 5)
    return __gab_shortstrcat(_a, _b);

  a_char *buff = a_char_empty(len + 1);

  // Copy the data into the string obj.
  memcpy(buff->data, gab_strdata(&_a), alen);
  memcpy(buff->data + alen, gab_strdata(&_b), blen);

// Pre compute the hash
#if cGAB_STRING_HASHLEN > 0
  uint64_t hash =
      hash_bytes(len < cGAB_STRING_HASHLEN ? len : cGAB_STRING_HASHLEN,
                 (unsigned char *)buff->data);
#else
  uint64_t hash = hash_bytes(len, (unsigned char *)buff->data);
#endif

  /*
    If this string was interned already, return.

    Unfortunately, we can't check for this before copying and computing the
    hash.
  */

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  struct gab_ostring *interned = gab_egstrfind(gab.eg, hash, len, buff->data);

  mtx_unlock(&gab.eg->gc_mtx);

  if (interned)
    return a_char_destroy(buff), __gab_obj(interned);

  gab_value result = nstring(gab, hash, len, buff->data);

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  d_strings_insert(&gab.eg->strings, GAB_VAL_TO_STRING(result), 0);

  mtx_unlock(&gab.eg->gc_mtx);

  assert(gab_valkind(result) == kGAB_STRING);
  assert(gab_strlen(result) == len);

  return a_char_destroy(buff), result;
};

gab_value gab_prototype(struct gab_triple gab, struct gab_src *src,
                        uint64_t offset, uint64_t len,
                        struct gab_prototype_argt args) {

  struct gab_oprototype *self = GAB_CREATE_FLEX_OBJ(
      gab_oprototype, uint8_t, args.nupvalues, kGAB_PROTOTYPE);

  self->src = src;
  self->offset = offset;
  self->len = args.nupvalues;
  self->len = len;
  self->nslots = args.nslots;
  self->nlocals = args.nlocals;
  self->nupvalues = args.nupvalues;
  self->narguments = args.narguments;
  self->env = args.env;

  if (args.nupvalues > 0) {
    if (args.data) {
      memcpy(self->data, args.data, args.nupvalues * sizeof(uint8_t));
    } else if (args.flags && args.indexes) {
      for (uint8_t i = 0; i < args.nupvalues; i++) {
        bool is_local = args.flags[i] & fLOCAL_LOCAL;
        self->data[i] = (args.indexes[i] << 1) | is_local;
      }
    } else {
      assert(0 && "Invalid arguments to gab_bprototype");
    }
  }

  return __gab_obj(self);
}

gab_value gab_prtenv(gab_value prt) {
  assert(gab_valkind(prt) == kGAB_PROTOTYPE);
  return GAB_VAL_TO_PROTOTYPE(prt)->env;
}

GAB_API gab_value gab_prtparams(struct gab_triple gab, gab_value prt) {
  assert(gab_valkind(prt) == kGAB_PROTOTYPE);
  gab_value shp = gab_prtshp(prt);
  uint8_t nargs = GAB_VAL_TO_PROTOTYPE(prt)->narguments;
  return gab_shape(gab, 1, nargs, gab_shpdata(shp), nullptr);
}

gab_value gab_native(struct gab_triple gab, gab_value name, gab_native_f f) {
  assert(gab_valkind(name) == kGAB_STRING || gab_valkind(name) == kGAB_MESSAGE);

  struct gab_onative *self = GAB_CREATE_OBJ(gab_onative, kGAB_NATIVE);

  self->name = name;
  self->function = f;

  return __gab_obj(self);
}

gab_value gab_snative(struct gab_triple gab, const char *name, gab_native_f f) {
  return gab_native(gab, gab_string(gab, name), f);
}

gab_value gab_block(struct gab_triple gab, gab_value prototype) {
  assert(gab_valkind(prototype) == kGAB_PROTOTYPE);
  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(prototype);

  struct gab_oblock *self =
      GAB_CREATE_FLEX_OBJ(gab_oblock, gab_value, p->nupvalues, kGAB_BLOCK);

  self->p = prototype;
  self->nupvalues = p->nupvalues;

  for (uint8_t i = 0; i < self->nupvalues; i++) {
    self->upvalues[i] = gab_cinvalid;
  }

  return __gab_obj(self);
}

GAB_API gab_value gab_blkproto(gab_value block) {
  assert(gab_valkind(block) == kGAB_BLOCK);
  return GAB_VAL_TO_BLOCK(block)->p;
}

gab_value gab_box(struct gab_triple gab, struct gab_box_argt args) {
  struct gab_obox *self =
      GAB_CREATE_FLEX_OBJ(gab_obox, unsigned char, args.size, kGAB_BOX);

  self->do_destroy = args.destructor;
  self->type = args.type;
  self->len = args.size;

  if (args.data) {
    memcpy(self->data, args.data, args.size);
  } else {
    memset(self->data, 0, args.size);
  }

  return __gab_obj(self);
}

GAB_API uint64_t gab_boxlen(gab_value box) {
  assert(gab_valkind(box) == kGAB_BOX);
  return GAB_VAL_TO_BOX(box)->len;
}

GAB_API void *gab_boxdata(gab_value box) {
  assert(gab_valkind(box) == kGAB_BOX);
  return GAB_VAL_TO_BOX(box)->data;
}

GAB_API gab_value gab_boxtype(gab_value value) {
  assert(gab_valkind(value) == kGAB_BOX);
  return GAB_VAL_TO_BOX(value)->type;
}

gab_value __gab_record(struct gab_triple gab, uint64_t len, uint64_t space,
                       gab_value *data) {
  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, space + len, kGAB_RECORD);

  self->len = len + space;
  self->shape = gab_cinvalid;
  self->shift = GAB_PVEC_BITS;

  if (len) {
    assert(data);
    memcpy(self->data, data, sizeof(gab_value) * len);
  }

  for (uint64_t i = len; i < self->len; i++)
    self->data[i] = gab_cinvalid;

  return __gab_obj(self);
}

gab_value __gab_recordnode(struct gab_triple gab, uint64_t len, uint64_t adjust,
                           gab_value *data) {
  assert(adjust + len > 0);
  struct gab_orecnode *self = GAB_CREATE_FLEX_OBJ(
      gab_orecnode, gab_value, adjust + len, kGAB_RECORDNODE);

  self->len = len + adjust;

  if (len) {
    assert(data);
    memcpy(self->data, data, sizeof(gab_value) * len);
  }

  for (uint64_t i = len; i < self->len; i++)
    self->data[i] = gab_cinvalid;

  return __gab_obj(self);
}

gab_value reccpy(struct gab_triple gab, gab_value r, int64_t adjust) {
  switch (gab_valkind(r)) {
  case kGAB_RECORD: {
    struct gab_orec *n = GAB_VAL_TO_REC(r);

    struct gab_orec *nm =
        GAB_VAL_TO_REC(__gab_record(gab, n->len, adjust, n->data));

    nm->shift = n->shift;
    nm->shape = n->shape;

    return __gab_obj(nm);
  }
  case kGAB_RECORDNODE: {
    struct gab_orecnode *n = GAB_VAL_TO_RECNODE(r);
    return __gab_recordnode(gab, n->len, adjust, n->data);
  }
    // Saw invalid
  case kGAB_PRIMITIVE:
    assert(r == gab_cinvalid);
    return __gab_recordnode(gab, 0, 1, nullptr);
  default:
    break;
  }

  assert(0 && "Only rec and recnodebranch cpy");
  return gab_cinvalid;
}
void recpop(gab_value rec) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE: {
    struct gab_orecnode *r = GAB_VAL_TO_RECNODE(rec);
    assert(r->len > 0);
    r->len--;
    return;
  }
  case kGAB_RECORD: {
    struct gab_orec *r = GAB_VAL_TO_REC(rec);
    assert(r->len > 0);
    r->len--;
    return;
  }
  default:
    break;
  }
  assert(false && "UNREACHABLE");
}

void recassoc(gab_value rec, gab_value v, uint64_t i) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE: {
    struct gab_orecnode *r = GAB_VAL_TO_RECNODE(rec);
    assert(i < r->len);
    r->data[i] = v;
    return;
  }
  case kGAB_RECORD: {
    struct gab_orec *r = GAB_VAL_TO_REC(rec);
    assert(i < r->len);
    r->data[i] = v;
    return;
  }
  default:
    break;
  }
  assert(false && "UNREACHABLE");
}

gab_value recnth(gab_value rec, uint64_t n) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE: {
    struct gab_orecnode *r = GAB_VAL_TO_RECNODE(rec);
    assert(n < r->len);
    return r->data[n];
  }
  case kGAB_RECORD: {
    struct gab_orec *r = GAB_VAL_TO_REC(rec);
    assert(n < r->len);
    return r->data[n];
  }
  default:
    break;
  }

  assert(false && "UNREACHABLE");
  return gab_cinvalid;
}

uint64_t reclen(gab_value rec) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE:
    return GAB_VAL_TO_RECNODE(rec)->len;
  case kGAB_RECORD:
    return GAB_VAL_TO_REC(rec)->len;
  case kGAB_PRIMITIVE:
    assert(rec == gab_cinvalid);
    return 0;
  default:
    assert(false && "UNREACHABLE");
    return 0;
  }
}

gab_value gab_uvrecat(gab_value rec, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);

  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  gab_value node = rec;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;

    gab_value next_node = recnth(node, idx);

    assert(gab_valkind(next_node) == kGAB_RECORDNODE ||
           gab_valkind(next_node) == kGAB_RECORD);

    node = next_node;
  }

  node = recnth(node, i & GAB_PVEC_MASK);

  assert(gab_valkind(node) != kGAB_RECORDNODE);

  return node;
}

bool recneedsspace(gab_value rec, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);
  uint64_t idx = (i >> r->shift) & GAB_PVEC_MASK;
  return idx >= r->len;
}

gab_value recsetshp(gab_value rec, gab_value shp) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);
  r->shape = shp;
  return rec;
}

/*
 * Since order is always dictated by shape, we can do a cheeky optimization for
 * dissoc.
 *
 * The bit-partitioned vector trie data structure used for records only supports
 * push-and-pop operations.
 *
 * To do a dissoc from anywhere within the record, we need to swap the value at
 * the end and of the record with the chosen value, and then perform the pop.
 * This means we need to clone two paths through the trie -
 *  1. one down to the chosen value
 *  2. one down to the last node
 *
 * then, perform the swap and pop
 *
 * we can do this because shapes dictate order, not records themselves.
 * this will create a new shape. (to account for the swapped value, not seen
 * here)
 *
 * There is a fast case, where the value popped *is* the last value.
 */
gab_value dissoc(struct gab_triple gab, gab_value rec, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  gab_value chosen_node = rec;
  gab_value root = chosen_node;
  gab_value chosen_path = root;
  gab_value rightmost_node = rec;
  gab_value rightmost_path = root;

  /*
   * Keep track of if the path to chosen elem has diverged
   * from the path to the last elem.
   *
   * While they converge, we only have to copy one path.
   */
  bool diverged = false;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;

    assert(idx < reclen(chosen_node));

    chosen_node = reccpy(gab, recnth(chosen_node, idx), 0);

    recassoc(chosen_path, chosen_node, idx);
    chosen_path = chosen_node;

    uint64_t rightmost_idx = reclen(rightmost_node) - 1;

    if (!diverged && idx == rightmost_idx) {
      rightmost_node = chosen_node;
      rightmost_path = chosen_path;
      continue;
    }

    diverged = true;

    assert(rightmost_idx < reclen(rightmost_node));

    // Improve this to trim this copy with negative space when necessary
    // TODO @cgab @bug: Account for popping out empty nodes
    rightmost_node = reccpy(gab, recnth(rightmost_node, rightmost_idx), 0);

    recassoc(rightmost_path, rightmost_node, rightmost_idx);
    rightmost_path = rightmost_node;
  }

  assert(chosen_node != gab_cinvalid);
  // Update the chosen node with the value we're popping
  recassoc(chosen_node, recnth(rightmost_node, reclen(rightmost_node) - 1),
           i & GAB_PVEC_MASK);

  // the rightmost node should have one less value. This can be done more
  // effieciently above.
  recpop(rightmost_node);
  return root;
}

gab_value assoc(struct gab_triple gab, gab_value rec, gab_value v, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  gab_value node = rec;
  gab_value root = node;
  gab_value path = root;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;
    uint64_t nidx = (i >> (level - GAB_PVEC_BITS)) & GAB_PVEC_MASK;

    if (idx < reclen(node))
      node = reccpy(gab, recnth(node, idx), nidx >= reclen(recnth(node, idx)));
    else
      node = __gab_recordnode(gab, 0, 1, nullptr);

    recassoc(path, node, idx);
    path = node;
  }

  assert(node != gab_cinvalid);
  recassoc(node, v, i & GAB_PVEC_MASK);
  return root;
}

void massoc(struct gab_triple gab, gab_value rec, gab_value v, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  assert(i < gab_reclen(rec));

  gab_value node = rec;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;

    assert(idx < reclen(node));
    node = recnth(node, idx);
  }

  assert(node != gab_cinvalid);
  recassoc(node, v, i & GAB_PVEC_MASK);

  return;
}

gab_value cons(struct gab_triple gab, gab_value rec, gab_value v,
               gab_value shp) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  uint64_t i = gab_reclen(rec);

  // overflow root
  if ((i >> GAB_PVEC_BITS) >= ((uint64_t)1 << r->shift)) {
    gab_value new_root = __gab_record(gab, 1, 1, &rec);

    struct gab_orec *new_r = GAB_VAL_TO_REC(new_root);

    new_r->shape = shp;
    new_r->shift = r->shift + 5;

#ifndef NDEBUG
    for (size_t j = 0; j < i; j++)
      gab_uvrecat(new_root, j);
#endif

    assoc(gab, new_root, v, i);

#ifndef NDEBUG
    for (size_t j = 0; j < i; j++)
      gab_uvrecat(new_root, j);
#endif

    return new_root;
  }

  gab_value record =
      recsetshp(assoc(gab, reccpy(gab, rec, recneedsspace(rec, i)), v, i), shp);

#ifndef NDEBUG
  for (size_t j = 0; j < i; j++)
    gab_uvrecat(record, j);
#endif

  return record;
}

gab_value gab_recput(struct gab_triple gab, gab_value rec, gab_value key,
                     gab_value val) {
  assert(gab_valkind(rec) == kGAB_RECORD);

  uint64_t idx = gab_recfind(rec, key);

  gab_gclock(gab);

  if (idx == -1) {
    gab_value result =
        cons(gab, rec, val, gab_shpwith(gab, gab_recshp(rec), key));

    return gab_gcunlock(gab), result;
  }

  gab_value result =
      assoc(gab, reccpy(gab, rec, recneedsspace(rec, idx)), val, idx);

  return gab_gcunlock(gab), result;
}

gab_value gab_rectake(struct gab_triple gab, gab_value rec, gab_value key,
                      gab_value *out_val) {
  /*
   * TODO @cgab @perf: This can be optimized.
   *
   * There is no need to allocate a new record here, we can reuse the original
   * record with a *new shape*.
   *
   * This may need to include *tombstone* kind of value, which we can replace
   * the original key in the shape with a gab_ctombstone. This will need to be
   * skipped in a lot of other gab_shape logic.
   */
  assert(gab_valkind(rec) == kGAB_RECORD);

  uint64_t idx = gab_recfind(rec, key);

  if (idx == -1) {
    if (out_val)
      *out_val = gab_nil;

    return rec;
  }

  gab_gclock(gab);

  if (out_val)
    *out_val = gab_uvrecat(rec, idx);

  gab_value result = recsetshp(dissoc(gab, reccpy(gab, rec, 0), idx),
                               gab_shpwithout(gab, gab_recshp(rec), key));

  return gab_gcunlock(gab), result;
}

gab_value gab_nlstpush(struct gab_triple gab, gab_value list, uint64_t len,
                       gab_value *values) {
  assert(gab_valkind(list) == kGAB_RECORD);

  uint64_t start = gab_reclen(list);

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    gab_value key = gab_number(start + i);
    gab_value val = values[i];
    list = gab_recput(gab, list, key, val);
  }

  return gab_gcunlock(gab), list;
}

gab_value gab_urecput(struct gab_triple gab, gab_value rec, uint64_t i,
                      gab_value v) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  assert(i < gab_reclen(rec));

  gab_gclock(gab);

  gab_value result = assoc(gab, reccpy(gab, rec, 0), v, i);

  return gab_gcunlock(gab), result;
}

uint64_t getlen(uint64_t n, uint64_t shift) {
  if (n)
    n--;

  return ((n >> shift) & GAB_PVEC_MASK) + 1;
}

void recfillchildren(struct gab_triple gab, gab_value rec, uint64_t shift,
                     uint64_t n, uint64_t len) {
  assert(len > 0);

  if (shift == 0)
    return;

  for (uint64_t l = 0; l < len - 1; l++) {
    gab_value lhs_child = __gab_recordnode(gab, 0, GAB_PVEC_SIZE, nullptr);

    recfillchildren(gab, lhs_child, shift - GAB_PVEC_BITS, n, GAB_PVEC_SIZE);

    recassoc(rec, lhs_child, l);
  }

  uint64_t rhs_childlen = getlen(n, shift - GAB_PVEC_BITS);

  gab_value rhs_child = __gab_recordnode(gab, 0, rhs_childlen, nullptr);

  recfillchildren(gab, rhs_child, shift - GAB_PVEC_BITS, n, rhs_childlen);

  recassoc(rec, rhs_child, len - 1);
}

uint64_t getshift(uint64_t n) {
  uint64_t shift = 0;

  if (n)
    n--;

  while ((n >> GAB_PVEC_BITS) >= (1 << shift)) {
    shift += 5;
  }

  return shift;
}

gab_value gab_shptorec(struct gab_triple gab, gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);

  uint64_t len = gab_shplen(shp);

  gab_gclock(gab);

  uint64_t shift = getshift(len);

  uint64_t rootlen = getlen(len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = shp;
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  if (len) {
    recfillchildren(gab, res, shift, len, rootlen);

    for (uint64_t i = 0; i < len; i++)
      massoc(gab, res, gab_nil, i);
  }

  return gab_gcunlock(gab), res;
}

gab_value gab_recordfrom(struct gab_triple gab, gab_value shape,
                         uint64_t stride, uint64_t len, gab_value *vals,
                         uint64_t *km) {
  gab_gclock(gab);

  uint64_t real_len = gab_shplen(shape);
  assert(real_len <= len);

  uint64_t shift = getshift(real_len);

  uint64_t rootlen = getlen(real_len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = shape;
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  if (real_len) {
    recfillchildren(gab, res, shift, real_len, rootlen);

    assert(real_len == gab_shplen(self->shape));

    uint64_t real_i = 0;
    for (uint64_t i = 0; i < len; i++) {
      uint64_t km_idx = i / 64;
      uint64_t in_idx = i % 64;
      assert(in_idx < 64);
      if (!km || !(km[km_idx] & ((uint64_t)1 << in_idx)))
        massoc(gab, res, vals[i * stride], real_i++);
    }
  }

  return gab_gcunlock(gab), res;
}

gab_value gab_record(struct gab_triple gab, uint64_t stride, uint64_t len,
                     gab_value *keys, gab_value *vals) {
  gab_gclock(gab);

  uint64_t km_size = 1 + (len / 64);
  uint64_t km[km_size];
  memset(km, 0, km_size * sizeof(uint64_t));

  gab_value shp = gab_shape(gab, stride, len, keys, km);
  gab_value rec = gab_recordfrom(gab, shp, stride, len, vals, km);
  return gab_gcunlock(gab), rec;
}

gab_value gab_recshp(gab_value record) {
  assert(gab_valkind(record) == kGAB_RECORD);
  return GAB_VAL_TO_REC(record)->shape;
};

gab_value nth_amongst(uint64_t n, uint64_t len, gab_value records[static len]) {
  assert(len > 0);

  uint64_t r = 0;
  uint64_t i = 0;

  while (r < len && n >= i + gab_reclen(records[r]))
    i += gab_reclen(records[r++]);

  return gab_uvrecat(records[r], n - i);
}

gab_value gab_nlstcat(struct gab_triple gab, uint64_t len,
                      gab_value records[static len]) {
  if (len == 0)
    return gab_erecord(gab);

  uint64_t total_len = 0;

  for (uint64_t i = 0; i < len; i++)
    total_len += gab_reclen(records[i]);

  if (total_len == 0)
    return gab_erecord(gab);

  gab_value total_keys[total_len];
  for (uint64_t i = 0; i < total_len; i++)
    total_keys[i] = gab_number(i);

  gab_gclock(gab);

  uint64_t shift = getshift(total_len);

  uint64_t rootlen = getlen(total_len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = gab_shape(gab, 1, total_len, total_keys, nullptr);
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  if (total_len) {
    recfillchildren(gab, res, shift, total_len, rootlen);

    assert(total_len == gab_shplen(self->shape));

    for (uint64_t i = 0; i < total_len; i++)
      massoc(gab, res, nth_amongst(i, total_len, records), i);
  }

  return gab_gcunlock(gab), res;
}

gab_value gab_nreccat(struct gab_triple gab, uint64_t len, gab_value *records) {

  if (len == 0)
    return gab_recordof(gab);

  gab_gclock(gab);

  gab_value shapes[len];
  for (uint64_t i = 0; i < len; i++)
    shapes[i] = gab_recshp(records[i]);

  gab_value new_shp = gab_nshpcat(gab, len, shapes);

  uint64_t total_len = gab_shplen(new_shp);
  uint64_t shift = getshift(total_len);
  uint64_t rootlen = getlen(total_len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = new_shp;
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  if (total_len) {
    recfillchildren(gab, res, shift, total_len, rootlen);
    assert(total_len == gab_shplen(self->shape));

    for (uint64_t i = 0; i < total_len; i++) {
      gab_value key = gab_ushpat(new_shp, i);
      for (uint64_t j = 0; j < len; j++) {
        gab_value rec = records[j];
        gab_value val = gab_recat(rec, key);
        if (val != gab_cundefined)
          massoc(gab, res, val, i);
      }
    }
  }

  return gab_gcunlock(gab), res;
}

gab_value gab_list(struct gab_triple gab, uint64_t stride, uint64_t size,
                   gab_value *values) {
  if (!size)
    return gab_record(gab, 0, 0, nullptr, nullptr);

  gab_gclock(gab);

  gab_value keys[size * stride] = {};

  for (uint64_t i = 0; i < size; i++)
    keys[i * stride] = gab_number(i);

  gab_value v = gab_record(gab, stride, size, keys, values);

  return gab_gcunlock(gab), v;
}

gab_value gab_shape(struct gab_triple gab, uint64_t stride, uint64_t len,
                    gab_value *keys, uint64_t *km_out) {
  gab_value shp = gab.eg->shapes;

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    gab_value new_shp = gab_shpwith(gab, shp, keys[i * stride]);

    if (km_out && new_shp == shp) {
      uint64_t km_idx = i / 64;
      uint64_t in_idx = i % 64;
      km_out[km_idx] |= ((uint64_t)1 << in_idx);
    }

    shp = new_shp;
  }

  gab_gcunlock(gab);

  /*assert(len == gab_shplen(shp));*/
  return shp;
}

uint64_t gab_shplen(gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);
  return s->len;
}

gab_value *gab_shpdata(gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);
  return s->keys;
};

uint64_t gab_shptfind(gab_value shp, gab_value key) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);

  uint64_t len = s->transitions.len / 2;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_valeq(key, v_gab_value_val_at(&s->transitions, i * 2)))
      return i;
  }

  return -1;
};

gab_value gab_nshpcat(struct gab_triple gab, uint64_t len,
                      gab_value shapes[static len]) {
  assert(len > 0);
  gab_value shp = shapes[0];

  for (uint64_t i = 1; i < len; i++)
    for (uint64_t k = 0; k < gab_shplen(shapes[i]); k++)
      shp = gab_shpwith(gab, shp, gab_ushpat(shapes[i], k));

  return shp;
}

gab_value __gab_shape(struct gab_triple gab, uint64_t len) {
  struct gab_oshape *self =
      GAB_CREATE_FLEX_OBJ(gab_oshape, gab_value, len, kGAB_SHAPELIST);

  self->len = len;

  v_gab_value_create(&self->transitions, 16);

  return gab_iref(gab, __gab_obj(self));
}

/*
 * This needs to mimic the swap-and-pop that records do to actually pop values
 */
gab_value gab_shpwithout(struct gab_triple gab, gab_value shape,
                         gab_value key) {
  gab_value shp = gab.eg->shapes;

  assert(gab_shpfind(shape, key) != UINT64_MAX);

  gab_gclock(gab);

  uint64_t len = gab_shplen(shape);

  gab_value last_key = gab_ushpat(shape, gab_shplen(shape) - 1);

  // Iterate through n - 1 keys
  for (uint64_t i = 0; i < len - 1; i++) {
    gab_value thiskey = gab_ushpat(shape, i);

    if (key == thiskey) // This performs the swap
      shp = gab_shpwith(gab, shp, last_key);
    else
      shp = gab_shpwith(gab, shp, thiskey);
  }

  assert(len - 1 == gab_shplen(shp));
  return gab_gcunlock(gab), shp;
}

int compare_value(const void *l, const void *r) {
  gab_value lhs = *(gab_value *)l;
  gab_value rhs = *(gab_value *)r;
  return lhs - rhs;
};

gab_value gab_shpwith(struct gab_triple gab, gab_value shp, gab_value key) {
  mtx_lock(&gab.eg->shapes_mtx);

  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);

  uint64_t idx = gab_shpfind(shp, key);
  if (idx != -1) {
    mtx_unlock(&gab.eg->shapes_mtx);
    return shp;
  }

  idx = gab_shptfind(shp, key);
  if (idx != -1) {
    mtx_unlock(&gab.eg->shapes_mtx);
    return v_gab_value_val_at(&s->transitions, idx * 2 + 1);
  }

  gab_value new_shape = __gab_shape(gab, s->len + 1);
  struct gab_oshape *self = GAB_VAL_TO_SHAPE(new_shape);

  if (gab_valkind(shp) != kGAB_SHAPELIST || key != gab_number(s->len))
    self->header.kind = kGAB_SHAPE;

  // Set the keys on the new shape
  memcpy(self->keys, s->keys, sizeof(gab_value) * s->len);
  self->keys[s->len] = key;

  // Push transition into parent shape
  v_gab_value_push(&s->transitions, key);
  v_gab_value_push(&s->transitions, new_shape);

  mtx_unlock(&gab.eg->shapes_mtx);
  return new_shape;
}

gab_value gab_shpwithout(struct gab_triple gab, gab_value shp, gab_value key);

gab_value setup_fibersend(struct gab_triple gab, struct gab_ofiber *self) {
  struct gab_vm *vm = &self->vm;

  memcpy(self->virtual_frame_bc,
         &(uint8_t[]){
             OP_SEND,
             fHAVE_TAIL,
             0,
             OP_RETURN,
         },
         sizeof(self->virtual_frame_bc));

  memcpy(self->virtual_frame_ks,
         &(gab_value[]){
             self->data[0],
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
         },
         sizeof(self->virtual_frame_ks));

  vm->ip = self->virtual_frame_bc;
  vm->kb = self->virtual_frame_ks;

  return __gab_obj(self);
}

gab_value gab_fiber(struct gab_triple gab, struct gab_fiber_argt args) {
  assert(gab_valkind(args.message) == kGAB_MESSAGE);

  struct gab_ofiber *self =
      GAB_CREATE_FLEX_OBJ(gab_ofiber, gab_value, args.argc + 2, kGAB_FIBER);

  self->len = args.argc + 2;

  if (args.argc) {
    assert(args.argv);
    memcpy(self->data + 2, args.argv, args.argc * sizeof(gab_value));
  }

  self->data[0] = args.message;
  self->data[1] = args.receiver;

  self->flags = gab.flags | args.flags;

  self->vm.sp = self->vm.sb;

  self->vm.sp += 3;          // Return frame data
  self->vm.fp = self->vm.sp; // Frame pointer

  // Setup main and args
  *self->vm.sp++ = args.receiver; // self
  for (uint8_t i = 0; i < args.argc; i++)
    *self->vm.sp++ = args.argv[i]; // i'th argument

  *self->vm.sp = args.argc + 1; // have

  self->vm.ip = nullptr;
  self->res_env = gab_cinvalid;

  return setup_fibersend(gab, self);
}

GAB_API inline struct gab_vm *gab_fibvm(gab_value fiber) {
  assert(gab_valkind(fiber) >= kGAB_FIBER &&
         gab_valkind(fiber) <= kGAB_FIBERRUNNING);
  return &GAB_VAL_TO_FIBER(fiber)->vm;
}

union gab_value_pair gab_tfibawait(struct gab_triple gab, gab_value f,
                                   size_t tries) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  uint64_t sofar = 0;

  while (fiber->header.kind != kGAB_FIBERDONE) {
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_union_cinvalid;
    default:
      break;
    }

    sofar++;
    if (sofar > tries)
      return (union gab_value_pair){.status = gab_ctimeout, .vresult = f};
  }

  return fiber->res_values;
}

union gab_value_pair gab_fibawait(struct gab_triple gab, gab_value f) {
  return gab_tfibawait(gab, f, (size_t)-1);
}

void *gab_fibmalloc(gab_value f, uint64_t n) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  v_uint8_t_cap(&fiber->allocator, fiber->len + n + 1);

  fiber->allocator.len += n;
  uint8_t *ptr = v_uint8_t_ref_at(&fiber->allocator, fiber->allocator.len - n);
  return ptr;
}

uint64_t gab_fibpush(gab_value f, uint8_t b) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  return v_uint8_t_push(&fiber->allocator, b);
}

uint64_t gab_wfibpush(gab_value f, uint64_t w) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  uint64_t idx = v_uint8_t_push(&fiber->allocator, w & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 8) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 16) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 24) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 32) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 40) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 48) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 56) & 0xff);
  return idx;
}

void *gab_fibat(gab_value f, uint64_t n) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  return v_uint8_t_ref_at(&fiber->allocator, n);
}

uint64_t gab_fibsize(gab_value f) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  return fiber->allocator.len;
}

void gab_fibclear(gab_value f) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  fiber->allocator.len = 0;
}

gab_value gab_fibawaite(struct gab_triple gab, gab_value f) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);

  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  while (fiber->header.kind != kGAB_FIBERDONE)
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      break;
    }

  return fiber->res_env;
}

gab_value gab_channel(struct gab_triple gab) {
  struct gab_ochannel *self = GAB_CREATE_OBJ(gab_ochannel, kGAB_CHANNEL);

  atomic_init(&self->data, nullptr);
  atomic_init(&self->len, 0);
  return __gab_obj(self);
}

void gab_chnclose(gab_value c) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  channel->header.kind = kGAB_CHANNELCLOSED;
}

bool gab_chnisclosed(gab_value c) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  return channel->header.kind == kGAB_CHANNELCLOSED;
};

bool gab_chnmatches(gab_value c, gab_value *ptr) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);
  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);
  return atomic_load(&channel->data) == ptr;
}

/*
 * The channel implementation is subtle here. There are two atomic components:
 *  - data (An atomic gab_value* which points to the beginning of the slice of
 * values in the channel)
 *  - len (An atomic uint64 which contains the number of values in the channel's
 * slice)
 *
 * Because there are *two* pieces of atomic state that need to be synced, the
 * implementation is a little more nuanced.
 *
 * "Putters" need to wait until the *data* ptr is null. This is how
 * gab_chnisfull() works. Therefore, "putters" wait in a loop like this:
 *
 * while(gab_chnisfull(channel))
 *  yield()
 *
 *  "Takers" need to wait until the *len*  is not zero. This is how
 * gab_chnisempty() works. Therefore, "takers" wait in a loop liek this:
 *
 *  while(gab_chnisempty(channel))
 *    yield()
 *
 * This way, Putters don't stomp over other putters, and they also don't stomp
 * over other takers. THis is because other putters are prevented from acting as
 * they don't have the data ptr, and takers cant act until they have the len.
 * This guarantees that no one sees the channel (Other than the putter who
 * succeeded) until the data is completely ready.
 *
 * The inverse is true for takers. Once a taker succeeds in taking the len, no
 * other takers will try. And no putters can act until the taker restores the
 * *data* atomic.
 */

bool gab_chnisempty(gab_value c) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);
  return atomic_load(&channel->len) == 0;
};

bool gab_chnisfull(gab_value c) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);
  return atomic_load(&channel->data) != nullptr;
};

/*
 * Abandon a put by storing nullptr into data.
 */
void channel_abandon(struct gab_ochannel *channel) {
  // Masquerades as a take, so has to cex the same way.
  uint64_t len = atomic_load(&channel->len);
  if (atomic_compare_exchange_strong(&channel->len, &len, 0))
    atomic_store(&channel->data, nullptr);
}

/*
 * Try to put a slice into a channel. Uses weak atomic exchange, so
 *  must be used in loops.
 */
bool channel_put(struct gab_ochannel *channel, uint64_t len, gab_value *vs) {
  static gab_value *null = nullptr;

  if (atomic_compare_exchange_weak(&channel->data, &null, vs))
    return atomic_store(&channel->len, len), true;

  return false;
}

/*
 * Try to load up to n values from the channel into dest.
 * If successful, return a gab_number of the number of values actually loaded.
 * Else return gab_cundefined.
 */
gab_value channel_take(struct gab_ochannel *channel, uint64_t n,
                       gab_value *dest) {
  gab_value *src = atomic_load(&channel->data);
  uint64_t avail = atomic_load(&channel->len);

  // If we don't have both avail and src yet, the channel is not ready.
  if (!(avail && src))
    return gab_cundefined;

  // No space to complete this take.
  // if (n < avail)
  //   // return gab_cundefined;
  //   return gab_number(-avail);
  uint64_t len = n < avail ? n : avail;
  memcpy(dest, src, sizeof(gab_value) * len);

  if (atomic_compare_exchange_weak(&channel->len, &avail, 0))
    return atomic_store(&channel->data, nullptr), gab_number(avail);
  else
    return gab_cundefined;
}

gab_value channel_block_while_full(struct gab_triple gab,
                                   struct gab_ochannel *channel, gab_value c,
                                   uint64_t tries, uint64_t *sofar) {
  while (gab_chnisfull(c)) {
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      gab_busywait(gab);
      break;
    }

    *sofar = *sofar + 1;

    if (gab_chnisclosed(c))
      return gab_cundefined;

    if (*sofar > tries)
      return gab_ctimeout;
  }

  return gab_cvalid;
}

gab_value channel_block_while_empty(struct gab_triple gab,
                                    struct gab_ochannel *channel, gab_value c,
                                    uint64_t tries, uint64_t *sofar) {
  while (gab_chnisempty(c)) {
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      gab_busywait(gab);
      break;
    }

    *sofar = *sofar + 1;

    if (gab_chnisclosed(c))
      return gab_cundefined;

    if (*sofar > tries)
      return gab_ctimeout;
  }

  return gab_cvalid;
}

gab_value unsafe_channel_blocking_put(struct gab_triple gab,
                                      struct gab_ochannel *channel, gab_value c,
                                      uint64_t len, gab_value *vs,
                                      uint64_t tries, uint64_t *sofar) {
  gab_value res = gab_cundefined;

  while (!gab_chnisclosed(c)) {
    res = channel_block_while_full(gab, channel, c, tries, sofar);

    if (res != gab_cvalid)
      return res;

    if (channel_put(channel, len, vs))
      return res;
  }

  return gab_cinvalid;
}

gab_value channel_blocking_put(struct gab_triple gab,
                               struct gab_ochannel *channel, gab_value c,
                               uint64_t len, gab_value *vs, size_t tries) {
  uint64_t sofar = 0;

  gab_value res =
      unsafe_channel_blocking_put(gab, channel, c, len, vs, tries, &sofar);

  // In any of these cases, we failed to put and
  // can forward the error.
  switch (res) {
  case gab_ctimeout:
  case gab_cinvalid:
  case gab_cundefined:
    return res;
  }

  // Wait for a taker.
  res = channel_block_while_full(gab, channel, c, tries, &sofar);

  switch (res) {
  // We were interrupted, timed out, or the channel closed.
  case gab_ctimeout:
  case gab_cinvalid:
  case gab_cundefined:
    // If a taker never arrives, we should remove our value as if our put
    // failed and return a timeout.
    return channel_abandon(channel), res;
  // A taker arrived.
  default:
    return gab_cvalid;
  }
}

/*
 * Returns
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * positive gab_number containing number of values written on success
 * negative gab_number containing number of values *would* have written, but
 * didn't have space. (failure).
 */
gab_value channel_blocking_take(struct gab_triple gab,
                                struct gab_ochannel *channel, gab_value c,
                                uint64_t len, gab_value *vs, size_t tries) {
  gab_value res = gab_cundefined;

  uint64_t sofar = 0;

  while (!gab_chnisclosed(c) && res == gab_cundefined) {
    res = channel_block_while_empty(gab, channel, c, tries, &sofar);

    if (res != gab_cvalid)
      return res;

    res = channel_take(channel, len, vs);
  }

  return res;
}

/*
 * Returns
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * gab_cvalid on success
 */
gab_value gab_ntchnput(struct gab_triple gab, gab_value c, uint64_t len,
                       gab_value *vs, uint64_t tries) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  switch (channel->header.kind) {
  case kGAB_CHANNEL:
    return channel_blocking_put(gab, channel, c, len, vs, tries);
  case kGAB_CHANNELCLOSED:
    return gab_cundefined;
  default:
    assert(false && "UNREACHABLE");
    return gab_cinvalid;
  }
}

// gab_value gab_untchntake(struct gab_triple gab, gab_value channel, uint64_t
// len,
//                          gab_value *value, uint64_t nms) {}

gab_value gab_untchnput(struct gab_triple gab, gab_value c, uint64_t len,
                        gab_value *vs, uint64_t tries) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  switch (channel->header.kind) {
  case kGAB_CHANNEL: {
    uint64_t sofar = 0;
    return unsafe_channel_blocking_put(gab, channel, c, len, vs, tries, &sofar);
  }
  case kGAB_CHANNELCLOSED:
    return gab_cundefined;
  default:
    assert(false && "UNREACHABLE");
    return gab_cinvalid;
  }
}

gab_value gab_tchnput(struct gab_triple gab, gab_value c, gab_value value,
                      uint64_t tries) {
  return gab_ntchnput(gab, c, 1, &value, tries);
}

gab_value gab_nchnput(struct gab_triple gab, gab_value channel, uint64_t len,
                      gab_value *vs) {
  gab_value v = gab_ntchnput(gab, channel, len, vs, (size_t)-1);
  assert(v != gab_ctimeout);
  return v;
}

gab_value gab_chnput(struct gab_triple gab, gab_value c, gab_value value) {
  gab_value v = gab_tchnput(gab, c, value, (size_t)-1);
  assert(v != gab_ctimeout);
  return v;
}

/*
 * Returns:
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * a gab_number on success. This number will contain the
 * amount of values the channel *had available to write*,
 * not the number that was *actually written*. To obtain the amount
 * actually written use MIN(result, len).
 */
gab_value gab_ntchntake(struct gab_triple gab, gab_value c, uint64_t len,
                        gab_value *data, uint64_t tries) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  switch (channel->header.kind) {
  case kGAB_CHANNEL:
    gab_value res = channel_blocking_take(gab, channel, c, len, data, tries);
    return res;
  case kGAB_CHANNELCLOSED:
    return gab_cundefined;
  default:
    assert(false && "Unreachable");
    return gab_cinvalid;
  }
};

gab_value gab_tchntake(struct gab_triple gab, gab_value channel,
                       uint64_t tries) {
  gab_value out;
  gab_value res = gab_ntchntake(gab, channel, 1, &out, tries);

  if (gab_valkind(res) != kGAB_NUMBER)
    return res;

  gab_int n = gab_valtoi(res);

  /* We should have received at least one value */
  assert(n >= 1);

  return out;
};

gab_value gab_nchntake(struct gab_triple gab, gab_value channel, uint64_t len,
                       gab_value *data) {
  return gab_ntchntake(gab, channel, len, data, (size_t)-1);
}

gab_value gab_chntake(struct gab_triple gab, gab_value c) {
  gab_value v = gab_tchntake(gab, c, (size_t)-1);
  assert(v != gab_ctimeout);
  return v;
}

static uint64_t dumpInstruction(FILE *stream, struct gab_oprototype *self,
                                uint64_t offset);

static uint64_t dumpSimpleInstruction(FILE *stream, struct gab_oprototype *self,
                                      uint64_t offset) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s\n", name);
  return offset + 1;
}

static uint64_t dumpSendInstruction(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  uint16_t constant =
      ((uint16_t)v_uint8_t_val_at(&self->src->bytecode, offset + 1)) << 8 |
      v_uint8_t_val_at(&self->src->bytecode, offset + 2);

  gab_value msg = v_gab_value_val_at(&self->src->constants,
                                     constant & (~(fHAVE_TAIL << 8)));

  bool tail = ((constant & (fHAVE_TAIL << 8)) != 0);

  fprintf(stream, "%-25s" GAB_BLUE, name);
  gab_fvalinspect(stream, msg, 0);
  fprintf(stream, GAB_RESET " %s\n", tail ? " [TAILCALL]" : "");

  return offset + 3;
}

static uint64_t dumpTwoByteInstruction(FILE *stream,
                                       struct gab_oprototype *self,
                                       uint64_t offset) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  uint8_t operand_a = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  uint8_t operand_b = v_uint8_t_val_at(&self->src->bytecode, offset + 2);
  fprintf(stream, "%-25s%hhx %hhx\n", name, operand_a, operand_b);
  return offset + 3;
}

static uint64_t dumpByteInstruction(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset, bool extra) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  offset += extra;

  uint8_t operand = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  fprintf(stream, "%-25s%hhx\n", name, operand);
  return offset + 2;
}

static uint64_t dumpTrimInstruction(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset) {
  uint8_t wantbyte = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s%hhx\n", name, wantbyte);
  return offset + 2;
}

static uint64_t dumpReturnInstruction(FILE *stream, struct gab_oprototype *self,
                                      uint64_t offset) {
  fprintf(stream, "%-25s\n", "RETURN");
  return offset + 1;
}

static uint64_t dumpPackInstruction(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset) {
  uint8_t operandA = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  uint8_t operandB = v_uint8_t_val_at(&self->src->bytecode, offset + 2);
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s -> %hhx %hhx\n", name, operandA, operandB);
  return offset + 3;
}

static uint64_t dumpConstantInstruction(FILE *stream,
                                        struct gab_oprototype *self,
                                        uint64_t offset, bool extra) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  offset += extra;

  uint16_t constant =
      ((uint16_t)v_uint8_t_val_at(&self->src->bytecode, offset + 1)) << 8 |
      v_uint8_t_val_at(&self->src->bytecode, offset + 2);

  fprintf(stream, "%-25s", name);
  gab_fvalinspect(stdout, v_gab_value_val_at(&self->src->constants, constant),
                  0);

  fprintf(stream, "\n");
  return offset + 3;
}

static uint64_t dumpNConstantInstruction(FILE *stream,
                                         struct gab_oprototype *self,
                                         uint64_t offset, bool extra) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  offset += extra;

  fprintf(stream, "%-25s", name);

  uint8_t n = v_uint8_t_val_at(&self->src->bytecode, offset + 1);

  for (int i = 0; i < n; i++) {
    uint16_t constant =
        ((uint16_t)v_uint8_t_val_at(&self->src->bytecode, offset + 2 + (2 * i)))
            << 8 |
        v_uint8_t_val_at(&self->src->bytecode, offset + 3 + (2 * i));

    gab_fvalinspect(stdout, v_gab_value_val_at(&self->src->constants, constant),
                    0);

    if (i < n - 1)
      fprintf(stream, ", ");
  }

  fprintf(stream, "\n");
  return offset + 2 + (2 * n);
}

static uint64_t dumpInstruction(FILE *stream, struct gab_oprototype *self,
                                uint64_t offset) {
  uint8_t op = v_uint8_t_val_at(&self->src->bytecode, offset);
  switch (op) {
  case OP_POP:
  case OP_TUPLE:
  case OP_NOP:
    return dumpSimpleInstruction(stream, self, offset);
  case OP_PACK_DICT:
  case OP_PACK_LIST:
    return dumpPackInstruction(stream, self, offset);
  case OP_TUPLE_NCONSTANT:
  case OP_NCONSTANT:
    return dumpNConstantInstruction(stream, self, offset, false);
  case OP_TUPLE_CONSTANT:
  case OP_CONSTANT:
    return dumpConstantInstruction(stream, self, offset, false);
  case OP_NTUPLE_CONSTANT:
    return dumpConstantInstruction(stream, self, offset, true);
  case OP_NTUPLE_NCONSTANT:
    return dumpNConstantInstruction(stream, self, offset, true);
  case OP_SEND:
  case OP_SEND_BLOCK:
  case OP_SEND_NATIVE:
  case OP_SEND_PROPERTY:
  case OP_SEND_PRIMITIVE_CONCAT:
  case OP_SEND_PRIMITIVE_SPLATLIST:
  case OP_SEND_PRIMITIVE_SPLATDICT:
  case OP_SEND_PRIMITIVE_ADD:
  case OP_SEND_PRIMITIVE_SUB:
  case OP_SEND_PRIMITIVE_MUL:
  case OP_SEND_PRIMITIVE_DIV:
  case OP_SEND_PRIMITIVE_MOD:
  case OP_SEND_PRIMITIVE_EQ:
  case OP_SEND_PRIMITIVE_LT:
  case OP_SEND_PRIMITIVE_LTE:
  case OP_SEND_PRIMITIVE_GT:
  case OP_SEND_PRIMITIVE_GTE:
  case OP_SEND_PRIMITIVE_CALL_BLOCK:
  case OP_SEND_PRIMITIVE_CALL_NATIVE:
  case OP_TAILSEND_BLOCK:
  case OP_TAILSEND_PRIMITIVE_CALL_BLOCK:
  case OP_LOCALSEND_BLOCK:
  case OP_LOCALTAILSEND_BLOCK:
  case OP_MATCHSEND_BLOCK:
  case OP_MATCHTAILSEND_BLOCK:
    return dumpSendInstruction(stream, self, offset);
  case OP_NTUPLE:
  case OP_POP_N:
  case OP_STORE_LOCAL:
  case OP_POPSTORE_LOCAL:
  case OP_LOAD_UPVALUE:
  case OP_LOAD_LOCAL:
  case OP_TUPLE_LOAD_LOCAL:
    return dumpByteInstruction(stream, self, offset, false);
  case OP_NTUPLE_LOAD_LOCAL:
    return dumpTwoByteInstruction(stream, self, offset);
  case OP_NTUPLE_NLOAD_LOCAL: {
    const char *name =
        gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

    uint8_t tuple_operand = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
    uint8_t operand = v_uint8_t_val_at(&self->src->bytecode, offset + 2);

    fprintf(stream, "%-25s(%hhx)%hhx: ", name, tuple_operand, operand);

    for (int i = 0; i < operand - 1; i++) {
      fprintf(stream, "%hhx, ",
              v_uint8_t_val_at(&self->src->bytecode, offset + 3 + i));
    }

    fprintf(stream, "%hhx\n",
            v_uint8_t_val_at(&self->src->bytecode, offset + 2 + operand));

    return offset + 3 + operand;
  }
  case OP_NPOPSTORE_LOCAL:
  case OP_NPOPSTORE_STORE_LOCAL:
  case OP_NLOAD_UPVALUE:
  case OP_NLOAD_LOCAL:
  case OP_TUPLE_NLOAD_LOCAL: {
    const char *name =
        gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

    uint8_t operand = v_uint8_t_val_at(&self->src->bytecode, offset + 1);

    fprintf(stream, "%-25s%hhx: ", name, operand);

    for (int i = 0; i < operand - 1; i++) {
      fprintf(stream, "%hhx, ",
              v_uint8_t_val_at(&self->src->bytecode, offset + 2 + i));
    }

    fprintf(stream, "%hhx\n",
            v_uint8_t_val_at(&self->src->bytecode, offset + 1 + operand));

    return offset + 2 + operand;
  }
  case OP_RETURN:
  case OP_RETURN_1:
  case OP_RETURN_2:
  case OP_RETURN_3:
  case OP_RETURN_4:
  case OP_RETURN_5:
  case OP_RETURN_6:
  case OP_RETURN_7:
  case OP_RETURN_8:
  case OP_RETURN_9:
    return dumpReturnInstruction(stream, self, offset);
  case OP_BLOCK: {
    offset++;

    uint16_t proto_constant =
        (((uint16_t)self->src->bytecode.data[offset] << 8) |
         self->src->bytecode.data[offset + 1]);

    offset += 2;

    gab_value pval = v_gab_value_val_at(&self->src->constants, proto_constant);

    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(pval);

    fprintf(stream, "%-25s" GAB_CYAN "%-20s\n" GAB_RESET, "OP_BLOCK",
            gab_strdata(&p->src->name));

    for (int j = 0; j < p->nupvalues; j++) {
      int isLocal = p->data[j] & fLOCAL_LOCAL;
      uint8_t index = p->data[j] >> 1;
      fprintf(stream, "      |                   %d %s\n", index,
              isLocal ? "local" : "upvalue");
    }
    return offset;
  }
  case OP_TRIM_UP1:
  case OP_TRIM_UP2:
  case OP_TRIM_UP3:
  case OP_TRIM_UP4:
  case OP_TRIM_UP5:
  case OP_TRIM_UP6:
  case OP_TRIM_UP7:
  case OP_TRIM_UP8:
  case OP_TRIM_UP9:
  case OP_TRIM_DOWN1:
  case OP_TRIM_DOWN2:
  case OP_TRIM_DOWN3:
  case OP_TRIM_DOWN4:
  case OP_TRIM_DOWN5:
  case OP_TRIM_DOWN6:
  case OP_TRIM_DOWN7:
  case OP_TRIM_DOWN8:
  case OP_TRIM_DOWN9:
  case OP_TRIM_EXACTLY0:
  case OP_TRIM_EXACTLY1:
  case OP_TRIM_EXACTLY2:
  case OP_TRIM_EXACTLY3:
  case OP_TRIM_EXACTLY4:
  case OP_TRIM_EXACTLY5:
  case OP_TRIM_EXACTLY6:
  case OP_TRIM_EXACTLY7:
  case OP_TRIM_EXACTLY8:
  case OP_TRIM_EXACTLY9:
  case OP_TRIM: {
    return dumpTrimInstruction(stream, self, offset);
  }
  default: {
    uint8_t code = v_uint8_t_val_at(&self->src->bytecode, offset);
    printf("Unknown opcode %d (%s?)\n", code, gab_opcode_names[code]);
    return offset + 1;
  }
  }
}

int gab_fmodinspect(FILE *stream, gab_value module) {
  struct gab_oprototype *proto = nullptr;

  switch (gab_valkind(module)) {
  case kGAB_BLOCK:
    proto = GAB_VAL_TO_PROTOTYPE(GAB_VAL_TO_BLOCK(module)->p);
    break;
  case kGAB_PROTOTYPE:
    proto = GAB_VAL_TO_PROTOTYPE(module);
    break;
  default:
    return -1;
  }

  uint64_t offset = proto->offset;

  uint64_t end = proto->offset + proto->len;

  gab_fvalinspect(stream, proto->src->name, 0);
  fputc('\n', stream);

  while (offset < end) {
    fprintf(stream, GAB_YELLOW "%04" PRIu64 " " GAB_RESET, offset);
    offset = dumpInstruction(stream, proto, offset);
  }

  return 0;
}

/*
 *
 * PRETTY PRINTING GAB OBJECTS
 *
 * An object produces a vector of gab_pprint structs. These are laid out
 * with a layout algorithm.
 */
enum gab_pprint_k {
  kPPRINT_VALUE,
  kPPRINT_ADDRESS,
  kPPRINT_STRING,
  kPPRINT_BINARY,
  kPPRINT_BREAK,
  kPPRINT_SPACE,
  kPPRINT_COMMA,
  kPPRINT_INDENT,
  kPPRINT_DEDENT,
};

struct gab_pprint {
  enum gab_pprint_k k; /* Kind of the token */
  int32_t width;       /* Pre-computed width of the token */
  union gab_pprint_d {
    gab_value val; /* Gab value to be printed with this token. This should be a
                 primitive value, not a nested one. */
    char c;
    const char *s;
    void *addr;
  } as;
};

#define T struct gab_pprint
#define NAME gab_pprint
#include "vector.h"

int32_t pprint_width(gab_value val) {
  switch (gab_valkind(val)) {
  case kGAB_STRING:
    return gab_strlen(val);
  case kGAB_MESSAGE:
    return gab_strlen(val) + 1;
  case kGAB_BINARY:
    return gab_strlen(val) * 2 + 15;
  case kGAB_NATIVE:
    return gab_strlen(GAB_VAL_TO_NATIVE(val)->name) + 13;
  case kGAB_BOX:
    return pprint_width(GAB_VAL_TO_BOX(val)->type) + 10;
  case kGAB_PRIMITIVE:
    switch (val) {
    case gab_cundefined:
      return strlen("cundefined");
    case gab_cinvalid:
      return strlen("cinvalid");
    case gab_ctimeout:
      return strlen("ctimeout");
    case gab_cvalid:
      return strlen("cvalid");
    default:
      return strlen(gab_opcode_names[gab_valtop(val)]) + 3;
    }
  case kGAB_BLOCK: {
    struct gab_oblock *blk = GAB_VAL_TO_BLOCK(val);
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);
    return gab_strlen(gab_srcname(p->src)) + 4 + 4;
  }
  case kGAB_NUMBER:
    char buf[20] = {};
    // Maybe I can just return number of bytes written?
    snprintf(buf, sizeof(buf), "%lg", gab_valtof(val));
    return strlen(buf);
  case kGAB_CHANNEL:
  case kGAB_CHANNELCLOSED:
    return 13; // Hardcoded and never change
  case kGAB_FIBER:
  case kGAB_FIBERDONE:
  case kGAB_FIBERRUNNING:
    return 11; // Needs to be updated to match
  default:
    fprintf(stderr, "unrecognized kind in pprint width: %d\n",
            gab_valkind(val));
    fflush(stderr);
    assert(false && "unreachable");
    return 0;
  }
}

void push_pprint_v(v_gab_pprint *self, gab_value val) {
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = kPPRINT_VALUE,
                              .width = pprint_width(val),
                              .as.val = val,
                          });
}

void push_pprint_p(v_gab_pprint *self, void *p) {
  char buf[30] = {};
  // Maybe I can just return number of bytes written?
  snprintf(buf, sizeof(buf), "%p", p);
  int width = strlen(buf);

  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = kPPRINT_ADDRESS,
                              .width = width,
                              .as.addr = p,
                          });
}

void push_pprint_k(v_gab_pprint *self, enum gab_pprint_k k) {
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = k,
                              .width = 1,
                              .as.val = gab_nil,
                          });
}

void push_pprint_s(v_gab_pprint *self, const char *s) {
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = kPPRINT_STRING,
                              .width = strlen(s),
                              .as.s = s,
                          });
}

void push_pprint_b(v_gab_pprint *self, const char *s) {}

void push_pprint_kd(v_gab_pprint *self, enum gab_pprint_k k,
                    union gab_pprint_d d) {
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = k,
                              .width = 1,
                              .as = d,
                          });
}

bool pprint_tokify(v_gab_pprint *self, gab_value val);

void pprint_rec(v_gab_pprint *self, gab_value rec) {
  assert(gab_valkind(rec) == kGAB_RECORD);

  push_pprint_kd(self, kPPRINT_INDENT, (union gab_pprint_d){'{'});
  push_pprint_k(self, kPPRINT_SPACE);

  uint64_t len = gab_reclen(rec);

  for (uint64_t i = 0; i < len; i++) {
    pprint_tokify(self, gab_ukrecat(rec, i));

    push_pprint_s(self, " ");

    pprint_tokify(self, gab_uvrecat(rec, i));

    if (i + 1 < len)
      push_pprint_k(self, kPPRINT_COMMA);

    push_pprint_k(self, kPPRINT_SPACE);
  }

  push_pprint_kd(self, kPPRINT_DEDENT, (union gab_pprint_d){'}'});
};

void pprint_channel(v_gab_pprint *self, gab_value chan) {
  assert(gab_valkind(chan) == kGAB_CHANNEL ||
         gab_valkind(chan) == kGAB_CHANNELCLOSED);

  struct gab_ochannel *ch = GAB_VAL_TO_CHANNEL(chan);
  push_pprint_kd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  push_pprint_s(self, tGAB_CHANNEL);
  push_pprint_k(self, kPPRINT_SPACE);
  push_pprint_p(self, ch);
  push_pprint_kd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

void pprint_fiber(v_gab_pprint *self, gab_value fib) {
  assert(gab_valkind(fib) == kGAB_FIBER ||
         gab_valkind(fib) == kGAB_FIBERRUNNING ||
         gab_valkind(fib) == kGAB_FIBERDONE);

  struct gab_ofiber *v = GAB_VAL_TO_FIBER(fib);

  gab_value msg = v->data[0];
  gab_value rec = v->data[1];

  push_pprint_kd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  push_pprint_s(self, tGAB_FIBER);
  push_pprint_k(self, kPPRINT_SPACE);
  push_pprint_p(self, v);
  push_pprint_k(self, kPPRINT_SPACE);
  pprint_tokify(self, msg);
  push_pprint_k(self, kPPRINT_SPACE);
  pprint_tokify(self, rec);
  push_pprint_kd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

void pprint_box(v_gab_pprint *self, gab_value box) {
  assert(gab_valkind(box) == kGAB_BOX);
  struct gab_obox *v = GAB_VAL_TO_BOX(box);
  push_pprint_kd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  push_pprint_s(self, tGAB_BOX);
  push_pprint_k(self, kPPRINT_SPACE);
  pprint_tokify(self, v->type);
  push_pprint_kd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

void pprint_binary(v_gab_pprint *self, gab_value bin) {
  assert(gab_valkind(bin) == kGAB_BINARY);
  push_pprint_kd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  push_pprint_s(self, tGAB_BINARY);
  push_pprint_k(self, kPPRINT_SPACE);
  push_pprint_s(self, "0x");
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = kPPRINT_BINARY,
                              .width = gab_strlen(bin),
                              .as.val = bin,
                          });
  push_pprint_kd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

void pprint_native(v_gab_pprint *self, gab_value ntv) {
  assert(gab_valkind(ntv) == kGAB_NATIVE);
  struct gab_onative *v = GAB_VAL_TO_NATIVE(ntv);
  push_pprint_kd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  push_pprint_s(self, tGAB_NATIVE);
  push_pprint_k(self, kPPRINT_SPACE);
  push_pprint_v(self, v->name);
  push_pprint_kd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

void pprint_block(v_gab_pprint *self, gab_value block) {
  assert(gab_valkind(block) == kGAB_BLOCK);

  struct gab_oblock *blk = GAB_VAL_TO_BLOCK(block);
  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);
  uint64_t line = gab_srcline(p->src, p->offset);

  push_pprint_kd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  push_pprint_s(self, tGAB_BLOCK);
  push_pprint_k(self, kPPRINT_SPACE);
  push_pprint_v(self, gab_srcname(p->src));
  push_pprint_k(self, kPPRINT_SPACE);
  push_pprint_v(self, gab_number(line));
  push_pprint_kd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}
void pprint_shape(v_gab_pprint *self, gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);

  push_pprint_kd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  push_pprint_s(self, tGAB_SHAPE);
  push_pprint_k(self, kPPRINT_SPACE);

  uint64_t len = gab_shplen(shp);
  for (uint64_t i = 0; i < len; i++) {
    pprint_tokify(self, gab_ushpat(shp, i));
    push_pprint_k(self, kPPRINT_SPACE);
  }

  push_pprint_kd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}
void pprint_reclist(v_gab_pprint *self, gab_value rec) {
  assert(gab_valkind(rec) == kGAB_RECORD);

  push_pprint_kd(self, kPPRINT_INDENT, (union gab_pprint_d){'['});
  push_pprint_k(self, kPPRINT_SPACE);

  uint64_t len = gab_reclen(rec);

  for (uint64_t i = 0; i < len; i++) {
    pprint_tokify(self, gab_uvrecat(rec, i));

    if (i + 1 < len)
      push_pprint_k(self, kPPRINT_COMMA);

    push_pprint_k(self, kPPRINT_SPACE);
  }

  push_pprint_kd(self, kPPRINT_DEDENT, (union gab_pprint_d){']'});
}

bool pprint_tokify(v_gab_pprint *self, gab_value val) {
  switch (gab_valkind(val)) {
  case kGAB_SHAPE:
  case kGAB_SHAPELIST:
    return pprint_shape(self, val), true;
  case kGAB_BLOCK:
    return pprint_block(self, val), false;
  case kGAB_BOX:
    return pprint_box(self, val), false;
  case kGAB_NATIVE:
    return pprint_native(self, val), false;
  case kGAB_BINARY:
    return pprint_binary(self, val), false;
  case kGAB_RECORD:
    return (gab_recisl(val) ? pprint_reclist(self, val)
                            : pprint_rec(self, val)),
           true;
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE:
    return pprint_fiber(self, val), false;
  case kGAB_CHANNEL:
  case kGAB_CHANNELCLOSED:
    return pprint_channel(self, val), false;
  default:
    return push_pprint_v(self, val), false;
  }
}

const char *colorforkind(gab_value v) {
  switch (gab_valkind(v)) {
  case kGAB_STRING:
    return GAB_GREEN;
  case kGAB_NUMBER:
    return GAB_YELLOW;
  case kGAB_MESSAGE:
    return GAB_CYAN;
  case kGAB_PRIMITIVE:
    return GAB_RED;
  case kGAB_CHANNEL:
  case kGAB_CHANNELCLOSED:
    return GAB_MAGENTA;
  default:
    return "";
  }
}

int spprint_through(char **dest, size_t *n, struct gab_pprint t) {
  switch (t.k) {
  case kPPRINT_SPACE:
    return snprintf_through(dest, n, " ");
  case kPPRINT_COMMA:
    return snprintf_through(dest, n, ",");
  case kPPRINT_INDENT:
    return snprintf_through(dest, n, GAB_YELLOW "%c" GAB_RESET, t.as.c);
  case kPPRINT_DEDENT:
    return snprintf_through(dest, n, GAB_YELLOW "%c" GAB_RESET, t.as.c);
  case kPPRINT_BREAK:
    return snprintf_through(dest, n, "\n");
  case kPPRINT_ADDRESS:
    return snprintf_through(dest, n, GAB_MAGENTA "%p" GAB_RESET, t.as.addr);
  case kPPRINT_STRING:
    return snprintf_through(dest, n, GAB_YELLOW "%.*s" GAB_RESET, t.width,
                            t.as.s);
  case kPPRINT_BINARY: {
    const char *s = gab_strdata(&t.as.val);
    int64_t len = t.width;

    if (snprintf_through(dest, n, GAB_YELLOW) < 0)
      return -1;

    if (len < cGAB_BINARY_LEN_CUTOFF) {
      while (len--) {
        if (snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;
      }
    } else {
      uint64_t preview = cGAB_BINARY_LEN_CUTOFF;
      while (preview--)
        if (snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;

      if (snprintf_through(dest, n, "...") < 0)
        return -1;
    }

    return snprintf_through(dest, n, GAB_RESET);
  }
  case kPPRINT_VALUE:
    // Depth should be irrelevant bc these should be
    // primitives only
    const char *color = colorforkind(t.as.val);
    if (snprintf_through(dest, n, "%s", color) < 0)
      return -1;

    if (sinspectval(dest, n, t.as.val, 1) < 0)
      return -1;

    return snprintf_through(dest, n, GAB_RESET);
  }
}

/*
 * LAYOUT ALGORITHM:
 * - We are given a list of tokens.
 * - Given a configured width (40 columns),
 *   convert some SPACE tokens to BREAK tokens
 *   such that the string fits in the given width.
 *
 *          { a: 'hi', b: 'world', c: [1, 2] }
 *  Spaces   ^  ^     ^  ^        ^  ^   ^  ^
 * Indents  ^                         ^
 * Dedents                                 ^ ^
 *    Seps           ^           ^      ^
 *
 *    => {
 *          a: 'hi',
 *          b: 'world',
 *          c: [1, 2],
 *       }
 *
 *  Track a stack of indents.
 *  Try to layout indent on one line.
 *  if width < 40
 *    all done
 *  else
 *    backtrack to INDENT token.
 *    Increment indent count.
 *    Layout until DEDENT is found
 *    by converting SPACE to BREAK
 */

struct layout {
  int32_t t, w;
};

struct layout dolayout(v_gab_pprint *self, int32_t t, int32_t indent);

// Compute if the whole indentation can be laid out on one line.
struct layout layout_line(v_gab_pprint *self, int32_t t, int32_t width) {
  // Begin with the given amount of indent. An 'indent' is two spaces (not a
  // tab) for now.
  assert(v_gab_pprint_val_at(self, t).k == kPPRINT_INDENT);

  struct layout l = {.t = t, .w = width};

  for (struct gab_pprint p = v_gab_pprint_val_at(self, ++l.t);
       p.k != kPPRINT_DEDENT; p = v_gab_pprint_val_at(self, ++l.t)) {
    assert(l.t < self->len);
    l.w += p.width;

    if (p.k == kPPRINT_INDENT)
      l = layout_line(self, l.t, l.w);

    if (l.t < 0 || l.w > 40)
      return (struct layout){-1};
  }

  return l;
};

struct layout layout_multi(v_gab_pprint *self, int32_t t, int32_t indent) {
  assert(v_gab_pprint_val_at(self, t).k == kPPRINT_INDENT);

  for (struct gab_pprint *p = v_gab_pprint_ref_at(self, ++t);
       p->k != kPPRINT_DEDENT; p = v_gab_pprint_ref_at(self, ++t)) {
    assert(t < self->len);

    if (p->k == kPPRINT_SPACE)
      p->k = kPPRINT_BREAK;

    if (p->k == kPPRINT_INDENT)
      t = dolayout(self, t, indent).t;
  }

  return (struct layout){t};
}

struct layout dolayout(v_gab_pprint *self, int32_t t, int32_t indent) {
  struct layout l = layout_line(self, t, indent * 2);

  if (l.t < 0)
    return layout_multi(self, t, indent + 1);

  return l;
}

int spprint_tokens(char **dest, size_t *n, v_gab_pprint *self,
                   const char *prefix) {
  int32_t indent = 0;

  if (snprintf_through(dest, n, "%s", prefix) < 0)
    return -1;

  for (uint64_t i = 0; i < self->len; i++) {
    struct gab_pprint t = v_gab_pprint_val_at(self, i);

    if (t.k == kPPRINT_INDENT)
      indent++;

    // Dedent needs to be applied *before* we
    // draw the dedent token
    if (i + 1 < self->len)
      if (v_gab_pprint_val_at(self, i + 1).k == kPPRINT_DEDENT)
        indent--;

    if (spprint_through(dest, n, t) < 0)
      return -1;

    if (t.k == kPPRINT_BREAK) {
      if (snprintf_through(dest, n, "%s", prefix) < 0)
        return -1;

      for (int32_t i = 0; i < indent; i++)
        if (snprintf_through(dest, n, "  ") < 0)
          return -1;
    };
  }

  return 0;
}

int gab_psvalinspect(char **dest, size_t *n, gab_value value,
                     const char *prefix, int depth) {
  v_gab_pprint tokens = {};

  if (pprint_tokify(&tokens, value))
    dolayout(&tokens, 0, 0);

  if (spprint_tokens(dest, n, &tokens, prefix) < 0)
    return v_gab_pprint_destroy(&tokens), -1;

  return v_gab_pprint_destroy(&tokens), 0;
}

#undef CREATE_GAB_FLEX_OBJ
#undef CREATE_GAB_OBJ

// -- PARSER --

#define FMT_MALFORMED_EXPRESSION                                               \
  "Expressions start with one of the following values:\n\n"                    \
  "  " GAB_YELLOW "-1.23" GAB_MAGENTA "\t\t\t# A number \n" GAB_RESET          \
  "  " GAB_GREEN "'hello, Joe!'" GAB_MAGENTA "\t\t# A string \n" GAB_RESET     \
  "  " GAB_RED "greet:" GAB_MAGENTA "\t\t# A message\n" GAB_RESET              \
  "  " GAB_BLUE "x :: x + 1" GAB_MAGENTA "\t\t# A block \n" GAB_RESET            \
  "  " GAB_CYAN "{ key: value }" GAB_MAGENTA "\t# A record\n" GAB_RESET "  "   \
  "(" GAB_YELLOW "0x22" GAB_RESET ", " GAB_GREEN "true:" GAB_RESET             \
  ")" GAB_MAGENTA "\t\t# A tuple\n" GAB_RESET "  "                               \
  "a_variable" GAB_MAGENTA "\t\t# Or a variable!" GAB_RESET

#define FMT_ID_NOT_FOUND                                                       \
  "Symbol @ is not yet bound in this scope, nor in parent scopes.\n\n"             \
  "Assignment expressions bind values to symbols.\n\n"                         \
  "  a := " GAB_CYAN "true:" GAB_RESET "\n\n"                                   \
  "Symbols within local scope may be rebound at any time.\n\n"                 \
  "  name := " GAB_GREEN "\"Bob\"" GAB_RESET "\n\n"                             \
  "  name := " GAB_GREEN "\"Uncle Bob\"" GAB_RESET "\n\n"                       \
  "Symbols captured from parent scopes may not be rebound.\n\n"                \
  "  name := " GAB_GREEN "\"Uncle Bob\"" GAB_RESET "\n\n"                       \
  "  () :: name := " GAB_GREEN "\"Old Bob\"" GAB_MAGENTA " # Not allowed"

#define FMT_MALFORMED_ASSIGNMENT                                               \
  "This assignment is malformed - a valid assignment looks like:\n\n"          \
  "  a := " GAB_YELLOW "1" GAB_MAGENTA                                          \
  "\t\t# A single variable and expression\n" GAB_RESET " " GAB_BLACK         \
  "=> a := 1\n" GAB_RESET "  (a, b) := (" GAB_YELLOW "1" GAB_RESET ", " GAB_RED  \
  "bark:" GAB_RESET ")" GAB_MAGENTA                                            \
  "\t# A tuple of variables and expressions\n" GAB_RESET " " GAB_BLACK         \
  "=> a := 1, b := bark:\n" GAB_RESET "  (a*, b) := (" GAB_YELLOW "1" GAB_RESET   \
  ", " GAB_YELLOW "2" GAB_RESET ", " GAB_YELLOW "3" GAB_RESET ")" GAB_MAGENTA  \
  "\t# Specify one variable to collect extra values with '*'\n" GAB_RESET      \
  " " GAB_BLACK "=> a := [1, 2], b := 3\n" GAB_RESET "  (a**) := (" GAB_RED       \
  "num:" GAB_RESET ", " GAB_YELLOW "2" GAB_RESET ")" GAB_MAGENTA               \
  "\t# Specify one variable to zip extra values with '**'\n" GAB_RESET         \
  " " GAB_BLACK "=> a := { num: 2 }\n" GAB_RESET

#define FMT_MALFORMED_ASSIGNMENT_NOTE "\nHint: "

#define FMT_GAB_MALFORMED_STRING                                               \
  "\nSingle quoted strings can contain escape "                                \
  "sequences.\n"                                                               \
  "\n   " GAB_GREEN "'a newline -> " GAB_MAGENTA "\\n" GAB_GREEN               \
  ", or a forward slash -> " GAB_MAGENTA "\\\\" GAB_GREEN "'" GAB_RESET        \
  "\n   " GAB_GREEN "'a valid unicode codepoint: " GAB_MAGENTA                 \
  "\\u[" GAB_YELLOW "2502" GAB_MAGENTA "]" GAB_GREEN "'" GAB_RESET

/*
 *******
 * AST *
 *******

  The gab ast is built of two kinds of nodes:
    - Sends  (behavior)
    - Values (data)

  VALUE NODE

  1       => [ 1 ]
  (1,2,3) => [1, 2, 3]

  Simply a list of 0 or more values

  SEND VALUE

  1 + 1   => [{ gab.lhs: [1], gab.msg: +, gab.rhs: [1] }]

  [ 1, 2 ] => [{ gab.lhs: [:gab.list], gab.msg: make, gab.rhs: [1, 2] }]

  Simply a record with a lhs, rhs, and msg.

  BLOCK VALUE

  do            => [[
    b .= 1 + 1       { gab.lhs: [b], gab.msg: =, gab.rhs: [{ 1, b, 1}] },
    b.toString       { gab.lhs: [b], gab.msg: toString, gab.rhs: [] }
  end             ]]

  And thats it! All Gab code can be described here. There are some nuances
 though:

      - Blocks (more specifically, prototypes) are given a shape just like
 records have.
      - gab_compile() accepts a *shape* as an argument. This shape determines
 the environment available as the AST is compiled into a block.
          -> How does this handle nested scopes and chaining?
          -> How do we implement load_local/load_upvalue
 */

struct parser {
  struct gab_src *src;
  size_t offset;
  gab_value err;
};

struct bc {
  v_uint8_t bc;
  v_uint64_t bc_toks;
  v_gab_value *ks;

  struct gab_src *src;

  uint8_t prev_op, pprev_op;
  size_t prev_op_at;

  gab_value err;
};

enum prec_k { kNONE, kEXP, kBINARY_SEND, kSEND, kBUILTIN, kPRIMARY };

typedef gab_value (*parse_f)(struct gab_triple gab, struct parser *,
                             gab_value lhs);

struct parse_rule {
  parse_f prefix;
  parse_f infix;
  enum prec_k prec;
};

struct parse_rule get_parse_rule(gab_token k);

/*static size_t prev_line(struct parser *parser) {*/
/*  return v_uint64_t_val_at(&parser->src->token_lines, parser->offset - 1);*/
/*}*/

static gab_token curr_tok(struct parser *parser) {
  return v_gab_token_val_at(&parser->src->tokens, parser->offset);
}

static bool curr_prefix(struct parser *parser) {
  return get_parse_rule(curr_tok(parser)).prefix != nullptr;
}

static gab_token prev_tok(struct parser *parser) {
  return v_gab_token_val_at(&parser->src->tokens, parser->offset - 1);
}

static s_char prev_src(struct parser *parser) {
  return v_s_char_val_at(&parser->src->token_srcs, parser->offset - 1);
}

static gab_value prev_id(struct gab_triple gab, struct parser *parser) {
  s_char s = prev_src(parser);

  return gab_nstring(gab, s.len, s.data);
}

bool msg_is_builtin(struct gab_triple gab, gab_value msg) {
  if (gab_valkind(msg) != kGAB_BINARY)
    return false;

  if (msg == gab_binary(gab, mGAB_ASSIGN))
    return true;

  if (msg == gab_binary(gab, mGAB_BLOCK))
    return true;

  return false;
}

static int encode_codepoint(char *out, int utf) {
  if (utf <= 0x7F) {
    // Plain ASCII
    out[0] = (char)utf;
    return 1;
  } else if (utf <= 0x07FF) {
    // 2-byte unicode
    out[0] = (char)(((utf >> 6) & 0x1F) | 0xC0);
    out[1] = (char)(((utf >> 0) & 0x3F) | 0x80);
    return 2;
  } else if (utf <= 0xFFFF) {
    // 3-byte unicode
    out[0] = (char)(((utf >> 12) & 0x0F) | 0xE0);
    out[1] = (char)(((utf >> 6) & 0x3F) | 0x80);
    out[2] = (char)(((utf >> 0) & 0x3F) | 0x80);
    return 3;
  } else if (utf <= 0x10FFFF) {
    // 4-byte unicode
    out[0] = (char)(((utf >> 18) & 0x07) | 0xF0);
    out[1] = (char)(((utf >> 12) & 0x3F) | 0x80);
    out[2] = (char)(((utf >> 6) & 0x3F) | 0x80);
    out[3] = (char)(((utf >> 0) & 0x3F) | 0x80);
    return 4;
  }

  return 0;
}

static a_char *parse_raw_str(struct parser *parser, s_char raw_str) {
  // The parsed string will be at most as long as the raw string.
  // (\n -> one char)
  char buffer[raw_str.len];
  size_t buf_end = 0;

  // Skip the first and last bytes of the string.
  // These are the opening/closing quotes, doublequotes, or brackets (For
  // interpolation).
  for (size_t i = 1; i < raw_str.len - 1; i++) {
    int8_t c = raw_str.data[i];

    if (c == '\\') {
      i++;

      switch (raw_str.data[i]) {
      case 'r':
        buffer[buf_end++] = '\r';
        break;
      case 'n':
        buffer[buf_end++] = '\n';
        break;
      case 't':
        buffer[buf_end++] = '\t';
        break;
      case '{':
        buffer[buf_end++] = '{';
        break;
      case '"':
        buffer[buf_end++] = '"';
        break;
      case '0':
        buffer[buf_end++] = '\0';
        break;
      case '\'':
        buffer[buf_end++] = '\'';
        break;
      case '\\':
        buffer[buf_end++] = '\\';
        break;
      case 'e':
        buffer[buf_end++] = '\033';
        break;
      case 'u':
        i++;

        if (raw_str.data[i] != '[')
          return nullptr;

        i++;

        uint8_t cpl = 0;
        char codepoint[8] = {0};

        while (raw_str.data[i] != ']') {

          if (cpl == 7)
            return nullptr;

          if (i >= raw_str.len)
            return nullptr;

          codepoint[cpl++] = raw_str.data[i++];
        }

        char *endptr = nullptr;
        long cp = strtol(codepoint, &endptr, 16);

        if (*codepoint == '\0' || *endptr != '\0')
          return nullptr;

        int result = encode_codepoint(buffer + buf_end, cp);

        if (!result)
          return nullptr;

        buf_end += result;

        break;
      default:
        return nullptr;
      }
    } else {
      buffer[buf_end++] = c;
    }
  }

  return a_char_create(buffer, buf_end);
};

static gab_value trimfront_prev_id(struct gab_triple gab,
                                   struct parser *parser) {
  s_char s = prev_src(parser);

  s.data++;
  s.len--;

  // These can cause collections during compilation.
  return gab_nstring(gab, s.len, s.data);
}

static gab_value trimback_prev_id(struct gab_triple gab,
                                  struct parser *parser) {
  s_char s = prev_src(parser);

  s.len--;

  // These can cause collections during compilation.
  return gab_nstring(gab, s.len, s.data);
}

static gab_value trim_prev_id(struct gab_triple gab, struct parser *parser) {
  s_char s = prev_src(parser);

  s.data++;
  s.len -= 2;

  // These can cause collections during compilation.
  return gab_nstring(gab, s.len, s.data);
}

static inline bool match_token(struct parser *parser, gab_token tok) {
  return v_gab_token_val_at(&parser->src->tokens, parser->offset) == tok;
}

static int vparser_error(struct gab_triple gab, struct parser *parser,
                         enum gab_status e, const char *fmt, va_list args) {
  parser->err = gab_vspanicf(gab, args,
                             (struct gab_err_argt){
                                 .src = parser->src,
                                 .status = e,
                                 .tok = parser->offset ? parser->offset - 1 : 0,
                                 .note_fmt = fmt,
                             });

  va_end(args);

  return 0;
}

static int parser_error(struct gab_triple gab, struct parser *parser,
                        enum gab_status e, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  return vparser_error(gab, parser, e, fmt, args);
}

static int eat_token(struct gab_triple gab, struct parser *parser) {
  if (match_token(parser, TOKEN_EOF))
    return parser_error(gab, parser, GAB_UNEXPECTED_EOF,
                        "Unexpectedly reached the end of input.");

  parser->offset++;

  if (match_token(parser, TOKEN_ERROR)) {
    eat_token(gab, parser);
    return parser_error(gab, parser, GAB_MALFORMED_TOKEN,
                        "This token is malformed or unrecognized.");
  }

  return 1;
}

static inline int match_and_eat_token_of(struct gab_triple gab,
                                         struct parser *parser, size_t len,
                                         gab_token tok[len]) {
  for (size_t i = 0; i < len; i++)
    if (match_token(parser, tok[i]))
      return (tok[i] == TOKEN_EOF) ? 1 : eat_token(gab, parser);

  return 0;
}

#define match_and_eat_token(gab, parser, ...)                                  \
  ({                                                                           \
    gab_token toks[] = {__VA_ARGS__};                                          \
    match_and_eat_token_of(gab, parser, sizeof(toks) / sizeof(gab_token),      \
                           toks);                                              \
  })

gab_value parse_expression(struct gab_triple gab, struct parser *parser,
                           enum prec_k prec);

static inline void skip_newlines(struct gab_triple gab, struct parser *parser) {
  while (match_and_eat_token(gab, parser, TOKEN_NEWLINE))
    ;
}

size_t node_getinfo_begin(struct gab_src *src, gab_value node) {
  assert(d_uint64_t_exists(&src->node_begin_toks, node));
  return d_uint64_t_read(&src->node_begin_toks, node);
}

size_t node_getinfo_end(struct gab_src *src, gab_value node) {
  assert(d_uint64_t_exists(&src->node_end_toks, node));
  return d_uint64_t_read(&src->node_end_toks, node);
}

gab_value node_storeinfo(struct gab_src *src, gab_value node, size_t begin,
                         size_t end) {
  d_uint64_t_insert(&src->node_begin_toks, node, begin);
  d_uint64_t_insert(&src->node_end_toks, node, end);
  return node;
}

gab_value node_stealinfo(struct gab_src *src, gab_value from, gab_value to) {
  size_t begin = d_uint64_t_read(&src->node_begin_toks, from);
  size_t end = d_uint64_t_read(&src->node_end_toks, from);
  return node_storeinfo(src, to, begin, end);
}

gab_value node_value(struct gab_triple gab, gab_value node) {
  return gab_listof(gab, node);
}

gab_value node_empty(struct gab_triple gab, struct parser *parser) {
  gab_value empty = gab_listof(gab);
  node_storeinfo(parser->src, empty, parser->offset, parser->offset);
  return empty;
  ;
}

bool node_isempty(gab_value node) {
  return gab_valkind(node) == kGAB_RECORD && gab_reclen(node) == 0;
}

bool node_issend(struct gab_triple gab, gab_value node) {
  if (gab_valkind(node) != kGAB_RECORD)
    return false;

  if (gab_recisl(node))
    return false;

  return !msg_is_builtin(gab, gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG));
}

bool node_ismulti(struct gab_triple gab, gab_value node) {
  if (gab_valkind(node) != kGAB_RECORD)
    return false;

  switch (gab_valkind(gab_recshp(node))) {
  case kGAB_SHAPE:
    return !msg_is_builtin(gab, gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG));
  case kGAB_SHAPELIST: {
    size_t len = gab_reclen(node);

    if (len == 0)
      return false;

    for (size_t i = 0; i < len; i++) {
      gab_value child_node = gab_uvrecat(node, i);
      if (node_ismulti(gab, child_node))
        return true;
    }

    return false;
  }
  default:
    assert(false && "UNREACHABLE");
    return false;
  }
}

size_t node_len(struct gab_triple gab, gab_value node);

gab_value node_tuple_lastnode(gab_value node) {
  assert(gab_valkind(node) == kGAB_RECORD);
  assert(gab_valkind(gab_recshp(node)) == kGAB_SHAPELIST);
  size_t len = gab_reclen(node);
  assert(len > 0);
  return gab_uvrecat(node, len - 1);
}

size_t node_valuelen(struct gab_triple gab, gab_value node) {
  // If this value node is a block, get the
  // last tuple in the block and return that
  // tuple's len
  if (gab_valkind(node) == kGAB_RECORD)
    if (gab_valkind(gab_recshp(node)) == kGAB_SHAPELIST)
      if (gab_reclen(node) > 0)
        return node_len(gab, node_tuple_lastnode(node));

  // Otherwise, values are 1 long
  return 1;
}

size_t node_len(struct gab_triple gab, gab_value node) {
  if (gab_valkind(node) != kGAB_RECORD)
    return 0;

  gab_assert(gab_valkind(node) == kGAB_RECORD, "Unreachable sanity check");
  gab_assert(gab_valkind(gab_recshp(node)) == kGAB_SHAPELIST,
             "Tried to get length of non-list record node.");

  size_t len = gab_reclen(node);
  size_t total_len = 0;

  // Tuple's length are the sum of their children
  // If the tuple as a whole is multi, subtract one
  // for that send
  for (size_t i = 0; i < len; i++)
    total_len += node_valuelen(gab, gab_uvrecat(node, i));

  return total_len;
}

gab_value node_send(struct gab_triple gab, gab_value lhs, gab_value msg,
                    gab_value rhs) {
  static const char *keys[] = {
      mGAB_AST_NODE_SEND_LHS,
      mGAB_AST_NODE_SEND_MSG,
      mGAB_AST_NODE_SEND_RHS,
  };

  gab_value vals[] = {
      lhs,
      msg,
      rhs,
  };

  return node_value(gab, gab_mrecord(gab, 1, 3, keys, vals));
}

static gab_value parse_expressions_body(struct gab_triple gab,
                                        struct parser *parser,
                                        enum gab_token t) {
  size_t begin = parser->offset;

  gab_value result = node_empty(gab, parser);

  skip_newlines(gab, parser);

  while (!match_and_eat_token(gab, parser, t)) {
    skip_newlines(gab, parser);

    gab_value exp = parse_expression(gab, parser, kEXP);

    if (exp == gab_cinvalid)
      return gab_cinvalid;

    gab_value tup = node_value(gab, exp);
    node_stealinfo(parser->src, exp, tup);

    result = gab_lstcat(gab, result, tup);

    if (result == gab_cinvalid)
      return gab_cinvalid;

    skip_newlines(gab, parser);
  }

  size_t end = parser->offset;

  gab_value res = node_value(gab, result);

  node_storeinfo(parser->src, res, begin, end);

  return res;
}

gab_value parse_expressions_until(struct gab_triple gab, struct parser *parser,
                                  enum gab_token t) {
  size_t begin = parser->offset;

  gab_value result = node_empty(gab, parser);

  skip_newlines(gab, parser);

  while (!match_and_eat_token(gab, parser, t)) {
    skip_newlines(gab, parser);

    gab_value exp = parse_expression(gab, parser, kEXP);

    if (exp == gab_cinvalid)
      return gab_cinvalid;

    result = gab_lstcat(gab, result, exp);

    if (result == gab_cinvalid)
      return gab_cinvalid;

    skip_newlines(gab, parser);
  }

  size_t end = parser->offset;

  node_storeinfo(parser->src, result, begin, end);

  return result;
}

gab_value parse_expression(struct gab_triple gab, struct parser *parser,
                           enum prec_k prec) {
  if (!eat_token(gab, parser))
    return gab_cinvalid;

  size_t tok = prev_tok(parser);

  struct parse_rule rule = get_parse_rule(tok);

  if (rule.prefix == nullptr)
    return parser_error(gab, parser, GAB_MALFORMED_EXPRESSION,
                        FMT_MALFORMED_EXPRESSION),
           gab_cinvalid;

  size_t begin = parser->offset;

  gab_value node = rule.prefix(gab, parser, gab_cinvalid);

  size_t end = parser->offset;
  size_t latest_valid_offset = parser->offset;

  node_storeinfo(parser->src, node, begin, end);

  skip_newlines(gab, parser);

  /*
   * The next section will skip newlines to peek and see
   * if we have an infix expression to continue.
   *
   * If we don't find one, we need to *backtrack* the
   * parser to where our initial prefix expression left off.
   *
   * This is because newlines are *expected* in some places as
   * separators. (tuples, lists, and dicts)
   */
  while (prec <= get_parse_rule(curr_tok(parser)).prec) {

    if (node == gab_cinvalid)
      return gab_cinvalid;

    if (!eat_token(gab, parser))
      return gab_cinvalid;

    rule = get_parse_rule(prev_tok(parser));

    if (rule.infix != nullptr)
      node = rule.infix(gab, parser, node);

    latest_valid_offset = parser->offset;

    skip_newlines(gab, parser);
  }

  parser->offset = latest_valid_offset;

  end = parser->offset;

  node_storeinfo(parser->src, node, begin, end);

  return node;
}

static gab_value parse_optional_expression_prec(struct gab_triple gab,
                                                struct parser *parser,
                                                enum prec_k prec) {
  if (!curr_prefix(parser)) {
    gab_value empty = node_empty(gab, parser);
    return empty;
  }

  return parse_expression(gab, parser, prec);
}

gab_value parse_exp_num(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  double num = strtod((char *)prev_src(parser).data, nullptr);
  return node_value(gab, gab_number(num));
}

gab_value parse_exp_msg(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  gab_value id = trimback_prev_id(gab, parser);

  return node_value(gab, gab_strtomsg(id));
}

gab_value parse_exp_sym(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  gab_value id = prev_id(gab, parser);

  return node_value(gab, gab_strtobin(id));
}

gab_value parse_exp_dstr(struct gab_triple gab, struct parser *parser,
                         gab_value lhs) {
  return node_value(gab, trim_prev_id(gab, parser));
}

gab_value parse_exp_sstr(struct gab_triple gab, struct parser *parser,
                         gab_value lhs) {
  a_char *parsed = parse_raw_str(parser, prev_src(parser));

  if (parsed == nullptr)
    return parser_error(gab, parser, GAB_MALFORMED_STRING,
                        FMT_GAB_MALFORMED_STRING),
           gab_cinvalid;

  gab_value str = gab_nstring(gab, parsed->len, parsed->data);

  a_char_destroy(parsed);

  return node_value(gab, str);
}

gab_value parse_exp_rec(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  size_t begin = parser->offset;

  gab_value result = parse_expressions_until(gab, parser, TOKEN_RBRACK);

  if (result == gab_cinvalid)
    return gab_cinvalid;

  gab_value lhs_node = node_value(gab, gab_message(gab, tGAB_RECORD));
  gab_value msg_node = gab_message(gab, mGAB_MAKE);

  gab_value node = node_send(gab, lhs_node, msg_node, result);

  size_t end = parser->offset;

  node_storeinfo(parser->src, result, begin, end);
  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, lhs_node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

gab_value parse_exp_lst(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  size_t begin = parser->offset;

  gab_value result = parse_expressions_until(gab, parser, TOKEN_RBRACE);

  if (result == gab_cinvalid)
    return gab_cinvalid;

  gab_value lhs_node = node_value(gab, gab_message(gab, tGAB_LIST));
  gab_value msg_node = gab_message(gab, mGAB_MAKE);

  gab_value node = node_send(gab, lhs_node, msg_node, result);

  size_t end = parser->offset;

  node_storeinfo(parser->src, result, begin, end);
  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, lhs_node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

gab_value parse_exp_shp(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  size_t begin = parser->offset;

  gab_value result = parse_expressions_until(gab, parser, TOKEN_RBRACK);

  if (result == gab_cinvalid)
    return gab_cinvalid;

  gab_value lhs_node = node_value(gab, gab_message(gab, tGAB_SHAPE));
  gab_value msg_node = gab_message(gab, mGAB_MAKE);

  gab_value node = node_send(gab, lhs_node, msg_node, result);

  size_t end = parser->offset;

  node_storeinfo(parser->src, result, begin, end);
  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, lhs_node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

gab_value parse_exp_tup(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  return parse_expressions_until(gab, parser, TOKEN_RPAREN);
}

gab_value parse_exp_blk(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  gab_value res = parse_expressions_body(gab, parser, TOKEN_END);

  if (res == gab_cinvalid)
    return gab_cinvalid;

  return res;
}

gab_value parse_exp_send(struct gab_triple gab, struct parser *parser,
                         gab_value lhs) {
  size_t begin = parser->offset;

  gab_value msg = trimfront_prev_id(gab, parser);

  gab_value rhs = parse_optional_expression_prec(gab, parser, kSEND + 1);

  if (rhs == gab_cinvalid)
    return gab_cinvalid;

  gab_value node = node_send(gab, lhs, gab_strtomsg(msg), rhs);

  size_t end = parser->offset;

  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

gab_value parse_exp_send_op(struct gab_triple gab, struct parser *parser,
                            gab_value lhs) {
  size_t begin = parser->offset;

  gab_value msg = prev_id(gab, parser);

  gab_value rhs = parse_optional_expression_prec(gab, parser, kBINARY_SEND + 1);

  if (rhs == gab_cinvalid)
    return gab_cinvalid;

  gab_value node = node_send(gab, lhs, gab_strtomsg(msg), rhs);

  size_t end = parser->offset;

  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

/*
 * TODO @feat: What would macros look like?
 *
 * I want to implement '=' and '=>' in userspace somehow if I can.
 *
 * I also want to get rid of OP_BLOCK for the purity of it all - but thats hard.
 * I suppose a macro would sort of solve it, as it could rewrite the () => do
 * ... end Into a Blocks.make(proto) call. This would involve first compiling
 * the rhs do with the lhs binding.
 *
 * The Blocks.make(proto) message would need safeguards somehow.
 * Is there a way it could check that its at the correct spot in the bytecode?
 *
 * Should we expand macros at this point?
 *
 * It would kind of make sense otherwise the only difference between a macro
 * and a send is the precedence.
 *
 * :> and := each affect *environments*. They're not very good candidates for
 * macros either. (For example, clojure's let and fn are special forms, not
 * macros or functions)
 *
 * Maybe this is a reason to have more of a :let kind of struscture?
 *
 * (a b) :fn  do
 * end
 *
 * (a 1 c 2) :in do
 * end
 *
 * This would allow macro impl to not include env.
 *
 * a := 2
 *
 * How does this make it any easier?
 *
 * The :fn or :> macro would be able to compile the rhs with the lhs bindings,
 * and then emit a Blocks.make(proto).
 *
 * But the := or :in *can't* be just macros, because they *compile* differently.
 * They have to emit a bunch of store-local instructions.
 *
 * This happens completely with the `unpack_binding_into_env` function.
 *
 * The notion of ENV is completely at *compile* time, not *parse* time. Macros
 * should be between *parse* and *compile* - so should not intersect.
 *
 * Lisps and Schemes have special forms `let` and `fn`, they do not handle this
 * in userspace. And I don't see a way to do so reasonably.
 *
 * This leaves the options of either creating special syntax for let and fn, or
 * using special sends (as is currently, with = and =>)
 *
 * a := 2
 *
 * (a, z*, b) :> do
 *    z*
 * end
 *
 * I think I am deciding against adding macros. I don't mind leaving some
 * infrastructure for them, but I think they are mostly a bad/unnecessary idea.
 *
 * I need to decide on a specific syntax for blocks though, I don't love :>
 *
 */
gab_value parse_exp_builtin(struct gab_triple gab, struct parser *parser,
                            gab_value lhs) {
  size_t begin = parser->offset;

  gab_value msg = prev_id(gab, parser);

  gab_value rhs = parse_expression(gab, parser, kEXP);

  if (rhs == gab_cinvalid)
    return gab_cinvalid;

  gab_value node = node_send(gab, lhs, gab_strtobin(msg), rhs);

  size_t end = parser->offset;

  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

const struct parse_rule parse_rules[] = {
    {parse_exp_blk, nullptr, kNONE},            // DO
    {nullptr, nullptr, kNONE},                  // END
    {nullptr, parse_exp_builtin, kBUILTIN},     // LAMBDA
    {nullptr, parse_exp_builtin, kBUILTIN},     // IN
    {parse_exp_lst, nullptr, kNONE},            // LBRACE
    {nullptr, nullptr, kNONE},                  // RBRACE
    {parse_exp_rec, nullptr, kNONE},            // LBRACK
    {nullptr, nullptr, kNONE},                  // RBRACK
    {parse_exp_tup, nullptr, kNONE},            // LPAREN
    {nullptr, nullptr, kNONE},                  // RPAREN
    {nullptr, parse_exp_send, kSEND},           // SEND
    {nullptr, parse_exp_send_op, kBINARY_SEND}, // OPERATOR
    {parse_exp_sym, nullptr, kNONE},            // SYMBOL
    {parse_exp_msg, nullptr, kNONE},            // MESSAGE
    {parse_exp_sstr, nullptr, kNONE},           // STRING
    {parse_exp_dstr, nullptr, kNONE},           // STRING
    {parse_exp_num, nullptr, kNONE},            // NUMBER
    {parse_exp_shp, nullptr, kNONE},            // SLASH
    {nullptr, nullptr, kNONE},                  // NEWLINE
    {nullptr, nullptr, kNONE},                  // EOF
    {nullptr, nullptr, kNONE},                  // ERROR
};

struct parse_rule get_parse_rule(gab_token k) { return parse_rules[k]; }

gab_value parse(struct gab_triple gab, struct parser *parser) {
  size_t begin = parser->offset;

  if (curr_tok(parser) == TOKEN_EOF)
    return eat_token(gab, parser),
           parser_error(gab, parser, GAB_UNEXPECTED_EOF, ""), gab_cinvalid;

  if (curr_tok(parser) == TOKEN_ERROR)
    return eat_token(gab, parser),
           parser_error(gab, parser, GAB_MALFORMED_TOKEN,
                        "This token is malformed or unrecognized."),
           gab_cinvalid;

  gab_value ast = parse_expressions_body(gab, parser, TOKEN_EOF);

  if (ast == gab_cinvalid)
    return gab_cinvalid;

  if (gab.flags & fGAB_AST_DUMP)
    gab_fprintf(stdout, "$\n", gab_pvalintos(gab, ast, ""));

  gab_iref(gab, ast);
  gab_egkeep(gab.eg, ast);

  size_t end = parser->offset;

  node_storeinfo(parser->src, ast, begin, end);

  return ast;
}

union gab_value_pair gab_parse(struct gab_triple gab,
                               struct gab_parse_argt args) {
  gab.flags |= args.flags;

  args.name = args.name ? args.name : "__main__";

  gab_gclock(gab);

  gab_value name = gab_string(gab, args.name);

  struct gab_src *src =
      gab_src(gab, name, (char *)args.source,
              args.source_len ? args.source_len : strlen(args.source) + 1);

  struct parser parser = {.src = src, .err = gab_cundefined};

  gab_value ast = parse(gab, &parser);

  gab_gcunlock(gab);

  assert(ast != gab_cinvalid || parser.err != gab_cundefined);

  if (ast == gab_cinvalid)
    return (union gab_value_pair){.status = gab_cinvalid,
                                  .vresult = parser.err};
  else
    return (union gab_value_pair){.status = gab_cvalid, .vresult = ast};
}

/*

 *************
 * COMPILING *
 *************

 Emiting bytecode for the gab AST.

[0] = LOAD_CONSTANT 0
[0, 1] = LOAD_NCONSTANT 0, 1

*/

void bc_destroy(struct bc *bc) {
  v_uint8_t_destroy(&bc->bc);
  v_uint64_t_destroy(&bc->bc_toks);
}

static int vbc_error(struct gab_triple gab, struct bc *bc, gab_value node,
                     enum gab_status e, const char *fmt, va_list args) {
  size_t tok = d_uint64_t_read(&bc->src->node_begin_toks, node);

  if (tok > 0)
    tok--;

  bc->err = gab_vspanicf(gab, args,
                         (struct gab_err_argt){
                             .src = bc->src,
                             .status = e,
                             .tok = tok,
                             .note_fmt = fmt,
                         });

  va_end(args);

  return 0;
}

static int bc_error(struct gab_triple gab, struct bc *bc, gab_value node,
                    enum gab_status e, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  return vbc_error(gab, bc, node, e, fmt, args);
}

static inline void push_op(struct bc *bc, uint8_t op, gab_value node) {
  bc->pprev_op = bc->prev_op;
  bc->prev_op = op;

  // assert(d_uint64_t_exists(&bc->src->node_begin_toks, node));

  bc->prev_op_at = v_uint8_t_push(&bc->bc, op);
  size_t offset = d_uint64_t_read(&bc->src->node_begin_toks, node);

  v_uint64_t_push(&bc->bc_toks, offset - (offset != 0));
}

static inline void push_byte(struct bc *bc, uint8_t data, gab_value node) {
  // assert(d_uint64_t_exists(&bc->src->node_begin_toks, node));

  v_uint8_t_push(&bc->bc, data);
  size_t offset = d_uint64_t_read(&bc->src->node_begin_toks, node);
  v_uint64_t_push(&bc->bc_toks, offset - (offset != 0));
}

static inline void push_short(struct bc *bc, uint16_t data, gab_value node) {
  push_byte(bc, (data >> 8) & 0xff, node);
  push_byte(bc, data & 0xff, node);
}

static inline uint16_t addk(struct gab_triple gab, struct bc *bc,
                            gab_value value) {
  gab_egkeep(gab.eg, gab_iref(gab, value));

  assert(bc->ks->len < UINT16_MAX);

  return v_gab_value_push(bc->ks, value);
}

/*
 * SUPER INSTRUCTION OPTIMIZATION
 */

enum super_instruction_transition_k : uint8_t {
  kSI_REPLACE,

  kSI_MAKE_MULTI,
  kSI_MULTI_APPEND,

  kSI_BYTE_ARG_MAKE_MULTI,
  kSI_MULTI_BYTE_ARG_APPEND,

  // Sometimes the second argument-byte contains
  // the multi-byte that needs to be incremented.
  kSI_BYTE_ARG_MAKE_MULTI2,
  kSI_MULTI2_BYTE_ARG_APPEND,

  kSI_SHORT_ARG_MAKE_MULTI,
  kSI_MULTI_SHORT_ARG_APPEND,

  // Sometimes the second argument-byte contains
  // the multi-byte that needs to be incremented.
  kSI_SHORT_ARG_MAKE_MULTI2,
  kSI_MULTI2_SHORT_ARG_APPEND,
};

struct super_instruction {
  uint8_t from, via, to, k;
};

struct super_instruction super_instructions[] = {
    {
        OP_LOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_NLOAD_LOCAL,
        kSI_BYTE_ARG_MAKE_MULTI,
    },
    {
        OP_NLOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_NLOAD_LOCAL,
        kSI_MULTI_BYTE_ARG_APPEND,
    },
    {
        OP_STORE_LOCAL,
        OP_POP,
        OP_POPSTORE_LOCAL,
        kSI_REPLACE,
    },
    {
        OP_POPSTORE_LOCAL,
        OP_STORE_LOCAL,
        OP_NPOPSTORE_STORE_LOCAL,
        kSI_BYTE_ARG_MAKE_MULTI,
    },
    {
        OP_NPOPSTORE_LOCAL,
        OP_STORE_LOCAL,
        OP_NPOPSTORE_STORE_LOCAL,
        kSI_MULTI_BYTE_ARG_APPEND,
    },
    {
        OP_NPOPSTORE_STORE_LOCAL,
        OP_POP,
        OP_NPOPSTORE_LOCAL,
        kSI_REPLACE,
    },
    {
        OP_LOAD_UPVALUE,
        OP_LOAD_UPVALUE,
        OP_NLOAD_UPVALUE,
        kSI_BYTE_ARG_MAKE_MULTI,
    },
    {
        OP_NLOAD_UPVALUE,
        OP_LOAD_UPVALUE,
        OP_NLOAD_UPVALUE,
        kSI_MULTI_BYTE_ARG_APPEND,
    },
    {
        OP_CONSTANT,
        OP_CONSTANT,
        OP_NCONSTANT,
        kSI_SHORT_ARG_MAKE_MULTI,
    },
    {
        OP_NCONSTANT,
        OP_CONSTANT,
        OP_NCONSTANT,
        kSI_MULTI_SHORT_ARG_APPEND,
    },
    {
        OP_TUPLE,
        OP_TUPLE,
        OP_NTUPLE,
        kSI_MAKE_MULTI,
    },
    {
        OP_NTUPLE,
        OP_TUPLE,
        OP_NTUPLE,
        kSI_MULTI_APPEND,
    },
    {
        OP_NTUPLE,
        OP_CONSTANT,
        OP_NTUPLE_CONSTANT,
        kSI_REPLACE,
    },
    {
        OP_NTUPLE_CONSTANT,
        OP_CONSTANT,
        OP_NTUPLE_NCONSTANT,
        kSI_SHORT_ARG_MAKE_MULTI2,
    },
    {
        OP_NTUPLE_NCONSTANT,
        OP_CONSTANT,
        OP_NTUPLE_NCONSTANT,
        kSI_MULTI2_SHORT_ARG_APPEND,
    },
    {
        OP_TUPLE,
        OP_CONSTANT,
        OP_TUPLE_CONSTANT,
        kSI_REPLACE,
    },
    {
        OP_TUPLE_CONSTANT,
        OP_CONSTANT,
        OP_TUPLE_NCONSTANT,
        kSI_SHORT_ARG_MAKE_MULTI,
    },
    {
        OP_TUPLE_NCONSTANT,
        OP_CONSTANT,
        OP_TUPLE_NCONSTANT,
        kSI_MULTI_SHORT_ARG_APPEND,
    },
    {
        OP_TUPLE,
        OP_LOAD_LOCAL,
        OP_TUPLE_LOAD_LOCAL,
        kSI_REPLACE,
    },
    {
        OP_TUPLE_LOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_TUPLE_NLOAD_LOCAL,
        kSI_BYTE_ARG_MAKE_MULTI,
    },
    {
        OP_TUPLE_NLOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_TUPLE_NLOAD_LOCAL,
        kSI_MULTI_BYTE_ARG_APPEND,
    },
    {
        OP_NTUPLE,
        OP_LOAD_LOCAL,
        OP_NTUPLE_LOAD_LOCAL,
        kSI_REPLACE,
    },
    {
        OP_NTUPLE_LOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_NTUPLE_NLOAD_LOCAL,
        kSI_BYTE_ARG_MAKE_MULTI2,
    },
    {
        OP_NTUPLE_NLOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_NTUPLE_NLOAD_LOCAL,
        kSI_MULTI2_BYTE_ARG_APPEND,
    },
};

const int nsuper_instructions = LEN_CARRAY(super_instructions);

struct inst_arg {
  uint8_t op;

  enum : uint8_t {
    kINST_ARG_NONE,
    kINST_ARG_BYTE,
    kINST_ARG_SHORT,
  } k;

  union {
    uint8_t byte_arg;
    uint16_t short_arg;
  } as;
};

static inline void byte_arg_make_multi(struct bc *bc, struct inst_arg arg,
                                       struct super_instruction si,
                                       gab_value node, int multiarg_offset) {
  // Transition a single-byte-arg instruction to a multi-byte-arg
  // instruction.
  size_t multi_arg = bc->prev_op_at + multiarg_offset;
  uint8_t prev_multi = v_uint8_t_val_at(&bc->bc, multi_arg);

  // Change the previous byte arg to now correspond to the number of bytes
  // to follow (2).
  v_uint8_t_set(&bc->bc, multi_arg, 2);

  // Push the previous byte value, and the new one.
  push_byte(bc, prev_multi, node);
  push_byte(bc, arg.as.byte_arg, node);

  // Update the old instruction to the new, and the previous op.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

static inline void multi_byte_arg_append(struct bc *bc, struct inst_arg arg,
                                         struct super_instruction si,
                                         gab_value node, int multiarg_offset) {
  // Append a new byte arg to a multi-byte instruction.
  size_t multi_arg = bc->prev_op_at + multiarg_offset;
  uint8_t multi = v_uint8_t_val_at(&bc->bc, multi_arg);

  // Increment the multi-arg count.
  v_uint8_t_set(&bc->bc, multi_arg, multi + 1);

  // Push the additional byte argument.
  push_byte(bc, arg.as.byte_arg, node);

  if (si.from == si.to)
    return;

  // Update the old instruction to the new, and the previous op.
  // We can skip this if the super instruction's from and to ops are the
  // same.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

static inline void short_arg_make_multi(struct bc *bc, struct inst_arg arg,
                                        struct super_instruction si,
                                        gab_value node, int multiarg_offset) {
  size_t multi_arg = bc->prev_op_at + multiarg_offset;

  // Reconstruct the short argument from the bytecode.
  uint8_t prev_arg_a = v_uint8_t_val_at(&bc->bc, multi_arg);
  uint8_t prev_arg_b = v_uint8_t_val_at(&bc->bc, multi_arg + 1);

  uint16_t prev_arg = prev_arg_a << 8 | prev_arg_b;

  // Pop off the old short argument, it isn't salvageable.
  v_uint8_t_pop(&bc->bc);
  v_uint8_t_pop(&bc->bc);
  v_uint64_t_pop(&bc->bc_toks);
  v_uint64_t_pop(&bc->bc_toks);

  // Push on a new count argument.
  push_byte(bc, 2, node);

  // Push the original short argument, and the new second one.
  push_short(bc, prev_arg, node);
  push_short(bc, arg.as.short_arg, node);

  // Update the old instruction to the new, and the previous op.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

static inline void multi_short_arg_append(struct bc *bc, struct inst_arg arg,
                                          struct super_instruction si,
                                          gab_value node, int multiarg_offset) {
  // Append a new short arg to a multi-byte instruction.
  size_t multi_arg = bc->prev_op_at + multiarg_offset;
  uint8_t multi = v_uint8_t_val_at(&bc->bc, multi_arg);

  // Increment the multi-arg count.
  v_uint8_t_set(&bc->bc, multi_arg, multi + 1);

  // Push the additional short.
  push_short(bc, arg.as.short_arg, node);

  if (si.from == si.to)
    return;

  // Update the old instruction to the new, and the previous op.
  // We can skip this if the super instruction's from and to ops are the
  // same.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

static inline void push_inst(struct bc *bc, struct inst_arg arg,
                             gab_value node) {
#if cGAB_SUPERINSTRUCTIONS
  for (int i = 0; i < nsuper_instructions; i++) {
    struct super_instruction si = super_instructions[i];

    if (si.from == bc->prev_op && si.via == arg.op) {

      switch (si.k) {
      default:
        assert(false);
        break;
      case kSI_REPLACE: {
        // Push the arg for this new instruction
        switch (arg.k) {
        case kINST_ARG_NONE:
          break;
        case kINST_ARG_BYTE:
          push_byte(bc, arg.as.byte_arg, node);
          break;
        case kINST_ARG_SHORT:
          push_short(bc, arg.as.short_arg, node);
          break;
        }

        // Update the old instruction to the new, and the previous op.
        v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
        bc->prev_op = si.to;
        break;
      }
      case kSI_MAKE_MULTI: {
        // Push a 2, to include previous op and this repetition.
        push_byte(bc, 2, node);

        // Update the instruction to the repeatable target.
        v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
        bc->prev_op = si.to;

        break;
      }
      case kSI_MULTI_APPEND: {
        size_t multi_arg = bc->prev_op_at + 1;
        uint8_t multi = v_uint8_t_val_at(&bc->bc, multi_arg);

        // Increment the multi-arg count.
        v_uint8_t_set(&bc->bc, multi_arg, multi + 1);

        // Leave the instruction, as we are just adding a repetition
        break;
      }
      case kSI_BYTE_ARG_MAKE_MULTI:
        byte_arg_make_multi(bc, arg, si, node, 1);
        break;
      case kSI_BYTE_ARG_MAKE_MULTI2:
        byte_arg_make_multi(bc, arg, si, node, 2);
        break;
      case kSI_MULTI_BYTE_ARG_APPEND:
        multi_byte_arg_append(bc, arg, si, node, 1);
        break;
      case kSI_MULTI2_BYTE_ARG_APPEND:
        multi_byte_arg_append(bc, arg, si, node, 2);
        break;
      case kSI_SHORT_ARG_MAKE_MULTI:
        short_arg_make_multi(bc, arg, si, node, 1);
        break;
      case kSI_SHORT_ARG_MAKE_MULTI2:
        short_arg_make_multi(bc, arg, si, node, 2);
        break;
      case kSI_MULTI_SHORT_ARG_APPEND:
        multi_short_arg_append(bc, arg, si, node, 1);
        break;
      case kSI_MULTI2_SHORT_ARG_APPEND:
        multi_short_arg_append(bc, arg, si, node, 2);
        break;
      };
      return;
    }
  }
#endif

  push_op(bc, arg.op, node);

  switch (arg.k) {
  case kINST_ARG_NONE:
    break;
  case kINST_ARG_BYTE:
    push_byte(bc, arg.as.byte_arg, node);
    break;
  case kINST_ARG_SHORT:
    push_short(bc, arg.as.short_arg, node);
    break;
  }
};

static inline void push_k(struct bc *bc, uint16_t k, gab_value node) {
  push_inst(bc,
            (struct inst_arg){
                .op = OP_CONSTANT,
                .k = kINST_ARG_SHORT,
                .as.short_arg = k,
            },
            node);
}

static inline void push_loadi(struct bc *bc, gab_value i, gab_value node) {
  assert(i == gab_cinvalid || i == gab_true || i == gab_false || i == gab_nil);

  switch (i) {
  case gab_nil:
    push_k(bc, 0, node);
    break;
  case gab_false:
    push_k(bc, 1, node);
    break;
  case gab_true:
    push_k(bc, 2, node);
    break;
  case gab_ok:
    push_k(bc, 3, node);
    break;
  case gab_err:
    push_k(bc, 4, node);
    break;
  case gab_none:
    push_k(bc, 5, node);
    break;
  default:
    assert(false && "Invalid constant");
  }
};

static inline void push_loadni(struct bc *bc, gab_value v, int n,
                               gab_value node) {
  for (int i = 0; i < n; i++)
    push_loadi(bc, v, node);
}

static inline void push_loadk(struct gab_triple gab, struct bc *bc, gab_value k,
                              gab_value node) {
  push_k(bc, addk(gab, bc, k), node);
}

static inline void push_loadl(struct bc *bc, uint8_t local, gab_value node) {
  push_inst(bc,
            (struct inst_arg){
                .k = kINST_ARG_BYTE,
                .op = OP_LOAD_LOCAL,
                .as.byte_arg = local,
            },
            node);
  return;
}

static inline void push_storel(struct bc *bc, uint8_t local, gab_value node) {
  push_inst(bc,
            (struct inst_arg){
                .k = kINST_ARG_BYTE,
                .op = OP_STORE_LOCAL,
                .as.byte_arg = local,
            },
            node);
}

static inline void push_loadu(struct bc *bc, uint8_t upv, gab_value node) {
  push_inst(bc,
            (struct inst_arg){
                .k = kINST_ARG_BYTE,
                .op = OP_LOAD_UPVALUE,
                .as.byte_arg = upv,
            },
            node);
}

static inline void push_send(struct gab_triple gab, struct bc *bc, gab_value m,
                             gab_value node) {
  if (gab_valkind(m) == kGAB_STRING)
    m = gab_strtomsg(m);

  assert(gab_valkind(m) == kGAB_MESSAGE);

  uint16_t ks = addk(gab, bc, m);
  addk(gab, bc, gab_cinvalid);

  for (int i = 0; i < cGAB_SEND_CACHE_LEN * GAB_SEND_CACHE_SIZE; i++)
    addk(gab, bc, gab_cinvalid);

  push_op(bc, OP_SEND, node);
  push_short(bc, ks, node);
}

static inline void push_pop(struct bc *bc, uint8_t n, gab_value node) {
  if (n > 1) {
    push_op(bc, OP_POP_N, node);
    push_byte(bc, n, node);
    return;
  }

  push_inst(bc,
            (struct inst_arg){
                .k = kINST_ARG_NONE,
                .op = OP_POP,
            },
            node);
}

// fix this to work with sends in the middle of tuples, doesn't trim properly
// now
static inline bool push_trim_node(struct gab_triple gab, struct bc *bc,
                                  uint8_t want, gab_value values,
                                  gab_value node) {
  if (bc->prev_op == OP_TRIM) {
    v_uint8_t_set(&bc->bc, bc->prev_op_at + 1, want);
    return true;
  }

  if (values == gab_cinvalid) {
    push_op(bc, OP_TRIM, node);
    push_byte(bc, want, node);
    return true;
  }

  size_t len = node_len(gab, values);

  if (node_ismulti(gab, values)) {
    if (want == 0) {
      push_op(bc, OP_TRIM, node);
      push_byte(bc, 0, node);
      return true;
    }

    push_op(bc, OP_TRIM, node);
    push_byte(bc, want, node);
    return true;
  }

  if (len > want) {
    push_pop(bc, len - want, values);
    return true;
  }

  if (len < want) {
    push_loadni(bc, gab_nil, want - len, values);
    return true;
  }

  // Nothing needs to be done
  return true;
}

static inline void push_listpack(struct gab_triple gab, struct bc *bc,
                                 uint8_t below, uint8_t above, gab_value node) {
  push_op(bc, OP_PACK_LIST, node);
  push_byte(bc, below, node);
  push_byte(bc, above, node);
}

static inline void push_dictpack(struct gab_triple gab, struct bc *bc,
                                 uint8_t below, uint8_t above, gab_value node) {
  push_op(bc, OP_PACK_DICT, node);
  push_byte(bc, below, node);
  push_byte(bc, above, node);
}

static inline void push_ret(struct gab_triple gab, struct bc *bc, gab_value tup,
                            gab_value node) {
  assert(node_len(gab, tup) < 16);

  bool is_multi = node_ismulti(gab, tup);
  size_t len = node_len(gab, tup);

  if (len && is_multi)
    len--;

#if cGAB_TAILCALL
  if (len == 0) {
    switch (bc->prev_op) {
    case OP_SEND: {
      uint8_t first_short_byte = v_uint8_t_val_at(&bc->bc, bc->bc.len - 2);
      assert(!(first_short_byte & fHAVE_TAIL));
      v_uint8_t_set(&bc->bc, bc->bc.len - 2, first_short_byte | fHAVE_TAIL);
      push_op(bc, OP_RETURN, node);

      return;
    }
    case OP_TRIM: {
      if (bc->pprev_op != OP_SEND)
        break;

      uint8_t first_short_byte = v_uint8_t_val_at(&bc->bc, bc->bc.len - 4);
      assert(!(first_short_byte & fHAVE_TAIL));
      v_uint8_t_set(&bc->bc, bc->bc.len - 4, first_short_byte | fHAVE_TAIL);
      bc->prev_op = bc->pprev_op;
      bc->bc.len -= 2;
      bc->bc_toks.len -= 2;
      push_op(bc, OP_RETURN, node);

      return;
    }
    }
  }
#endif

  push_op(bc, OP_RETURN, node);
  return;
}

void patch_init(struct bc *bc, uint8_t nlocals) {
  if (v_uint8_t_val_at(&bc->bc, 0) == OP_TRIM)
    v_uint8_t_set(&bc->bc, 1, nlocals);
  else if (v_uint8_t_val_at(&bc->bc, 3) == OP_TRIM)
    v_uint8_t_set(&bc->bc, 4, nlocals);
  else
    assert(false && "UNREACHABLE");
}

size_t locals_in_env(gab_value env) {
  size_t n = 0, len = gab_reclen(env);

  for (size_t i = 0; i < len; i++) {
    gab_value num_or_nil = gab_uvrecat(env, i);

    if (num_or_nil == gab_nil)
      n++;
  }

  return n;
}

size_t upvalues_in_env(gab_value env) {
  size_t n = 0, len = gab_reclen(env);

  for (size_t i = 0; i < len; i++) {
    gab_value num_or_nil = gab_uvrecat(env, i);

    if (gab_valkind(num_or_nil) == kGAB_NUMBER)
      n++;
  }

  return n;
}

gab_value peek_env(gab_value env, int depth) {
  size_t nenv = gab_reclen(env);

  if (depth + 1 > nenv)
    return gab_cundefined;

  return gab_uvrecat(env, nenv - depth - 1);
}

gab_value put_env(struct gab_triple gab, gab_value env, int depth,
                  gab_value new_ctx) {
  size_t nenv = gab_reclen(env);

  assert(depth + 1 <= nenv);
  return gab_urecput(gab, env, nenv - depth - 1, new_ctx);
}

struct lookup_res {
  gab_value env;

  enum {
    kLOOKUP_NONE,
    kLOOKUP_UPV,
    kLOOKUP_LOC,
  } k;

  int32_t idx;
};

/*
 * Modify the given environment to capture the message 'id'.
 * If already captured, the environment is unchanged.
 */
static struct lookup_res add_upvalue(struct gab_triple gab, gab_value env,
                                     gab_value id, int depth) {
  gab_value ctx = peek_env(env, depth);

  if (ctx == gab_cundefined)
    return (struct lookup_res){env, kLOOKUP_NONE};

  // Don't pull redundant upvalues
  gab_value current_upv_idx = gab_recat(ctx, id);

  if (current_upv_idx != gab_cundefined)
    return (struct lookup_res){env, kLOOKUP_UPV, gab_valtoi(current_upv_idx)};

  uint16_t count = upvalues_in_env(ctx);

  // Throw some sort of error here
  if (count == GAB_UPVALUE_MAX)
    return (struct lookup_res){env, kLOOKUP_NONE};

  /*compiler_error(*/
  /*    bc, GAB_TOO_MANY_UPVALUES,*/
  /*    "For arbitrary reasons, blocks cannot capture more than 255 "*/
  /*    "variables.");*/
  assert(!gab_rechas(ctx, id));
  ctx = gab_recput(gab, ctx, id, gab_number(count));
  env = put_env(gab, env, depth, ctx);

  return (struct lookup_res){env, kLOOKUP_UPV, count};
}

static int64_t lookup_upv(gab_value ctx, gab_value id) {
  assert(gab_valkind(gab_recat(ctx, id)) == kGAB_NUMBER);
  return gab_valtoi(gab_recat(ctx, id));
}

static int lookup_local(gab_value ctx, gab_value id) {
  size_t idx = 0, len = gab_reclen(ctx);

  for (size_t i = 0; i < len; i++) {
    gab_value k = gab_ukrecat(ctx, i);
    gab_value v = gab_uvrecat(ctx, i);

    // If v isn't nil, then this is an upvalue. Skip it.
    if (v != gab_nil)
      continue;

    if (k == id)
      return idx;

    idx++;
  }

  return -1;
}

/*
 * Find for an id in the env at depth.
 */
static int resolve_local(struct gab_triple gab, gab_value env, gab_value id,
                         uint8_t depth) {
  gab_value ctx = peek_env(env, depth);

  if (ctx == gab_cundefined)
    return -1;

  return lookup_local(ctx, id);
}

static struct lookup_res resolve_upvalue(struct gab_triple gab, gab_value env,
                                         gab_value name, uint8_t depth) {
  size_t nenvs = gab_reclen(env);

  if (depth >= nenvs)
    return (struct lookup_res){env, kLOOKUP_NONE};

  int local = resolve_local(gab, env, name, depth + 1);

  if (local >= 0)
    return add_upvalue(gab, env, name, depth);

  struct lookup_res res = resolve_upvalue(gab, env, name, depth + 1);

  if (res.k) // This means we found either a local, or an upvalue
    return add_upvalue(gab, res.env, name, depth);

  return (struct lookup_res){env, kLOOKUP_NONE};
}

/* Returns COMP_ERR if an error is encountered,
 * COMP_ID_NOT_FOUND if no matching local or upvalue is found,
 * COMP_RESOLVED_TO_LOCAL if the id was a local, and
 * COMP_RESOLVED_TO_UPVALUE if the id was an upvalue.
 */
static struct lookup_res resolve_id(struct gab_triple gab, struct bc *bc,
                                    gab_value env, gab_value id) {
  int idx = resolve_local(gab, env, id, 0);

  if (idx == -1)
    return resolve_upvalue(gab, env, id, 0);
  else
    return (struct lookup_res){env, kLOOKUP_LOC, idx};
}

gab_value compile_symbol(struct gab_triple gab, struct bc *bc, gab_value tuple,
                         gab_value id, gab_value env) {
  struct lookup_res res = resolve_id(gab, bc, env, id);

  switch (res.k) {
  case kLOOKUP_LOC:
    push_loadl(bc, res.idx, tuple);
    return res.env;
  case kLOOKUP_UPV:
    push_loadu(bc, res.idx, tuple);
    return res.env;
  default:
    bc_error(gab, bc, tuple, GAB_UNBOUND_SYMBOL, FMT_ID_NOT_FOUND,
             gab_bintostr(id));
    return gab_cinvalid;
  }
};

union gab_value_pair expand_value(struct gab_triple gab, gab_value tuple,
                                  size_t n, gab_value env);

union gab_value_pair expand_tuple(struct gab_triple gab, gab_value node,
                                  gab_value env) {
  // Map the tuple, expanding each element.
  size_t len = gab_reclen(node);

  gab_value tuple = gab_erecord(gab);

  for (size_t i = 0; i < len; i++) {
    union gab_value_pair res = expand_value(gab, node, i, env);

    if (res.status == gab_cinvalid)
      return res;

    env = res.data[0];
    tuple = gab_lstpush(gab, tuple, res.data[1]);
  }

  return (union gab_value_pair){{env, tuple}};
};

union gab_value_pair expand_record(struct gab_triple gab, gab_value tuple,
                                   gab_value node, gab_value env) {
  switch (gab_valkind(gab_recshp(node))) {
  case kGAB_SHAPE: {
    // We have a send node!
    // We can actually try to expand a macro.
    gab_value lhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_LHS);
    gab_value rhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_RHS);
    gab_value msg = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG);

    gab_value args[] = {rhs_node, env};

    // TODO @bug: The impl can change between here and the actual send.
    if (gab_impl(gab, msg, lhs_node).status != kGAB_IMPL_MACRO)
      return (union gab_value_pair){{env, node}};

    // TODO @bug: Non-macro sends still expand here.
    // Send the message to the receiver.
    // OOF I just noticed a bug.
    // What if the lhs_node type implements `msg`, but with
    // a runtime component and not a macro?
    // Maybe we should check for a macro impl before trying this?
    // TODO @bug: Possible GC while locked.
    union gab_value_pair res = gab_send(gab, (struct gab_send_argt){
                                                 .receiver = lhs_node,
                                                 .argv = args,
                                                 .len = LEN_CARRAY(args),
                                                 .message = msg,
                                             });

    // TODO @bug: Report macro execution error
    if (res.status != gab_cvalid)
      return (union gab_value_pair){{gab_cinvalid}};

    for (int i = 0; i < res.aresult->len; i++)
      gab_fprintf(stderr, "$ $\n", gab_number(i), res.aresult->data[i]);

    gab_assert(res.aresult->len >= 3,
               "Macro should return a new node and environment, not %u.",
               res.aresult->len);
    if (res.aresult->len < 3)
      return (union gab_value_pair){{gab_cinvalid}};

    // TODO @bug: Report macro error
    if (res.aresult->data[0] != gab_ok)
      return (union gab_value_pair){{gab_cinvalid}};

    node = res.aresult->data[1];
    env = res.aresult->data[2];

    return (union gab_value_pair){{env, node}};
  }
  case kGAB_SHAPELIST:
    return expand_tuple(gab, node, env);
  default:
    assert(false && "INVALID SHAPE KIND");
    return (union gab_value_pair){{env, node}};
  }
}

union gab_value_pair expand_value(struct gab_triple gab, gab_value tuple,
                                  size_t n, gab_value env) {
  gab_value node = gab_uvrecat(tuple, n);

  switch (gab_valkind(node)) {
    // do no macro expanding
  case kGAB_NUMBER:
  case kGAB_STRING:
  case kGAB_MESSAGE:
  case kGAB_BINARY:
    return (union gab_value_pair){{env, node}};

    // may macro expand
  case kGAB_RECORD:
    return expand_record(gab, tuple, node, env);

  default:
    assert(false && "UN-UNQUOATABLE VALUE");
    return (union gab_value_pair){{gab_cinvalid}};
  }
}

gab_value compile_tuple(struct gab_triple gab, struct bc *bc, gab_value node,
                        gab_value env);

gab_value compile_record(struct gab_triple gab, struct bc *bc, gab_value tuple,
                         gab_value node, gab_value env);

gab_value compile_value(struct gab_triple gab, struct bc *bc, gab_value tuple,
                        size_t n, gab_value env) {
  gab_value node = gab_uvrecat(tuple, n);

  switch (gab_valkind(node)) {
  case kGAB_NUMBER:
  case kGAB_STRING:
  case kGAB_MESSAGE:
    push_loadk(gab, bc, node, tuple);
    return env;

  case kGAB_BINARY:
    return compile_symbol(gab, bc, tuple, node, env);

  case kGAB_RECORD:
    return compile_record(gab, bc, tuple, node, env);

  default:
    assert(false && "UN-UNQUOATABLE VALUE");
    return gab_cinvalid;
  }
}

// TODO @feat: Destructuring
// It would be quite useful to automagically do some de-structuring here.
gab_value unpack_binding(struct gab_triple gab, struct bc *bc,
                         gab_value bindings, size_t i, gab_value ctx,
                         v_gab_value *targets, int *listpack_at_n,
                         int *recpack_at_n) {
  gab_value binding = gab_uvrecat(bindings, i);

  switch (gab_valkind(binding)) {

  case kGAB_BINARY:
    if (gab_valkind(gab_recat(ctx, binding)) == kGAB_NUMBER) {
      return bc_error(gab, bc, bindings, GAB_MALFORMED_ASSIGNMENT,
                      FMT_MALFORMED_ASSIGNMENT FMT_MALFORMED_ASSIGNMENT_NOTE
                      "Cannot assign to a captured variable: $.",
                      gab_bintostr(binding)),
             gab_cinvalid;
    }

    ctx = gab_recput(gab, ctx, binding, gab_nil);
    v_gab_value_push(targets, binding);
    return ctx;

  case kGAB_RECORD: {
    if (gab_valkind(gab_recshp(binding)) == kGAB_SHAPE) {
      // Assume this is a send
      gab_value lhs = gab_mrecat(gab, binding, mGAB_AST_NODE_SEND_LHS);
      gab_value rhs = gab_mrecat(gab, binding, mGAB_AST_NODE_SEND_RHS);
      gab_value m = gab_mrecat(gab, binding, mGAB_AST_NODE_SEND_MSG);

      gab_value rec = gab_uvrecat(lhs, 0);

      /*
       * Compiling a PACK member.
       */
      if (m == gab_message(gab, mGAB_SPLATLIST)) {
        if (gab_valkind(rec) != kGAB_BINARY)
          goto err;

        if (!node_isempty(rhs))
          goto err;

        if (*listpack_at_n >= 0 || *recpack_at_n >= 0)
          return bc_error(gab, bc, binding, GAB_MALFORMED_ASSIGNMENT,
                          FMT_MALFORMED_ASSIGNMENT FMT_MALFORMED_ASSIGNMENT_NOTE
                          "There may only be one assignment target with '*' or "
                          "'**'"),
                 gab_cinvalid;

        assert(gab_valkind(gab_recat(ctx, rec)) != kGAB_NUMBER);
        ctx = gab_recput(gab, ctx, rec, gab_nil);
        v_gab_value_push(targets, rec);

        *listpack_at_n = i;

        return ctx;
      }

      /*
       * Compiling a DICT member
       */
      if (m == gab_message(gab, mGAB_SPLATDICT)) {
        if (gab_valkind(rec) != kGAB_BINARY)
          goto err;

        if (!node_isempty(rhs))
          goto err;

        if (*listpack_at_n >= 0 || *recpack_at_n >= 0)
          return bc_error(gab, bc, binding, GAB_MALFORMED_ASSIGNMENT,
                          FMT_MALFORMED_ASSIGNMENT FMT_MALFORMED_ASSIGNMENT_NOTE
                          "There may only be one assignment target with '*' or "
                          "'**'"),
                 gab_cinvalid;

        assert(gab_valkind(gab_recat(ctx, rec)) != kGAB_NUMBER);
        ctx = gab_recput(gab, ctx, rec, gab_nil);
        v_gab_value_push(targets, rec);

        *recpack_at_n = i;

        return ctx;
      }

    err:
      return bc_error(gab, bc, binding, GAB_MALFORMED_ASSIGNMENT,
                      FMT_MALFORMED_ASSIGNMENT),
             gab_cinvalid;
    }
  }

  default:
    return bc_error(gab, bc, binding, GAB_MALFORMED_ASSIGNMENT,
                    FMT_MALFORMED_ASSIGNMENT),
           gab_cinvalid;
  }
}

gab_value unpack_bindings_into_env(struct gab_triple gab, struct bc *bc,
                                   gab_value bindings, gab_value env,
                                   gab_value values) {
  size_t local_ctx = gab_reclen(env) - 1;
  gab_value ctx = gab_uvrecat(env, local_ctx);

  int listpack_at_n = -1, recpack_at_n = -1;

  size_t len = gab_reclen(bindings);

  if (!len)
    return env;

  v_gab_value targets = {};

  for (size_t i = 0; i < len; i++) {
    ctx = unpack_binding(gab, bc, bindings, i, ctx, &targets, &listpack_at_n,
                         &recpack_at_n);

    if (ctx == gab_cinvalid)
      return v_gab_value_destroy(&targets), ctx;
  }

  size_t actual_targets = targets.len;

  if (listpack_at_n >= 0) {
    push_listpack(gab, bc, listpack_at_n, actual_targets - listpack_at_n - 1,
                  bindings);
  } else if (recpack_at_n >= 0) {
    push_dictpack(gab, bc, recpack_at_n, actual_targets - recpack_at_n - 1,
                  bindings);
  } else if (!push_trim_node(gab, bc, actual_targets, values, bindings)) {
    return v_gab_value_destroy(&targets), gab_cinvalid;
  }

  env = gab_urecput(gab, env, local_ctx, ctx);

  /*
   * Sometimes, these bindings don't have a corresponding value AST
   * to take care of right now (Such as arguments to a function).
   * In thase case, binding stops here.
   */
  if (values == gab_cinvalid)
    return v_gab_value_destroy(&targets), env;

  for (size_t i = 0; i < actual_targets; i++) {
    gab_value target = targets.data[actual_targets - i - 1];

    switch (gab_valkind(target)) {
    case kGAB_BINARY: {
      struct lookup_res res = resolve_id(gab, bc, env, target);

      switch (res.k) {
      case kLOOKUP_LOC:
        push_storel(bc, res.idx, bindings);
        if (i + 1 != actual_targets)
          push_pop(bc, 1, bindings);
        break;
      case kLOOKUP_UPV:
        assert(false && "INVALID UPV TARGET");
      case kLOOKUP_NONE:
        assert(false && "INVALID NONE TARGET");
        break;
      }

      break;
    }
    default:
      return v_gab_value_destroy(&targets),
             bc_error(gab, bc, bindings, GAB_MALFORMED_ASSIGNMENT,
                      FMT_MALFORMED_ASSIGNMENT),
             gab_cinvalid;
    }
  }

  return v_gab_value_destroy(&targets), env;
}

gab_value compile_lambda(struct gab_triple gab, struct bc *bc, gab_value node,
                         gab_value env) {
  gab_value LHS = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_LHS);
  gab_value RHS = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_RHS);

  gab_value lst = gab_listof(gab, gab_binary(gab, "self"));

  env = gab_lstpush(gab, env, gab_erecord(gab));

  gab_value bindings = gab_lstcat(gab, lst, LHS);
  node_stealinfo(bc->src, LHS, bindings);

  union gab_value_pair pair = gab_compile(gab, (struct gab_compile_argt){
                                                   .ast = RHS,
                                                   .env = env,
                                                   .bindings = bindings,
                                                   .mod = bc->src->name,
                                               });

  if (pair.status == gab_cinvalid)
    return bc->err = pair.vresult, gab_cinvalid;

  gab_value prt = pair.vresult;
  assert(gab_valkind(prt) == kGAB_PROTOTYPE);

  env = gab_recpop(gab, gab_prtenv(prt), nullptr, nullptr);

  push_op(bc, OP_BLOCK, RHS);
  push_short(bc, addk(gab, bc, prt), RHS);

  return env;
}

gab_value compile_assign(struct gab_triple gab, struct bc *bc, gab_value node,
                     gab_value env) {
  gab_value lhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_LHS);
  gab_value rhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_RHS);

  env = compile_tuple(gab, bc, rhs_node, env);

  if (env == gab_cinvalid)
    return gab_cinvalid;

  env = unpack_bindings_into_env(gab, bc, lhs_node, env, rhs_node);

  if (env == gab_cinvalid)
    return gab_cinvalid;

  return env;
}

gab_value compile_specialform(struct gab_triple gab, struct bc *bc,
                              gab_value tuple, gab_value node, gab_value env) {
  gab_value msg = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG);

  if (msg == gab_binary(gab, mGAB_ASSIGN))
    return compile_assign(gab, bc, node, env);

  if (msg == gab_binary(gab, mGAB_BLOCK))
    return compile_lambda(gab, bc, node, env);

  assert(false && "UNHANDLED SPECIAL FORM");
  return gab_cinvalid;
};

gab_value compile_record(struct gab_triple gab, struct bc *bc, gab_value tuple,
                         gab_value node, gab_value env) {
  // Unquoting a record can mean one of two things:
  //  - This is a block, and each of the membres need to be compiled and
  //  trimmed, except for the last.
  //  - This is a send, and the send needs to be emitted.
  switch (gab_valkind(gab_recshp(node))) {
  case kGAB_SHAPE: {
    // We have a send node!
    gab_value lhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_LHS);
    gab_value rhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_RHS);
    gab_value msg = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG);

    if (msg_is_builtin(gab, msg))
      return compile_specialform(gab, bc, tuple, node, env);

    push_inst(bc, (struct inst_arg){OP_TUPLE}, node);

    env = compile_tuple(gab, bc, lhs_node, env);

    if (env == gab_cinvalid)
      return gab_cinvalid;

    env = compile_tuple(gab, bc, rhs_node, env);

    if (env == gab_cinvalid)
      return gab_cinvalid;

    push_send(gab, bc, msg, node);

    break;
  }
  case kGAB_SHAPELIST: {
    size_t len = gab_reclen(node);
    size_t last_node = len - 1;

    for (size_t i = 0; i < len; i++) {
      gab_value child_node = gab_uvrecat(node, i);

      env = compile_tuple(gab, bc, child_node, env);

      if (env == gab_cinvalid)
        return gab_cinvalid;

      if (i != last_node)
        if (!push_trim_node(gab, bc, 0, child_node, child_node))
          return gab_cinvalid;
    }
    break;
  }
  default:
    assert(false && "INVALID SHAPE KIND");
  }

  return env;
}

// Explicit tuple passed in here? Because of sends, where the lhs and rhs are
// each compiled as tuples, but really they are part of one tuple (which may or
// may not need to be cons'd)
gab_value compile_tuple(struct gab_triple gab, struct bc *bc, gab_value node,
                        gab_value env) {
  size_t len = gab_reclen(node);

  for (size_t i = 0; i < len; i++) {
    env = compile_value(gab, bc, node, i, env);

    if (env == gab_cinvalid)
      return gab_cinvalid;
  }

  return env;
}

void build_upvdata(gab_value env, uint8_t len, char *data) {
  if (len == 0)
    return;

  /*
   * Iterate through the env, and build out the data argument expected
   * by prototypes.
   *
   * I need to iterate through each environment in the stack
   *
   * If we're local, flag the captures as local.
   * Otherwise, do nothing.
   *
   * Upvalues are all the non-nil values in the context.
   *
   * As insertion-order is preserved, we can iterate the
   * ctx 0..len and be fine.
   *
   */
  size_t nenvs = gab_reclen(env);

  assert(nenvs >= 2);

  gab_value ctx = gab_uvrecat(env, nenvs - 1);
  gab_value parent = gab_uvrecat(env, nenvs - 2);

  bool has_grandparent = nenvs >= 3;

  size_t nbindings = gab_reclen(ctx);

  size_t nlocals = locals_in_env(parent);
  size_t nupvalues = upvalues_in_env(parent);

  for (size_t i = 0; i < nbindings; i++) {
    gab_value k = gab_ukrecat(ctx, i);
    gab_value v = gab_uvrecat(ctx, i);

    if (v == gab_nil)
      continue;

    bool is_local = has_grandparent ? (gab_recat(parent, k) == gab_nil) : true;

    size_t idx = is_local ? lookup_local(parent, k) : lookup_upv(parent, k);

    assert((is_local && idx < nlocals) || (!is_local && idx < nupvalues));

    uint64_t nth_upvalue = gab_valtou(v);
    assert(nth_upvalue < len);

    data[nth_upvalue] = (idx << 1) | is_local;
  }
}

/*
 *    *******
 *    * ENV *
 *    *******
 *
 *    The ENV argument is a stack of records.
 *
 *    [ { self {}, a {} }, { self {}, b {} } ]
 *
 *    Each variable has a record of attributes:
 *      * captured? -> number - is the variable captured by a child scope? If
 * so, whats its upv index?
 *
 *    Each scope needs to keep track of:
 *     - local variables introduced in this scope
 *     - local variables captured by child scopes
 *     - update parent scopes whose variables it captures
 */
union gab_value_pair gab_compile(struct gab_triple gab,
                                 struct gab_compile_argt args) {
  assert(gab_valkind(args.ast) == kGAB_RECORD);
  assert(gab_valkind(args.env) == kGAB_RECORD);
  gab.flags |= args.flags;

  struct gab_src *src = d_gab_src_read(&gab.eg->sources, args.mod);

  if (src == nullptr)
    return (union gab_value_pair){{gab_cinvalid, gab_cinvalid}};

  struct bc bc = {.ks = &src->constants, .src = src, .err = gab_cinvalid};

  args.env =
      unpack_bindings_into_env(gab, &bc, args.bindings, args.env, gab_cinvalid);

  if (args.env == gab_cinvalid)
    return assert(bc.err != gab_cinvalid),
           (union gab_value_pair){{gab_cinvalid, bc.err}};

  size_t nenvs = gab_reclen(args.env);
  assert(nenvs > 0);

  size_t nargs = gab_reclen(gab_uvrecat(args.env, nenvs - 1));
  assert(nargs < GAB_ARG_MAX);

  if (!push_trim_node(gab, &bc, nargs, gab_cinvalid, args.bindings))
    return assert(bc.err != gab_cinvalid),
           (union gab_value_pair){{gab_cinvalid, bc.err}};

  assert(bc.bc.len == bc.bc_toks.len);

  /*
   * The first tuple in a block is its *arguments*. We don't want to return
   * this tuple from the block when the block returns.
   * We push another, empty tuple here. This will be returned by the block.
   **/
  push_inst(&bc, (struct inst_arg){OP_TUPLE}, args.ast);

  args.env = compile_tuple(gab, &bc, args.ast, args.env);

  assert(bc.bc.len == bc.bc_toks.len);

  if (args.env == gab_cinvalid)
    return assert(bc.err != gab_cinvalid), bc_destroy(&bc),
           (union gab_value_pair){{gab_cinvalid, bc.err}};

  assert(gab_reclen(args.env) == nenvs);

  gab_value local_env = gab_uvrecat(args.env, nenvs - 1);
  assert(bc.bc.len == bc.bc_toks.len);

  push_ret(gab, &bc, args.ast, args.ast);

  size_t nlocals = locals_in_env(local_env);
  assert(nlocals < GAB_LOCAL_MAX);

  size_t nupvalues = upvalues_in_env(local_env);
  assert(nupvalues < GAB_UPVALUE_MAX);

  assert(bc.bc.len == bc.bc_toks.len);

  patch_init(&bc, nlocals);

  size_t len = bc.bc.len;
  size_t end = gab_srcappend(src, len, bc.bc.data, bc.bc_toks.data);

  bc_destroy(&bc);

  size_t begin = end - len;

  /*
   * Some blocks may have 0 upvalues.
   * To prevent undefined behavior, just allocate an extra
   * byte that is always unused.
   */
  char data[nupvalues + 1];
  build_upvdata(args.env, nupvalues, data);

  size_t bco = d_uint64_t_read(&bc.src->node_begin_toks, args.ast);
  v_uint64_t_set(&bc.src->bytecode_toks, begin, bco);

  gab_value proto = gab_prototype(gab, src, begin, len,
                                  (struct gab_prototype_argt){
                                      .nupvalues = nupvalues,
                                      .nlocals = nlocals,
                                      .narguments = nargs,
                                      .nslots = (nlocals + 3),
                                      .env = args.env,
                                      .data = data,
                                  });

  if (gab.flags & fGAB_BUILD_DUMP)
    gab_fmodinspect(stdout, proto);

  assert(bc.err == gab_cinvalid);

  return (union gab_value_pair){
      .status = gab_cvalid,
      .vresult = proto,
  };
}

union gab_value_pair gab_build(struct gab_triple gab,
                               struct gab_parse_argt args) {
  gab.flags |= args.flags;

  args.name = args.name ? args.name : "__main__";

  gab_gclock(gab);

  gab_value mod = gab_string(gab, args.name);

  union gab_value_pair ast = gab_parse(gab, args);

  assert(ast.vresult != gab_cundefined);
  if (ast.status != gab_cvalid)
    return gab_gcunlock(gab), ast;

  struct gab_src *src = d_gab_src_read(&gab.eg->sources, mod);

  if (src == nullptr)
    return gab_gcunlock(gab), (union gab_value_pair){.status = gab_cinvalid,
                                                     .vresult = gab_cundefined};

  // Default to empty list here
  gab_value bindings = gab_listof(gab);

  if (args.len) {
    gab_value vargs[args.len];

    for (int i = 0; i < args.len; i++)
      vargs[i] = gab_binary(gab, args.argv[i]);

    bindings = gab_list(gab, 1, args.len, vargs);
  }

  node_storeinfo(src, bindings, 0, 0);

  gab_value env =
      gab_listof(gab, gab_recordof(gab, gab_binary(gab, "self"), gab_nil));

  // TODO @bug: Repeatedly expand until we get no new expansions.
  union gab_value_pair res = expand_tuple(gab, ast.vresult, env);

  if (res.status == gab_cinvalid)
    return gab_gcunlock(gab), res;

  gab_value expanded_env = res.data[0];
  gab_value expanded_ast = res.data[1];

  res = gab_compile(gab, (struct gab_compile_argt){
                             .ast = expanded_ast,
                             .env = expanded_env,
                             .mod = mod,
                             .bindings = bindings,
                         });
  assert(res.vresult != gab_cundefined);

  if (res.status == gab_cinvalid)
    return gab_gcunlock(gab), res;

  gab_srccomplete(gab, src);

  gab_value main = gab_block(gab, res.vresult);
  assert(main != gab_cundefined);

  gab_iref(gab, main);
  gab_iref(gab, res.vresult);
  gab_egkeep(gab.eg, main);
  gab_egkeep(gab.eg, res.vresult);

  return gab_gcunlock(gab),
         (union gab_value_pair){.status = gab_cvalid, .vresult = main};
}

// -- VM --

/**
 * @file
 * @brief This file contains the interpreter for Gab's bytecode.
 *
 * This interpreter is stack-based, which is quite conventional. There are
 * however some unconventional pieces to it.
 *
 * --< INTERPRETER DISPATCH >--
 *
 * Most implementations of a bytecode interpreter
 * include a switch statement in a big loop, or sometimes a more optimized
 * goto-loop. In Gab, we use a tail-calling interpreter. Each opcode is
 * implemented as a function adhering to the same interface (defined here with
 * macros, @see handler and @see OP_HANDLER_ARGS).
 *
 * When these opcodes finish their work, they tail-call into the next opcode
 * (@see DISPATCH). We annotate each of these return statements with
 * [[clang::musttail]], to ensure that the compiler can and does emit a tail
 * call at each of these call sites.
 *
 * - PROS -
 * Each instruction is its own function. This is easier to reason about when
 * implementing op codes, as there is no mutable state global to the
 * interpreting function. On top of that, it is more likely that the compiler
 * keeps crucial VM variables like the stack pointer and instruction pointer in
 * REGISTERS, because they are confined to registers by calling convention. (It
 * is for this reason that the stack pointer, constant pointer, and instruction
 * pointer are unboxed and passed as arguments, as opposed to just updating the
 * values in the gab_vm struct).
 *
 * Since each of our op-codes are individual functions, debugging tools like
 * callgrind can be used to create web-like visualizations, which show what
 * opcodes often follow others, and how much time is spent in each.
 *
 * - CONS -
 * Implementation requires more understanding of how c function calls work - and
 * there are limitations on these functions that are unclear. (Such as, no
 * dynamic stack allocations, like using int a[n], where n is not known at
 * compile time.)
 *
 * Tail-calling also makes for somewhat confusing stack traces, and can confuse
 * some debugging / performance tools. (Most do a good enough job though).
 *
 * --< STACK BASED TUPLES >--
 *
 * Gab as a language makes heavy use of ~tuples~. (returning multiple values
 * from a function, etc). To avoid allocating these as slices in memory, Gab
 * stores these tuples on its internal VM stack. The top of the stack (pointed
 * to by SP()), stores the *number of values* in the tuple below it. When a
 * value is pushed, that number is incremented. This is how function calls and
 * returns know how many values are present. (and how something like (1, 2,
 * func.send, 5, 6) => (1, 2, 3, 4, 5, 6) is able to work.
 *
 * There is plenty of runtime overhead in tracking this. But it is made up for
 * by the amount of allocation that is *Saved* through using this system
 * instead.
 *
 * --< INVARIANTS >--
 * There are some invariants which must hold true in these opcodes. It is
 * impossible to encode them into the c-typesystem, so I try to write them down
 * here. There may be more in my head which aren't written down.
 *
 *  1. Before calling out to a gab_* api function, STORE() must be called. This
 * stores the cached variables for the stack pointer and frame pointer into the
 * VM struct. Without this call, the gab_* api function will see an out-of-date
 * version of the fiber's stack.
 *
 *  2. Opcodes which may yield (by calling VM_YIELD()), much first make a call
 * to CHECK_SIGNAL(). This is to guarantee that a signal is received and
 * processed if the interpreter resumes at that specific opcode.
 *
 */

/*
 * move-macros for shifting slices of gab values up and down the vm stack.
 * Thereis a separate macro for an ascending-move (up the stack)
 * and a descending-move (down the stack). This is because the moves
 * must account for overlapping regions, so the dst doesn't overwrite the src.
 */
#define gmoved(dst, src, n)                                                    \
  ({                                                                           \
    gab_value *D = dst;                                                        \
    gab_value *S = src;                                                        \
    uint64_t N = n;                                                            \
    if (N && D != S) {                                                         \
      gab_assert(D < S, "Tried to write ascending move. %lu", D - S);          \
      while (N--)                                                              \
        *D++ = *S++;                                                           \
    }                                                                          \
  })
// #define gmoved(dst, src, n) memmove(dst, src, n * sizeof(gab_value))

#define gmovea(dst, src, n)                                                    \
  ({                                                                           \
    gab_value *D = dst;                                                        \
    gab_value *S = src;                                                        \
    uint64_t N = n;                                                            \
    if (N && D != S) {                                                         \
      D += N - 1;                                                              \
      S += N - 1;                                                              \
      gab_assert(S < D, "Tried to write descending move. %lu", S - D);         \
      while (N--)                                                              \
        *D-- = *S--;                                                           \
    }                                                                          \
  })
// #define gmovea(dst, src, n) memmove(dst, src, n * sizeof(gab_value))

#define OP_HANDLER_ARGS                                                        \
  struct gab_triple *__gab, struct gab_vm *__vm, uint8_t *__ip,                \
      gab_value *__kb, gab_value *__fb, gab_value *__sp

#define CASE_CODE(name)                                                        \
  cGAB_VM_OPCODE_ATTRIBUTES union gab_value_pair OP_##name##_HANDLER(          \
      OP_HANDLER_ARGS)

#define DISPATCH_ARGS() __gab, __vm, __ip, __kb, __fb, __sp

cGAB_VM_OPCODE_ATTRIBUTES typedef union gab_value_pair (*handler)(
    OP_HANDLER_ARGS);

// Forward declare all our opcode handlers
#define OP_CODE(name)                                                          \
  cGAB_VM_OPCODE_ATTRIBUTES union gab_value_pair OP_##name##_HANDLER(          \
      OP_HANDLER_ARGS);
#include "bytecode.h"
#undef OP_CODE

// Plop them all in an array
static handler handlers[] = {
#define OP_CODE(name) OP_##name##_HANDLER,
#include "bytecode.h"
#undef OP_CODE
};

#define GAB() (*__gab)
#define EG() (GAB().eg)
#define FIBER() (GAB_VAL_TO_FIBER(gab_thisfiber(GAB())))
#define REENTRANT() (FIBER()->reentrant)
#define RESET_REENTRANT() (FIBER()->reentrant = 0)
#define RESET_BUMP() (FIBER()->allocator.len = 0)
#define GC() (GAB().eg->gc)
#define VM() (__vm)
#define SET_BLOCK(b) ({ FB()[-(1 + FRAME_BK)] = (uintptr_t)(b); });
#define BLOCK() ((struct gab_oblock *)(uintptr_t)FB()[-(1 + FRAME_BK)])
#define BLOCK_PROTO()                                                          \
  ({                                                                           \
    gab_assert(BLOCK(), "Null block while accessing block prototype");         \
    GAB_VAL_TO_PROTOTYPE(BLOCK()->p);                                          \
  })
#define IP() (__ip)
#define SP() (__sp)
#define SB() (VM()->sb)
#define FB() (__fb)
#define HV() (*SP())
#define BELOW_HV() (PEEK_N(HV() + 1))
#define UPV() (BLOCK()->upvalues)
#define KB() (__kb)
#define LOCAL(i) (FB()[i])
#define STORE_LOCAL(i, v) (LOCAL(i) = v)
#define UPVALUE(i) (BLOCK()->upvalues[i])

#if cGAB_LOG_VM
#define LOG(gab, op)                                                           \
  fprintf(stderr, "%p OP_%s [%lu] (%i)\n", IP() - 1, gab_opcode_names[op],     \
          HV(), GAB().wkid);
#else
#define LOG(gab, op)
#endif

#define CHECK_SIGNAL()                                                         \
  if (gab_sigwaiting(GAB()))                                                   \
    switch (gab_yield(GAB())) {                                                \
    case sGAB_COLL:                                                            \
      STORE_SP();                                                              \
      gab_gcepochnext(GAB());                                                  \
      gab_sigpropagate(GAB());                                                 \
      break;                                                                   \
    case sGAB_TERM:                                                            \
      STORE_SP();                                                              \
      VM_TERM();                                                               \
    default:                                                                   \
      break;                                                                   \
    }

// assert(SP() >= FB());
#define DISPATCH(op)                                                           \
  ({                                                                           \
    uint8_t o = (op);                                                          \
                                                                               \
    LOG(GAB(), o);                                                             \
                                                                               \
    assert(SP() < VM()->sb + cGAB_STACK_MAX);                                  \
                                                                               \
    [[clang::musttail]] return handlers[o](DISPATCH_ARGS());                   \
  })

#define NEXT_CHECKED()                                                         \
  ({                                                                           \
    CHECK_SIGNAL();                                                            \
    NEXT();                                                                    \
  })

#define NEXT() ({ DISPATCH(*(IP()++)); })
// #define NEXT() NEXT_CHECKED()

#define VM_PANIC(status)                                                       \
  ({                                                                           \
    STORE();                                                                   \
    SP()[1] = status;                                                          \
    [[clang::musttail]] return vm_eerror(DISPATCH_ARGS());                     \
  })

#define VM_PANIC3(status, a, b, c)                                             \
  ({                                                                           \
    STORE();                                                                   \
    SP()[1] = status;                                                          \
    SP()[2] = a;                                                               \
    SP()[3] = b;                                                               \
    SP()[4] = c;                                                               \
    [[clang::musttail]] return vm_eerror(DISPATCH_ARGS());                     \
  })

#define VM_PANIC5(status, a, b, c, d, e)                                       \
  ({                                                                           \
    STORE();                                                                   \
    SP()[1] = status;                                                          \
    SP()[2] = a;                                                               \
    SP()[3] = b;                                                               \
    SP()[4] = c;                                                               \
    SP()[5] = d;                                                               \
    SP()[6] = e;                                                               \
    [[clang::musttail]] return vm_eerror(DISPATCH_ARGS());                     \
  })

#define VM_GIVEN(err)                                                          \
  ({                                                                           \
    STORE();                                                                   \
    return vm_givenerr(GAB(), err);                                            \
  })

#define GET_STACKSPACE(sp, sb) ((sp - sb) + 3)

#define HAS_STACKSPACE(sp, sb, space)                                          \
  (GET_STACKSPACE(sp, sb) + space < cGAB_STACK_MAX)

// assert(SP() >= FB());
#define SET_HV(n) ({ *SP() = n; })

#define PUSH_FRAME(b, have)                                                    \
  ({                                                                           \
    assert(have < UINT32_MAX);                                                 \
                                                                               \
    int64_t delta = (SP() - have) - FB();                                      \
                                                                               \
    assert((SP() - have) > FB());                                              \
    assert(delta > 0);                                                         \
    assert(delta < UINT32_MAX);                                                \
    assert(SP()[-(int64_t)(have + 1 + FRAME_IP)] == FRAME_IP);                 \
    assert(SP()[-(int64_t)(have + 1 + FRAME_BK)] == FRAME_BK);                 \
                                                                               \
    SP()[-(int64_t)(have + 1)] |= ((uint64_t)delta << 32);                     \
    SP()[-(int64_t)(have + 1 + FRAME_IP)] = (uintptr_t)IP();                   \
    SP()[-(int64_t)(have + 1 + FRAME_BK)] = (uintptr_t)b;                      \
  })

#define PUSH_VM_PANIC_FRAME(have) ({})

#define STORE_MICRO_OP_VM_PANIC_FRAME(have)                                    \
  ({                                                                           \
    STORE();                                                                   \
    PUSH_VM_PANIC_FRAME(have);                                                 \
  })

#define RETURN_FB_DELTA() (FB()[-(1)] >> 32)
#define RETURN_FB() ((FB() - RETURN_FB_DELTA()))

#define RETURN_IP() ((uint8_t *)(void *)FB()[-(1 + FRAME_IP)])
#define RETURN_BK() ((struct gab_oblock *)(void *)FB()[-(1 + FRAME_BK)])
#define RETURN_HAVE() (FB()[-(1)] & 0xffffffff)

#define LOAD_FRAME()                                                           \
  ({                                                                           \
    IP() = RETURN_IP();                                                        \
    FB() = RETURN_FB();                                                        \
    KB() = proto_ks(GAB(), BLOCK_PROTO());                                     \
    assert(GAB_VAL_TO_FIBER(gab_thisfiber(GAB()))->vm.sb[2] == 0);             \
  })

#if cGAB_DEBUG_VM
#define PUSH(value)                                                            \
  ({                                                                           \
    if (SP() > (FB() + BLOCK_PROTO()->nslots + 1)) {                           \
      fprintf(gab.eg->stderr,                                                  \
              "Stack exceeded frame "                                          \
              "(%d). %lu passed\n",                                            \
              BLOCK_PROTO()->nslots, SP() - FB() - BLOCK_PROTO()->nslots);     \
      gab_fvminspect(stdout, VM(), 0);                                         \
      exit(1);                                                                 \
    }                                                                          \
    *SP()++ = value;                                                           \
  })

#else
#define PUSH(value) (*SP()++ = value)
#endif
#define POP() (*(--SP()))
#define DROP() (SP()--)
#define POP_N(n) (SP() -= (n))
#define DROP_N(n) (SP() -= (n))
#define PEEK() (*(SP() - 1))
#define PEEK2() (*(SP() - 2))
#define PEEK3() (*(SP() - 3))
#define PEEK_N(n) (*(SP() - (n)))

#define WRITE_BYTE(dist, n) ({ *(IP() - dist) = (n); })

#define WRITE_INLINEBYTE(n) (*IP()++ = (n))

#define SKIP_BYTE (IP()++)
#define SKIP_SHORT (IP() += 2)

#define READ_BYTE (*IP()++)
#define READ_SHORT (IP() += 2, (((uint16_t)IP()[-2] << 8) | IP()[-1]))

#define READ_CONSTANT (KB()[READ_SHORT])

// Turn off the highest bit, as this is used to store tail-calling information.
#define READ_SENDCONSTANTS                                                     \
  ({                                                                           \
    uint16_t shrt = READ_SHORT & (~(fHAVE_TAIL << 8));                         \
    KB() + shrt;                                                               \
  })

#define READ_SENDCONSTANTS_ANDTAIL(t)                                          \
  ({                                                                           \
    uint16_t shrt = READ_SHORT;                                                \
    t = ((shrt & (fHAVE_TAIL << 8)) != 0);                                     \
    KB() + (shrt & ~(fHAVE_TAIL << 8));                                        \
  })

// The *below_have* is 64 bits of space. It already exists on stack when
// The send is *sent*. Is there a way to convert this value into
// a stack frame? Can we jam everything we need into 64 bits?
//
// Store:
// - Return frame base
// - Return ip
// - Return block
// - Return have
//
// - Return frame base can be 8 bits of delta, from new fb to old.
// - Return ip is a pointer, which is 48 bits of data.
// - Return block is also a pointer, which is 48 bits of data.
// - Return have should really be 32, but we can make it 8
//
// - A send is always followed by a trim or a pack, so storing anything further
// - up on the stack doesn't really work either.
// - Maybe I can store the block @ the IP somehow?
//
// - If I am returning from a frame, I am returning to an IP which should have
// - send with a block in ks[GAB_SEND_KSPEC].
// - Problem - without knowing what the block is, we can't actually know *where*
// - the ks are. So its a chicken and egg situation.
//
// - Also, that returning send block in ks[GAB_SEND_KSPEC] is also the frame
// that
// - *is returning*, not the frame *to return to*.
//
// It also isn't known at compile time how big a stack frame can get, so
// putting it underneath is really the only option.
//
// A FB() Delta is stored in the same machine word as the below have. This
// saves 8 bytes of space on the stack.
//
// The upper 4 bytes are the frame delta, and the lower 4 bytes are the have.

#define FRAME_SIZE 2
#define FRAME_IP 1
#define FRAME_BK 2

static inline gab_value *frame_parent(gab_value *f) {
  int64_t delta = f[-(1)] >> 32;
  return delta ? f - delta : nullptr;
}

static inline struct gab_oblock *frame_block(gab_value *f) {
  return (void *)f[-(1 + FRAME_BK)];
}

static inline uint8_t *frame_ip(gab_value *f) {
  return (void *)f[-(1 + FRAME_IP)];
}

static inline uint64_t compute_token_from_ip(struct gab_triple gab,
                                             struct gab_oblock *b,
                                             uint8_t *ip) {
  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

  assert(ip >= proto_srcbegin(gab, p));
  uint64_t offset = ip - proto_srcbegin(gab, p);

  if (offset)
    offset--;

  uint64_t token = v_uint64_t_val_at(&p->src->bytecode_toks, offset);

  return token;
}

struct gab_err_argt vm_frame_build_err(struct gab_triple gab,
                                       struct gab_oblock *b, uint8_t *ip,
                                       enum gab_status s, const char *fmt) {
  if (b) {
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

    uint64_t tok = compute_token_from_ip(gab, b, ip);

    return (struct gab_err_argt){
        .tok = tok,
        .src = p->src,
        .note_fmt = fmt,
        .status = s,
        .wkid = gab.wkid,
    };
  }

  return (struct gab_err_argt){
      .note_fmt = fmt,
      .status = s,
      .wkid = gab.wkid,
  };
}

union gab_value_pair vm_yield(struct gab_triple gab, uintptr_t value) {
  gab_value f = gab_thisfiber(gab);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  assert(value != 0);

  assert(fiber->header.kind = kGAB_FIBERRUNNING);
  fiber->header.kind = kGAB_FIBER;
  fiber->reentrant = value;

  return (union gab_value_pair){{gab_ctimeout, f}};
}

gab_value sprint_stacktrace(struct gab_triple gab, struct gab_vm *vm,
                            gab_value *f, uint8_t *ip, int s, const char *fmt,
                            va_list va) {
  // TODO @cgab: Place reasonable limit on number of frames to sprint.
  // Also, skip middle ones sometimes.
  int nframes = 0;
  gab_value vframes[1024] = {0};

  struct gab_err_argt frame =
      vm_frame_build_err(gab, frame_block(f), ip, s, fmt);

  vframes[nframes] = gab_vspanicf(gab, va, frame);
  if (vframes[nframes] != gab_cinvalid)
    nframes++;

  ip = frame_ip(f);
  f = frame_parent(f);

  while (f && frame_parent(f) > vm->sb) {
    frame = vm_frame_build_err(gab, frame_block(f), ip, GAB_NONE, "");
    vframes[nframes] = gab_vspanicf(gab, va, frame);
    if (vframes[nframes] != gab_cinvalid)
      nframes++;

    ip = frame_ip(f);
    f = frame_parent(f);
  }

  if (nframes)
    return gab_list(gab, 1, nframes, vframes);
  else
    return gab_cinvalid;
}

gab_value gab_fibstacktrace(struct gab_triple gab, gab_value fiber) {
  struct gab_vm *vm = gab_fibvm(fiber);

  gab_value *f = vm->fp;
  uint8_t *ip = vm->ip;

  va_list empty;
  return sprint_stacktrace(gab, vm, f, ip, GAB_TERM, nullptr, empty);
}

union gab_value_pair vvm_terminate(struct gab_triple gab, const char *fmt,
                                   va_list va) {
  gab_value fiber = gab_thisfiber(gab);

  /*
   * It is possible that a fiber is *done* here, if gab_panic was called in a
   * native fn.
   */
  if (gab_valkind(fiber) == kGAB_FIBERDONE)
    return GAB_VAL_TO_FIBER(fiber)->res_values;

  gab_assert(gab_valkind(fiber) == kGAB_FIBERRUNNING,
             "(%i) Terminating fiber %p must be running, not: %d. Terminating.",
             gab.wkid, GAB_VAL_TO_FIBER(fiber), gab_valkind(fiber));

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_env == gab_cinvalid,
             "Terminating fiber res_env shall be uninitialized.");

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_values.status == 0,
             "Terminating fiber res shall be uninitialized.");

  struct gab_vm *vm = gab_thisvm(gab);

  // gab_value *f = vm->fp;
  // uint8_t *ip = vm->ip;

  // gab_value err = sprint_stacktrace(gab, vm, f, ip, GAB_TERM, fmt, va);
  //
  // gab_iref(gab, err);
  // gab_egkeep(gab.eg, err);

  union gab_value_pair res = {{gab_cinvalid, gab_cinvalid}};

  struct gab_oblock *blk = frame_block(vm->fp);
  gab_value env;

  if (blk) {
    gab_value p = blk->p;
    gab_value shape = gab_prtshp(p);
    env = gab_recordfrom(gab, shape, 1, gab_shplen(shape), vm->fp, nullptr);
  } else {
    env = gab_recordof(gab);
  }

  gab_egkeep(gab.eg, gab_iref(gab, env));

  GAB_VAL_TO_FIBER(fiber)->res_values = res;
  GAB_VAL_TO_FIBER(fiber)->res_env = env;
  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERDONE;
#if cGAB_LOG_VM
  gab_fprintf(stderr, "($) VMTERM finished fiber $.\n", gab_number(gab.wkid),
              __gab_obj(fiber));
#endif

  return res;
}

union gab_value_pair vm_givenerr(struct gab_triple gab,
                                 union gab_value_pair given) {
  gab_value fiber = gab_thisfiber(gab);

  /*
   * It is possible that a fiber is *done* here, if gab_panic was called in a
   * native fn.
   */
  if (gab_valkind(fiber) == kGAB_FIBERDONE)
    return GAB_VAL_TO_FIBER(fiber)->res_values;

  gab_assert(gab_valkind(fiber) == kGAB_FIBERRUNNING,
             "Terminating fiber %p must be running, not: %d. Given err.",
             GAB_VAL_TO_FIBER(fiber), gab_valkind(fiber));

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_env == gab_cinvalid,
             "Terminating fiber res_env shall be uninitialized.");

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_values.status == 0,
             "Terminating fiber res shall be uninitialized.");

  struct gab_vm *vm = gab_thisvm(gab);

  GAB_VAL_TO_FIBER(fiber)->res_values = given;

  if (frame_block(vm->fp)) {
    gab_value p = frame_block(vm->fp)->p;
    gab_value shape = gab_prtshp(p);

    gab_value env =
        gab_recordfrom(gab, shape, 1, gab_shplen(shape), vm->fp, nullptr);
    gab_egkeep(gab.eg, gab_iref(gab, env));

    GAB_VAL_TO_FIBER(fiber)->res_env = env;
  }

  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERDONE;
#if cGAB_LOG_VM
  gab_fprintf(stderr, "($) VVMGIVEN ERR finished fiber $.\n",
              gab_number(gab.wkid), __gab_obj(fiber));
#endif

  return given;
}

union gab_value_pair vvm_error(struct gab_triple gab, enum gab_status s,
                               const char *fmt, va_list va) {
  gab_value fiber = gab_thisfiber(gab);

  gab_assert(
      gab_valkind(fiber) == kGAB_FIBERRUNNING,
      "(%i) Terminating fiber must be running, not: %d. Error status %s.",
      gab.wkid, gab_valkind(fiber), gab_status_names[s]);

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_env == gab_cinvalid,
             "Terminating fiber res_env shall be uninitialized.");

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_values.status == 0,
             "Terminating fiber res shall be uninitialized.");

  struct gab_vm *vm = gab_thisvm(gab);

  gab_value *f = vm->fp;
  uint8_t *ip = vm->ip;

  gab_value err = sprint_stacktrace(gab, vm, f, ip, s, fmt, va);

  if (err == gab_cinvalid)
    return vvm_terminate(gab, "While executing $\n", va);

  gab_iref(gab, err);
  gab_egkeep(gab.eg, err);

  gab_value vals[] = {gab_err, err};
  a_gab_value *results =
      a_gab_value_create(vals, sizeof(vals) / sizeof(gab_value));

  gab_niref(gab, 1, results->len, results->data);
  gab_negkeep(gab.eg, results->len, results->data);

  union gab_value_pair res = {.status = gab_cvalid, .aresult = results};

  assert(GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERRUNNING);

  GAB_VAL_TO_FIBER(fiber)->res_values = res;
  if (frame_block(vm->fp)) {
    gab_value p = frame_block(vm->fp)->p;
    gab_value shape = gab_prtshp(p);

    gab_value env =
        gab_recordfrom(gab, shape, 1, gab_shplen(shape), vm->fp, nullptr);
    gab_egkeep(gab.eg, gab_iref(gab, env));
    assert(GAB_VAL_TO_FIBER(fiber)->res_env == gab_cinvalid);
    GAB_VAL_TO_FIBER(fiber)->res_env = env;
  }
  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERDONE;
#if cGAB_LOG_VM
  gab_fprintf(stderr, "($) VVMERR finished fiber $.\n", gab_number(gab.wkid),
              __gab_obj(fiber));
#endif

  return res;
}

union gab_value_pair vm_terminate(struct gab_triple gab, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  union gab_value_pair res = vvm_terminate(gab, fmt, va);

  va_end(va);

  return res;
}

union gab_value_pair vm_error(struct gab_triple gab, enum gab_status s,
                              const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  union gab_value_pair res = vvm_error(gab, s, fmt, va);

  va_end(va);

  return res;
}

#define FMT_TYPEMISMATCH                                                       \
  "@ @ found a value with an unexpected type.\n\n"                             \
  "$\n\n"                                                                      \
  "which has type\n\n"                                                         \
  "$\n\n"                                                                      \
  "but expected type\n\n"                                                      \
  "$\n"

#define FMT_MISSINGIMPL                                                        \
  "Sent message @ does not specialize for this receiver.\n\n"                  \
  "$\n\n"                                                                      \
  "of type\n\n"                                                                \
  "$\n"

union gab_value_pair gab_vpanicf(struct gab_triple gab, const char *fmt,
                                 va_list va) {
  if (gab_thisfiber(gab) == gab_cinvalid) {
    gab_value err = gab_vspanicf(gab, va,
                                 (struct gab_err_argt){
                                     .status = GAB_PANIC,
                                     .note_fmt = fmt,
                                     .wkid = gab.wkid,
                                 });

    if (err != gab_cinvalid) {
      gab_iref(gab, err);
      gab_egkeep(gab.eg, err);
    }

    gab_value res[] = {gab_err, err};
    a_gab_value *results =
        a_gab_value_create(res, sizeof(res) / sizeof(gab_value));

    return (union gab_value_pair){
        .status = gab_cvalid,
        .aresult = results,
    };
  };

  union gab_value_pair res = vvm_error(gab, GAB_PANIC, fmt, va);

  return res;
}

union gab_value_pair gab_panicf(struct gab_triple gab, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  union gab_value_pair res = gab_vpanicf(gab, fmt, va);

  va_end(va);

  return res;
}

// This isn't intuitive when compared to the MISS_CACHED_SEND
// code, but operator precedence is tricky like that.
// Because the other code is ip -= 3 -1 => ip -= (3 - 1)
// whereas here ip = other_ip - 3 - 1 => ip = (other_ip - 3) - 1
//
// So we add one instead and all lines up.
gab_value gab_vmmsg(struct gab_vm *vm) {
  uint8_t *__ip = vm->ip - GAB_SEND_CACHE_SIZE + 1;
  gab_value *__kb = vm->kb;
  gab_value *ks = READ_SENDCONSTANTS;
  return ks[GAB_SEND_KMESSAGE];
}

gab_value gab_vmspec(struct gab_vm *vm) {
  uint8_t *__ip = vm->ip - GAB_SEND_CACHE_SIZE + 1;
  gab_value *__kb = vm->kb;
  gab_value *ks = READ_SENDCONSTANTS;
  return ks[GAB_SEND_KSPEC];
}

union gab_value_pair gab_ptypemismatch(struct gab_triple gab, gab_value found,
                                       gab_value texpected) {
  gab_value msg = gab_vmmsg(gab_thisvm(gab));
  gab_value spec = gab_vmspec(gab_thisvm(gab));
  gab_value tfound = gab_valtype(gab, found);
  return vm_error(gab, GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, msg, spec, found,
                  tfound, texpected);
}

gab_value gab_vmframe(struct gab_triple gab, uint64_t depth) {
  // uint64_t frame_count = gab_vm(gab)->fp - gab_vm(gab)->sb;
  //
  // if (depth >= frame_count)
  return gab_cinvalid;

  // struct gab_vm_frame *f = gab_vm(gab)->fp - depth;
  //
  // const char *keys[] = {
  //     "line",
  // };
  //
  // gab_value line = gab_nil;
  //
  // if (f->b) {
  //   struct gab_src *src = GAB_VAL_TO_PROTOTYPE(f->b->p)->src;
  //   uint64_t tok = compute_token_from_ip(f);
  //   line = gab_number(v_uint64_t_val_at(&src->token_lines, tok));
  // }
  //
  // gab_value values[] = {
  //     line,
  // };
  //
  // uint64_t len = sizeof(keys) / sizeof(keys[0]);
  //
  // return gab_srecord(gab, len, keys, values);
}

void gab_fvminspect(FILE *stream, struct gab_vm *vm, int depth) {
  // uint64_t frame_count = vm->fp - vm->fb;
  //
  // if (value > frame_count)
  // return;

  // struct gab_vm_frame *f = vm->fp - value;
  //
  // struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(f->b->p);
  //
  // fprintf(stream,
  //         GAB_GREEN " %03lu" GAB_RESET " closure:" GAB_CYAN "%-20s" GAB_RESET
  //                   " %d upvalues\n",
  //         frame_count - value, gab_strdata(&p->src->name), p->nupvalues);

  gab_value *f = vm->fp;
  gab_value *t = vm->sp - 1;

  while (depth > 0) {
    if (frame_parent(f) > vm->sb) {
      t = f - 4;
      f = frame_parent(f);
      depth--;
    } else {
      return;
    }
  }

  gab_fvalinspect(stream, __gab_obj(frame_block(f)), 0);
  fprintf(stream, "\n");

  while (t >= f) {
    fprintf(stream, "%2s" GAB_YELLOW "%4" PRIu64 " " GAB_RESET,
            vm->sp == t ? "->" : "", (uint64_t)(t - vm->sb));
    gab_fvalinspect(stream, *t, 0);
    fprintf(stream, "\n");
    t--;
  }
}

void gab_fvminspectall(FILE *stream, struct gab_vm *vm) {
  for (uint64_t i = 0; i < 64; i++) {
    gab_fvminspect(stream, vm, i);
  }
}
gab_value gab_vmpop(struct gab_vm *vm) {
  if (__gab_unlikely(vm->sp == vm->sb))
    return gab_cundefined;

  uint64_t have = *vm->sp;
  gab_value popped = *(--vm->sp);
  *vm->sp = have - 1;
  return popped;
}

gab_value gab_vmpeek(struct gab_vm *vm, uint64_t dist) {
  if (__gab_unlikely(vm->sp - dist < vm->sb))
    return gab_cundefined;

  return vm->sp[-(int64_t)(dist + 1)];
}

uint64_t gab_nvmpush(struct gab_vm *vm, uint64_t argc, gab_value argv[argc]) {
  if (__gab_unlikely(argc == 0 || !HAS_STACKSPACE(vm->sp, vm->sb, argc))) {
    return 0;
  }

  uint64_t have = *vm->sp;

  for (uint8_t n = 0; n < argc; n++) {
    *vm->sp++ = argv[n];
  }

  *vm->sp = have + argc;

  return argc;
}

cGAB_VM_OPCODE_ATTRIBUTES union gab_value_pair vm_eerror(OP_HANDLER_ARGS) {
  enum gab_status status = SP()[1];
  switch (status) {
  case GAB_OVERFLOW:
    return vm_error(GAB(), status, "");
  case GAB_PANIC:
    return vm_error(GAB(), status, "");
  case GAB_SPECIALIZATION_MISSING:
    return vm_error(GAB(), status, FMT_MISSINGIMPL, SP()[2], SP()[3], SP()[4]);
  case GAB_TYPE_MISMATCH:
    return vm_error(GAB(), status, FMT_TYPEMISMATCH, SP()[2], SP()[3], SP()[4],
                    SP()[5], SP()[6]);
  default:
    assert(false && "Unreachable");
  }
}

cGAB_VM_OPCODE_ATTRIBUTES union gab_value_pair vm_ok(OP_HANDLER_ARGS) {
  uint64_t have = *VM()->sp;
  gab_value *from = VM()->sp - have;

  // TODO @bug: When gab_sending just a send constant call, we hit a seg fault.
  // This catches the issue - we overwrite the frame here without updating fb
  // I think.
  // assert(FB() < SP());
  // Once the fiber has returned, the local values have been overwritten
  // with return values. This makes extracting new locals impossible.

  a_gab_value *results = a_gab_value_empty(have + 1);
  results->data[0] = gab_ok;
  memcpy(results->data + 1, from, have * sizeof(gab_value));

  gab_niref(GAB(), 1, results->len, results->data);
  gab_negkeep(EG(), results->len, results->data);

  union gab_value_pair res = (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = results,
  };

  VM()->sp = VM()->sb;

  struct gab_ofiber *fiber = FIBER();

  // if (fiber->header.kind == kGAB_FIBERDONE) {
  //   gab_fprintf(stderr, "($) OK'd finished fiber $.\n",
  //   gab_number(GAB().wkid),
  //               __gab_obj(fiber));
  //   gab_fprintf(stderr, "STATUS: $\n", fiber->res_values.status);
  //   gab_fprintf(stderr, "VRESULT: $\n", fiber->res_values.vresult);
  // }
  gab_assert(fiber->header.kind == kGAB_FIBERRUNNING,
             "(%i) Terminating fiber %p must be running, not: %d. OK!",
             GAB().wkid, fiber, fiber->header.kind);

  gab_assert(fiber->res_env == gab_cinvalid,
             "Terminating fiber res_env shall be uninitialized.");

  gab_assert(fiber->res_values.status == 0,
             "Terminating fiber res shall be uninitialized.");

  fiber->res_values = res;

  // TODO @bug: Find some way to pull the env out of a fiber.
  // if (frame_block(VM()->fp)) {
  //   gab_value p = frame_block(VM()->fp)->p;
  //   gab_value shape = gab_prtshp(p);
  //
  //   gab_value env =
  //       gab_recordfrom(GAB(), shape, 1, gab_shplen(shape), VM()->fp,
  //       nullptr);
  //
  //   gab_egkeep(EG(), gab_iref(GAB(), env));
  //
  //   fiber->res_env = env;
  // }

  fiber->header.kind = kGAB_FIBERDONE;
#if cGAB_LOG_VM
  gab_fprintf(stderr, "($) VMOK finished fiber $.\n", gab_number(GAB().wkid),
              __gab_obj(fiber));
#endif

  return res;
}

union gab_value_pair do_vmexecfiber(struct gab_triple gab, gab_value f) {
  assert(gab_valkind(f) == kGAB_FIBER);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  assert(fiber->header.kind != kGAB_FIBERDONE);

  assert(fiber->vm.sb[2] == 0);
  assert(fiber->vm.kb);
  assert(fiber->vm.ip);

  uint8_t *ip = fiber->vm.ip;

  uint8_t op = *ip++;

  // We can't return *to* this frame because it has no block.
  // But we *should* return here so that the environment returned
  // to the fiber is as expected

  assert(fiber->header.kind != kGAB_FIBERDONE);
  fiber->header.kind = kGAB_FIBERRUNNING;

  return handlers[op](&gab, &fiber->vm, ip, fiber->vm.kb, fiber->vm.fp,
                      fiber->vm.sp);
};

union gab_value_pair gab_vmexec(struct gab_triple gab, gab_value f) {
  gab_assert(gab_valkind(f) == kGAB_FIBER,
             "Only gab\\fiber shall be exec'd. Not a value of type: %d",
             gab_valkind(f));
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  gab.flags |= fiber->flags;

  return do_vmexecfiber(gab, f);
}

static inline bool try_setup_localmatch(struct gab_triple gab, gab_value m,
                                        gab_value *ks,
                                        struct gab_oprototype *p) {
  gab_value specs = gab_thisfibmsgrec(gab, m);

  if (specs == gab_cundefined)
    return false;

  if (gab_reclen(specs) > 4 || gab_reclen(specs) < 2)
    return false;

  uint64_t len = gab_reclen(specs);

  for (uint64_t i = 0; i < len; i++) {
    gab_value spec = gab_uvrecat(specs, i);

    if (gab_valkind(spec) != kGAB_BLOCK)
      return false;

    struct gab_oblock *b = GAB_VAL_TO_BLOCK(spec);
    struct gab_oprototype *spec_p = GAB_VAL_TO_PROTOTYPE(b->p);

    if (spec_p->src != p->src)
      return false;

    gab_value t = gab_ukrecat(specs, i);

    uint8_t idx = GAB_SEND_HASH(t) * GAB_SEND_CACHE_SIZE;

    // We have a collision - no point in messing about with this.
    if (ks[GAB_SEND_KSPEC + idx] != gab_cinvalid)
      return false;

    uint8_t *ip = proto_ip(gab, spec_p);

    ks[GAB_SEND_KTYPE + idx] = t;
    ks[GAB_SEND_KSPEC + idx] = (intptr_t)b;
    ks[GAB_SEND_KOFFSET + idx] = (intptr_t)ip;
  }

  ks[GAB_SEND_KSPECS] = atomic_load(&gab.eg->messages_epoch);
  return true;
}

/*
 * This file defines implementations for an extensive set of macros.
 *
 * These macros are used by `ops.h`, which definies the bytecode operations
 * in terms of these smaller, primitive bytecodes.
 */

/* IMPL in vm.c */
union gab_value_pair vm_terminate(struct gab_triple gab, const char *fmt, ...);

/* IMPL in vm.c */
union gab_value_pair vm_yield(struct gab_triple gab, uintptr_t value);

extern void putl(uintptr_t arg);
extern void putp(uintptr_t arg);
extern void puti(int64_t arg);
extern void putf(double arg);
extern void putg(gab_value arg);
extern void putcs(char *arg);

#define PANIC_GUARD_STACKSPACE(space)                                          \
  if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), space)))                      \
    VM_PANIC(GAB_OVERFLOW);

#define PANIC_GUARD_STACKSPACE_SPLATDICT(r)                                    \
  ({                                                                           \
    uint64_t n = gab_shplen(r) * 2;                                            \
    if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), n)))                        \
      VM_PANIC(GAB_OVERFLOW);                                                  \
    n;                                                                         \
  })

#define PANIC_GUARD_STACKSPACE_SPLATLIST(r)                                    \
  ({                                                                           \
    uint64_t n = gab_shplen(r);                                                \
    if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), n)))                        \
      VM_PANIC(GAB_OVERFLOW);                                                  \
    n;                                                                         \
  })

#define PANIC_GUARD_STACKSPACE_SPLATSHAPE(r)                                   \
  if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), gab_shplen(r))))              \
    VM_PANIC(GAB_OVERFLOW);

#define PANIC_GUARD_SHAPE_LEN(shape, len)                                      \
  if (__gab_unlikely(gab_shplen(shape) != len))                                \
    VM_PANIC(GAB_PANIC);

#define MICRO_OP_CALL_BLOCK(blk, have)                                         \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
    KB() = proto_ks(GAB(), p);                                                 \
    FB() = SP() - have;                                                        \
    assert(BLOCK()->header.kind == kGAB_BLOCK);                                \
    assert(BLOCK_PROTO()->header.kind == kGAB_PROTOTYPE);                      \
  })

#define MICRO_OP_LOCALCALL_BLOCK(blk, have)                                    \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET];                                       \
    FB() = SP() - have;                                                        \
    assert(BLOCK()->header.kind == kGAB_BLOCK);                                \
    assert(BLOCK_PROTO()->header.kind == kGAB_PROTOTYPE);                      \
                                                                               \
    SET_HV(have);                                                              \
  })

#define MICRO_OP_TAILCALL_BLOCK(blk, have)                                     \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(p->nslots - have);                                  \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    gmoved(to, from, have);                                                    \
    SP() = to + have;                                                          \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
    KB() = proto_ks(GAB(), p);                                                 \
                                                                               \
    SET_BLOCK(blk);                                                            \
    SET_HV(have);                                                              \
  })

#define MICRO_OP_LOCALTAILCALL_BLOCK(blk, have)                                \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(p->nslots - have);                                  \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    gmoved(to, from, have);                                                    \
    SP() = to + have;                                                          \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET];                                       \
                                                                               \
    SET_BLOCK(blk);                                                            \
    SET_HV(have);                                                              \
  })

#define MICRO_OP_MATCHTAILCALL_BLOCK(idx, have)                                \
  ({                                                                           \
    struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC + idx]);         \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    gmoved(to, from, have);                                                    \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET + idx];                                 \
    SP() = to + have;                                                          \
    FB() = SP() - have;                                                        \
                                                                               \
    SET_BLOCK(b);                                                              \
    SET_HV(have);                                                              \
  })

#define MICRO_OP_MATCHCALL_BLOCK(idx, have)                                    \
  ({                                                                           \
    struct gab_oblock *blk = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC + idx]);       \
                                                                               \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET + idx];                                 \
    FB() = SP() - have;                                                        \
                                                                               \
    SET_HV(have);                                                              \
  })

#define MICRO_OP_CALL_NATIVE(native, have, below_have, message)                \
  ({                                                                           \
    STORE();                                                                   \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    gab_value *returnptr = RETURN_FB();                                        \
                                                                               \
    gab_value *to = SP() - (have + message + FRAME_SIZE);                      \
    gab_assert(to >= FB() - 3,                                                 \
               "EXPECTED DEST TO BE GREATER THAN FRAME BASE. DIST: %li\n",     \
               to - FB());                                                     \
                                                                               \
    gab_value *before = SP();                                                  \
                                                                               \
    uint64_t pass = (have - !message);                                         \
                                                                               \
    union gab_value_pair res =                                                 \
        (*native->function)(GAB(), pass, SP() - pass, REENTRANT());            \
                                                                               \
    RESET_REENTRANT();                                                         \
                                                                               \
    SP() = VM()->sp;                                                           \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (__gab_unlikely(res.status == gab_ctimeout))                            \
      assert(res.bresult != 0), VM_YIELD(res.bresult);                         \
                                                                               \
    RESET_BUMP();                                                              \
                                                                               \
    if (__gab_unlikely(res.status == gab_cvalid))                              \
      return res;                                                              \
                                                                               \
    gab_assert(SP() >= before, "Fewer than zero values returned from native"); \
    uint64_t have = SP() - before;                                             \
                                                                               \
    if (!have)                                                                 \
      PUSH(MICRO_OP_NIL()), have++;                                            \
                                                                               \
    gmoved(to, before, have);                                                  \
    SP() = to + have;                                                          \
                                                                               \
    SET_HV(below_have + have);                                                 \
                                                                               \
    assert(returnptr == RETURN_FB());                                          \
  })

/*
 * These primitives need some sort of control-flow in order
 * to work cleanly with the JIT IR.
 */
#define MICRO_OP_TAKE(channel)                                                 \
  ({                                                                           \
    if (!REENTRANT()) {                                                        \
      SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                    \
      SEND_GUARD_ISCHN(c);                                                     \
    }                                                                          \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    /*                                                                         \
     * Adjust for the tuple-len value at *SP() on the stack.                   \
     * Store above it, and subract one from the stackspace to reserve it.      \
     */                                                                        \
    uint64_t stackspace = GET_STACKSPACE(SP(), SB()) - 1;                      \
                                                                               \
    gab_value v = gab_ntchntake(GAB(), c, stackspace, SP() + 1,                \
                                cGAB_VM_CHANNEL_TAKE_TRIES);                   \
                                                                               \
    RESET_REENTRANT();                                                         \
                                                                               \
    switch (v) {                                                               \
    case gab_ctimeout:                                                         \
      VM_YIELD(gab_ctimeout);                                                  \
    case gab_cinvalid:                                                         \
      VM_TERM();                                                               \
    case gab_cundefined:                                                       \
      DROP_N(have + 1 + FRAME_SIZE);                                           \
      PUSH(gab_none);                                                          \
                                                                               \
      SET_HV(below_have + 1);                                                  \
      NEXT();                                                                  \
    default:                                                                   \
      uint64_t len = gab_valtou(v);                                            \
                                                                               \
      DROP_N(have + 1 + FRAME_SIZE);                                           \
      PUSH(gab_ok);                                                            \
      /*                                                                       \
       * ntchntake returns the number of values *available*, but will only     \
       * write up to *stackspace*.                                             \
       *                                                                       \
       * If there were more available to take than we had room for on the      \
       * stack, return an overflow.                                            \
       * */                                                                    \
      if (__gab_unlikely(len > stackspace))                                    \
        VM_PANIC(GAB_OVERFLOW);                                                \
                                                                               \
      /*                                                                       \
       * We now know that we wrote *len* values to the buffer, because         \
       * it is guaranteed that len <= stackspace                               \
       * */                                                                    \
      gmoved(SP(), SP() + have + 1 + FRAME_SIZE, len);                         \
      SP() += len;                                                             \
                                                                               \
      SET_HV(below_have + len + 1);                                            \
      NEXT();                                                                  \
    }                                                                          \
  })

#define MICRO_OP_PUT(channel)                                                  \
  ({                                                                           \
    if (!REENTRANT()) {                                                        \
      SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                    \
      SEND_GUARD_ISCHN(c);                                                     \
    }                                                                          \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (REENTRANT() == c) {                                                    \
      /* If we're reentering, check that our channel                           \
       is still holding our data ptr.                                          \
       I *believe* this is sound based on the following principles:            \
        - Fibers only ever run on *one* thread, they never migrate.            \
        - Fibers don't share vm's - the address range of one stack             \
          can never overlap with another's. (If stacks become resizeable, this \
          changes)                                                             \
        - Fiber's stacks never resize                                          \
      */                                                                       \
      if (!gab_chnisclosed(c) && gab_chnmatches(c, SP() - (have - 1)))         \
        VM_YIELD(c);                                                           \
                                                                               \
      RESET_REENTRANT();                                                       \
                                                                               \
      /* If not, our put is complete and we can move on */                     \
      DROP_N(have + 1 + FRAME_SIZE);                                           \
                                                                               \
      PUSH(c);                                                                 \
                                                                               \
      SET_HV(below_have + 1);                                                  \
                                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    /* All values *but* the channel are put into the channel. */               \
    gab_value r = gab_untchnput(GAB(), c, have - 1, SP() - (have - 1),         \
                                cGAB_VM_CHANNEL_PUT_TRIES);                    \
                                                                               \
    switch (r) {                                                               \
    case gab_cinvalid:                                                         \
      VM_TERM();                                                               \
    case gab_ctimeout:                                                         \
      /* The put timed-out */                                                  \
      VM_YIELD(gab_ctimeout);                                                  \
    default:                                                                   \
      /* The put succeeded, we must yield until it completes.*/                \
      VM_YIELD(c);                                                             \
    }                                                                          \
  })

#define MICRO_OP_FIBER(block)                                                  \
  ({                                                                           \
    STORE_SP();                                                                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    gab_value fb = REENTRANT();                                                \
                                                                               \
    if (!REENTRANT()) {                                                        \
      fb = gab_fiber(GAB(), (struct gab_fiber_argt){                           \
                                .message = gab_message(GAB(), mGAB_CALL),      \
                                .receiver = block,                             \
                                .flags = GAB().flags,                          \
                            });                                                \
    }                                                                          \
                                                                               \
    bool spawned = gab_wkspawn(GAB(), fb);                                     \
                                                                               \
    if (spawned)                                                               \
      goto fin;                                                                \
                                                                               \
    gab_value result = gab_tchnput(GAB(), EG()->work_channel, fb, 1 << 16);    \
                                                                               \
    switch (result) {                                                          \
    /* Timed out */                                                            \
    case gab_ctimeout:                                                         \
      VM_YIELD(fb);                                                            \
    /* Terminated */                                                           \
    case gab_cinvalid:                                                         \
      VM_TERM();                                                               \
    /* For the successful put & closed case */                                 \
    case gab_cundefined:                                                       \
    case gab_cvalid:                                                           \
    fin:                                                                       \
      RESET_REENTRANT();                                                       \
                                                                               \
      DROP_N(have + 1 + FRAME_SIZE);                                           \
      PUSH(fb);                                                                \
                                                                               \
      SET_HV(below_have + 1);                                                  \
                                                                               \
      NEXT();                                                                  \
    default:                                                                   \
      gab_fprintf(stdout, "UNREACHABLE: $\n", result);                         \
      assert(false && "UNEXPECTED");                                           \
    }                                                                          \
  })

#define MICRO_OP_SEND(have)                                                    \
  ({                                                                           \
    /* Have can not be 0. We need a receiver. */                               \
    if (__gab_unlikely(!have)) {                                               \
      PUSH(MICRO_OP_NIL());                                                    \
      SET_HV(1);                                                               \
      have++;                                                                  \
    }                                                                          \
                                                                               \
    gab_value r = PEEK_N(have);                                                \
    gab_value m = ks[GAB_SEND_KMESSAGE];                                       \
                                                                               \
    if (BLOCK() && try_setup_localmatch(GAB(), m, ks, BLOCK_PROTO())) {        \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_MATCHSEND_BLOCK + adjust);            \
      IP() -= GAB_SEND_CACHE_SIZE;                                             \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    /* Do the expensive lookup */                                              \
    struct gab_impl_rest res = gab_impl(GAB(), m, r);                          \
                                                                               \
    if (__gab_unlikely(!res.status))                                           \
      VM_PANIC3(GAB_SPECIALIZATION_MISSING, m, r, gab_valtype(GAB(), r));      \
                                                                               \
    gab_value spec = res.status == kGAB_IMPL_PROPERTY                          \
                         ? gab_primitive(OP_SEND_PROPERTY)                     \
                         : res.as.spec;                                        \
                                                                               \
    ks[GAB_SEND_KSPECS] = atomic_load(&EG()->messages_epoch);                  \
    ks[GAB_SEND_KTYPE] = gab_valtype(GAB(), r);                                \
    ks[GAB_SEND_KSPEC] = res.as.spec;                                          \
                                                                               \
    switch (gab_valkind(spec)) {                                               \
    case kGAB_PRIMITIVE: {                                                     \
      uint8_t op = gab_valtop(spec);                                           \
                                                                               \
      if (op == OP_SEND_PRIMITIVE_CALL_BLOCK)                                  \
        op += adjust;                                                          \
                                                                               \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, op);                                     \
                                                                               \
      break;                                                                   \
    }                                                                          \
    case kGAB_BLOCK: {                                                         \
      struct gab_oblock *b = GAB_VAL_TO_BLOCK(spec);                           \
      struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);                   \
                                                                               \
      uint8_t local = (GAB_VAL_TO_PROTOTYPE(b->p)->src == BLOCK_PROTO()->src); \
      adjust |= (local << 1);                                                  \
                                                                               \
      if (local) {                                                             \
        ks[GAB_SEND_KOFFSET] = (intptr_t)proto_ip(GAB(), p);                   \
      }                                                                        \
                                                                               \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_BLOCK + adjust);                 \
                                                                               \
      break;                                                                   \
    }                                                                          \
    case kGAB_NATIVE: {                                                        \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_NATIVE);                         \
      break;                                                                   \
    }                                                                          \
    default:                                                                   \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_CONSTANT);                       \
      break;                                                                   \
    }                                                                          \
                                                                               \
    IP() -= GAB_SEND_CACHE_SIZE;                                               \
                                                                               \
    NEXT();                                                                    \
  })

#define MICRO_OP_TRIM(want, have)                                              \
  ({                                                                           \
    uint64_t nulls = 0;                                                        \
                                                                               \
    if (have == want && want < 10) {                                           \
      WRITE_BYTE(2, OP_TRIM_EXACTLY0 + want);                                  \
      IP() -= 2;                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    if (have > want && have - want < 10) {                                     \
      WRITE_BYTE(2, OP_TRIM_DOWN1 - 1 + (have - want));                        \
      IP() -= 2;                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    if (want > have && want - have < 10) {                                     \
      WRITE_BYTE(2, OP_TRIM_UP1 - 1 + (want - have));                          \
      IP() -= 2;                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    SP() -= have;                                                              \
                                                                               \
    if (__gab_unlikely(have != want && want != VAR_EXP)) {                     \
      if (have > want) {                                                       \
        have = want;                                                           \
      } else {                                                                 \
        nulls = want - have;                                                   \
      }                                                                        \
    }                                                                          \
                                                                               \
    SP() += have + nulls;                                                      \
                                                                               \
    while (nulls--)                                                            \
      PEEK_N(nulls + 1) = gab_nil;                                             \
                                                                               \
    SET_HV(want);                                                              \
  })

#define MICRO_OP_USE(have)                                                     \
  ({                                                                           \
    CHECK_SIGNAL();                                                            \
                                                                               \
    STORE();                                                                   \
    uintptr_t reentrant = REENTRANT();                                         \
    union gab_value_pair mod;                                                  \
                                                                               \
    if (reentrant) {                                                           \
      assert(gab_valisfib(reentrant));                                         \
                                                                               \
      mod = gab_tfibawait(GAB(), reentrant, 0);                                \
                                                                               \
      RESET_REENTRANT();                                                       \
    } else {                                                                   \
      /*                                                                       \
       * TODO @cgab @api: Really fix this, Its rough in a lot of ways chief.   \
       * This is a better way of pulling arguments, off the fiber itself.      \
       * I don't see why *instead* of this I couldn't just store an            \
       * environment on the fiber that I can forward instead.                  \
       */                                                                      \
      gab_value shp = gab_prtshp(BLOCK()->p);                                  \
                                                                               \
      gab_value rec = gab_record(GAB(), 1, FIBER()->len - 2, gab_shpdata(shp), \
                                 FIBER()->data + 2);                           \
                                                                               \
      bool should_reload = have > 1 ? PEEK_N(have - 1) == gab_true : false;    \
                                                                               \
      gab_value module = have > 1 ? PEEK_N(have - 1) : 0;                      \
                                                                               \
      mod = gab_use(GAB(), (struct gab_use_argt){                              \
                               .flags = should_reload ? fGAB_USE_RELOAD : 0,   \
                               .vpackage_name = r,                             \
                               .vmodule_name = module,                         \
                               .env = rec,                                     \
                           });                                                 \
    }                                                                          \
                                                                               \
    if (mod.status == gab_ctimeout) {                                          \
      assert(gab_valisfib(mod.vresult));                                       \
      VM_YIELD(mod.vresult);                                                   \
    }                                                                          \
                                                                               \
    if (mod.status != gab_cvalid)                                              \
      VM_GIVEN(mod);                                                           \
                                                                               \
    if (mod.aresult->data[0] != gab_ok)                                        \
      VM_GIVEN(mod);                                                           \
                                                                               \
    DROP_N(have + 1 + FRAME_SIZE);                                             \
                                                                               \
    for (uint64_t i = 1; i < mod.aresult->len; i++)                            \
      PUSH(mod.aresult->data[i]);                                              \
                                                                               \
    SET_HV(below_have + mod.aresult->len - 1);                                 \
  })

#define MISS_CACHED_SEND(reason)                                               \
  ({                                                                           \
    IP() -= GAB_SEND_CACHE_SIZE - 1;                                           \
    [[clang::musttail]] return OP_SEND_HANDLER(DISPATCH_ARGS());               \
  })

#define MISS_CACHED_TRIM(reason)                                               \
  ({                                                                           \
    IP()--;                                                                    \
    [[clang::musttail]] return OP_TRIM_HANDLER(DISPATCH_ARGS());               \
  })

#define MISS_CACHED_RETURN(reason)                                             \
  ({ [[clang::musttail]] return OP_RETURN_HANDLER(DISPATCH_ARGS()); })

#define VM_YIELD(value)                                                        \
  ({                                                                           \
    IP() -= GAB_SEND_CACHE_SIZE;                                               \
    STORE();                                                                   \
    return vm_yield(GAB(), value);                                             \
  })

#define VM_TERM()                                                              \
  ({                                                                           \
    STORE();                                                                   \
    return vm_terminate(GAB(), "While executing $\n", gab_thisfiber(GAB()));   \
  })

#define STORE_SP() (VM()->sp = SP())
#define STORE_FP() (VM()->fp = FB())
#define STORE_IP() (VM()->ip = IP())
#define STORE_KB() (VM()->kb = KB())

#define STORE()                                                                \
  ({                                                                           \
    STORE_SP();                                                                \
    STORE_FP();                                                                \
    STORE_IP();                                                                \
    STORE_KB();                                                                \
  })

#define PANIC_GUARD_KIND(value, kind)                                          \
  if (__gab_unlikely(gab_valkind(value) != kind)) {                            \
    STORE_MICRO_OP_VM_PANIC_FRAME(1);                                          \
    VM_PANIC5(GAB_TYPE_MISMATCH, ks[GAB_SEND_KMESSAGE], ks[GAB_SEND_KSPEC],    \
              value, gab_valtype(GAB(), value), gab_type(GAB(), kind));        \
  }

#define PANIC_GUARD_ISB(value)                                                 \
  if (__gab_unlikely(!__gab_valisb(value))) {                                  \
    STORE_MICRO_OP_VM_PANIC_FRAME(have);                                       \
    VM_PANIC5(GAB_TYPE_MISMATCH, ks[GAB_SEND_KMESSAGE], ks[GAB_SEND_KSPEC],    \
              value, gab_valtype(GAB(), value),                                \
              gab_type(GAB(), kGAB_MESSAGE));                                  \
  }

#define PANIC_GUARD_ISN(value)                                                 \
  if (__gab_unlikely(!__gab_valisn(value))) {                                  \
    STORE_MICRO_OP_VM_PANIC_FRAME(have);                                       \
    VM_PANIC5(GAB_TYPE_MISMATCH, ks[GAB_SEND_KMESSAGE], ks[GAB_SEND_KSPEC],    \
              value, gab_valtype(GAB(), value), gab_type(GAB(), kGAB_NUMBER)); \
  }

#define PANIC_GUARD_ISS(value) PANIC_GUARD_KIND(value, kGAB_STRING)

#define SEND_GUARD_ISS(value) SEND_GUARD_KIND(value, kGAB_STRING)

#define SEND_GUARD(clause, reason)                                             \
  if (__gab_unlikely(!(clause)))                                               \
    MISS_CACHED_SEND(reason);

#define SEND_GUARD_KIND(r, k) SEND_GUARD(gab_valkind(r) == k, "Unexpected kind")

#define SEND_GUARD_ISN(value) SEND_GUARD(__gab_valisn(value), "Not number")
#define SEND_GUARD_ISB(value) SEND_GUARD(__gab_valisb(value), "Not number")

/*
 * SEND guard which checks that the
 * world is as we expect, and the receiver is a channel.
 * */
#define SEND_GUARD_ISCHN(r)                                                    \
  SEND_GUARD(gab_valkind(r) >= kGAB_CHANNEL &&                                 \
                 gab_valkind(r) <= kGAB_CHANNELCLOSED,                         \
             "Not Channel")

/*
 * SEND guard which checks that the world
 * is as we expect, and the receiver is a record.
 */
#define SEND_GUARD_ISREC(r) SEND_GUARD_KIND(r, kGAB_RECORD)

/*
 * SEND guard which checks that the world
 * is as we expect, and the receiver is a shape.
 */
#define SEND_GUARD_ISSHP(r)                                                    \
  SEND_GUARD(gab_valkind(r) == kGAB_SHAPE || gab_valkind(r) == kGAB_SHAPELIST, \
             "Not shape")

/*
 * SEND guard which compares the message record checked against last time
 * to the current rec.
 *
 * IS IT POSSIBLE THAT THE MESSAGE SPECS are *replaced* by another at the same
 * address after a collection happens, and then some sends *think* they have it
 * cached but they havent?
 */
#define SEND_GUARD_CACHED_MESSAGE_SPECS(epoch)                                 \
  SEND_GUARD(gab_valeq(atomic_load(&EG()->messages_epoch), epoch),             \
             "Global message change detected.")

#define SEND_GUARD_TYPE(r, type)                                               \
  SEND_GUARD(gab_valisa(GAB(), r, type), "Type failed")

/*
 * SEND guard which checks that the world is
 * as we expect, and that the receiver type is the
 * same as seen last time.
 */
#define SEND_GUARD_CACHED_RECEIVER_TYPE(r)                                     \
  SEND_GUARD_TYPE(r, ks[GAB_SEND_KTYPE])

#define SEND_GUARD_CACHED_MATCH_TYPE(r, ks)                                    \
  ({                                                                           \
    int64_t idx = MATCH_HASHT(gab_valtype(GAB(), r));                          \
    SEND_GUARD_TYPE(r, ks[GAB_SEND_KTYPE + idx]);                              \
  })

#define TRIM_GUARD(clause, reason)                                             \
  if (__gab_unlikely(!(clause)))                                               \
    MISS_CACHED_TRIM(reason);

#define TRIM_GUARD_EXACTLY_N(want, n)                                          \
  TRIM_GUARD(HV() == want, "Mismatched tuple length")

#define TRIM_GUARD_UP_N(want, n)                                               \
  TRIM_GUARD((HV() + n) == want, "Mismatched tuple length")

#define TRIM_GUARD_DOWN_N(want, n)                                             \
  TRIM_GUARD((HV() - n) == want, "Mismatched tuple length")

#define RETURN_GUARD(clause, reason)                                           \
  if (__gab_unlikely(!(clause)))                                               \
    MISS_CACHED_RETURN(reason);

#define RETURN_GUARD_EXACTLY_N(n)                                              \
  RETURN_GUARD(HV() == n, "Mismatched return tuple length")

#define SHORTCUT_GUARD_ARGS_LT(n)                                              \
  ({                                                                           \
    if (__gab_unlikely(have < n))                                              \
      SET_HV(below_have + 1), NEXT();                                          \
  })

#define NILPAD_GUARD_ARGS_GTE(n)                                               \
  ({                                                                           \
    if (__gab_unlikely(have < n))                                              \
      PUSH(MICRO_OP_NIL()), have++;                                            \
  })

// If the LSB is 1, the number is not divisible by 2.
#define MICRO_OP_RECORD(len)                                                   \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_record(GAB(), 2, len / 2, SP() - len, SP() + 1 - len);                 \
  })

#define MICRO_OP_RECORDFROM(shape, len)                                        \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_recordfrom(GAB(), shape, 1, len, SP() - len, nullptr);                 \
  })

#define MICRO_OP_SHAPE(len)                                                    \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_shape(GAB(), 1, len, SP() - len, nullptr);                             \
  })

#define MICRO_OP_LIST(n, len)                                                  \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_list(GAB(), 1, (len), SP() - ((n) + (len)));                           \
  })

#define MICRO_OP_CHANNEL()                                                     \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_channel(GAB());                                                        \
  })

#define MICRO_OP_PACK_LIST(below, above)                                       \
  ({                                                                           \
    uint64_t have = HV();                                                      \
                                                                               \
    uint64_t want = below + above;                                             \
                                                                               \
    while (have < want)                                                        \
      PUSH(MICRO_OP_NIL()), have++;                                            \
                                                                               \
    assert(have >= want);                                                      \
    int64_t len = have - want;                                                 \
                                                                               \
    gab_value *ap = SP() - above;                                              \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    gab_value rec = gab_list(GAB(), 1, len, ap - len);                         \
                                                                               \
    DROP_N(len - 1);                                                           \
                                                                               \
    if (len)                                                                   \
      gmoved(ap - len + 1, ap, above);                                         \
    else                                                                       \
      gmovea(ap + 1, ap, above);                                               \
                                                                               \
    PEEK_N(above + 1) = rec;                                                   \
                                                                               \
    SET_HV(want + 1);                                                          \
  })

#define MICRO_OP_PACK_DICT(below, above)                                       \
  ({                                                                           \
    uint64_t have = HV();                                                      \
                                                                               \
    uint64_t want = below + above;                                             \
                                                                               \
    while (have < want)                                                        \
      PUSH(MICRO_OP_NIL()), have++;                                            \
                                                                               \
    assert(have >= want);                                                      \
    int64_t len = have - want;                                                 \
                                                                               \
    gab_value *ap = SP() - above;                                              \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    gab_value rec = gab_record(GAB(), 2, len / 2, ap - len, ap - len + 1);     \
                                                                               \
    DROP_N(len - 1);                                                           \
                                                                               \
    if (len)                                                                   \
      gmoved(ap - len + 1, ap, above);                                         \
    else                                                                       \
      gmovea(ap + 1, ap, above);                                               \
                                                                               \
    PEEK_N(above + 1) = rec;                                                   \
                                                                               \
    SET_HV(want + 1);                                                          \
  })

#define MICRO_OP_BLOCK(p)                                                      \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_value blk = gab_block(GAB(), p);                                       \
                                                                               \
    struct gab_oblock *b = GAB_VAL_TO_BLOCK(blk);                              \
    struct gab_oprototype *proto = GAB_VAL_TO_PROTOTYPE(p);                    \
                                                                               \
    for (int i = 0; i < proto->nupvalues; i++) {                               \
      uint8_t is_local = proto->data[i] & fLOCAL_LOCAL;                        \
      uint8_t index = proto->data[i] >> 1;                                     \
                                                                               \
      if (is_local)                                                            \
        b->upvalues[i] = LOCAL(index);                                         \
      else                                                                     \
        b->upvalues[i] = UPVALUE(index);                                       \
    }                                                                          \
                                                                               \
    blk;                                                                       \
  })

#define MICRO_OP_TYPE(v) (gab_valtype(GAB(), v))

#define PUSHTUPLE(n)                                                           \
  ({                                                                           \
    SP() += 2;                                                                 \
    assert(SP()[-1] = FRAME_IP);                                               \
    assert(SP()[-2] = FRAME_BK);                                               \
    PUSH(n);                                                                   \
  })

#define MICRO_OP_RETURN(have)                                                  \
  ({                                                                           \
    uint64_t below_have = RETURN_HAVE();                                       \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB() - (FRAME_SIZE + 1);                                   \
                                                                               \
    if (__gab_unlikely(RETURN_FB_DELTA() == 0)) {                              \
      STORE();                                                                 \
                                                                               \
      gmoved(to, from, have);                                                  \
      SP() = to + have;                                                        \
      SET_HV(have + below_have);                                               \
                                                                               \
      [[clang::musttail]] return vm_ok(DISPATCH_ARGS());                       \
    }                                                                          \
                                                                               \
    assert(RETURN_IP() != nullptr);                                            \
                                                                               \
    LOAD_FRAME();                                                              \
                                                                               \
    gmoved(to, from, have);                                                    \
    SP() = to + have;                                                          \
    SET_HV(have + below_have);                                                 \
                                                                               \
    assert(FB() >= VM()->sb + FRAME_SIZE);                                     \
    assert(BLOCK()->header.kind == kGAB_BLOCK);                                \
    assert(BLOCK_PROTO()->header.kind == kGAB_PROTOTYPE);                      \
  })

#define MICRO_OP_UVRECAT(r, i) (gab_uvrecat(r, i))

#define MICRO_OP_UKRECAT(r, i) (gab_ukrecat(r, i))

#define MICRO_OP_SPLATSHAPE(s)                                                 \
  ({                                                                           \
    uint64_t len = gab_shplen(s);                                              \
                                                                               \
    for (uint64_t i = 0; i < len; i++)                                         \
      PUSH(gab_ushpat(s, i));                                                  \
                                                                               \
    len;                                                                       \
  })

#define MICRO_OP_CONS_RECORD(r, arg) (gab_lstpush(GAB(), r, arg))

#define MICRO_OP_CONS(a, b) (gab_listof(GAB(), a, b))

#define MICRO_OP_SENDK() (ks[GAB_SEND_KSPEC])

#define MICRO_OP_NIL() (gab_nil)

#define MICRO_OP_SPILL(r, n) (r)

#define MICRO_OP_BINARY_ADD(a, b) (a + b)
#define MICRO_OP_BINARY_SUB(a, b) (a - b)
#define MICRO_OP_BINARY_MUL(a, b) (a * b)
#define MICRO_OP_BINARY_DIV(a, b) (a / b)
#define MICRO_OP_BINARY_LT(a, b) (a < b)
#define MICRO_OP_BINARY_LTE(a, b) (a <= b)
#define MICRO_OP_BINARY_GT(a, b) (a > b)
#define MICRO_OP_BINARY_GTE(a, b) (a >= b)
#define MICRO_OP_BINARY_BOR(a, b) (a | b)
#define MICRO_OP_BINARY_BND(a, b) (a & b)
#define MICRO_OP_BINARY_MOD(a, b)                                              \
  (__gab_unlikely(b == 0) ? (0.0 / 0.0) : (a % b))

#define BINARY_SHIFT(a, b, op, op_op)                                          \
  ({                                                                           \
    gab_int result = ((__gab_unlikely(b >= GAB_INTWIDTH)) ? (0)                \
                      : (__gab_unlikely(b < 0)) ? (a op_op(uint32_t)(-b))      \
                                                : (a op(uint32_t) b));         \
    result;                                                                    \
  })

#define MICRO_OP_BINARY_LSH(a, b) BINARY_SHIFT(a, b, <<, >>)
#define MICRO_OP_BINARY_RSH(a, b) BINARY_SHIFT(a, b, >>, <<)

#define MICRO_OP_BINARY_EQ(a, b) (gab_valeq(a, b))

#define MICRO_OP_BINARY_CONCAT(a, b)                                           \
  ({                                                                           \
    gab_value val_ab = gab_tstrcat(GAB(), a, b);                               \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (val_ab == gab_cinvalid)                                                \
      VM_TERM();                                                               \
                                                                               \
    if (val_ab == gab_ctimeout)                                                \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    assert(gab_valkind(val_ab) == kGAB_STRING);                                \
                                                                               \
    val_ab;                                                                    \
  })

#define MICRO_OP_BINARY_STRLT(a, b) (strcoll(a, b) < 0)
#define MICRO_OP_BINARY_STRLTE(a, b) (strcoll(a, b) <= 0)
#define MICRO_OP_BINARY_STRGT(a, b) (strcoll(a, b) > 0)
#define MICRO_OP_BINARY_STRGTE(a, b) (strcoll(a, b) >= 0)

#define MICRO_OP_UNARY_BIN(a) (~a)
#define MICRO_OP_UNARY_LIN(a) (!a)

#define MICRO_OP_BOXN(dbl) (gab_number(dbl))
#define MICRO_OP_BOXI(i) (gab_safeinteger(i))
#define MICRO_OP_BOXB(t_or_f) (gab_bool(t_or_f))
#define MICRO_OP_BOXV(v) (v)

#define MICRO_OP_UNBOXF(v) (gab_valtof(v))
#define MICRO_OP_UNBOXI(v) (({ gab_valtoi(v); }))
#define MICRO_OP_UNBOXU(v) (({ gab_valtou(v); }))
#define MICRO_OP_UNBOXB(v) (gab_valintob(v))
#define MICRO_OP_UNBOXS(v) (gab_strdata(&v))
#define MICRO_OP_UNBOXV(v) (v)

#define MICRO_OP_UNBOXF2(v) (MICRO_OP_UNBOXF(v))
#define MICRO_OP_UNBOXI2(v) (MICRO_OP_UNBOXI(v))
#define MICRO_OP_UNBOXU2(v) (MICRO_OP_UNBOXU(v))
#define MICRO_OP_UNBOXB2(v) (MICRO_OP_UNBOXB(v))
#define MICRO_OP_UNBOXS2(v) (MICRO_OP_UNBOXS(v))
#define MICRO_OP_UNBOXV2(v) (MICRO_OP_UNBOXV(v))

#define MICRO_OP_UNBOXF_T gab_float
#define MICRO_OP_UNBOXU_T gab_uint
#define MICRO_OP_UNBOXI_T gab_int
#define MICRO_OP_UNBOXB_T bool
#define MICRO_OP_UNBOXS_T const char *
#define MICRO_OP_UNBOXV_T gab_value

#define SEND_GUARD_NOP(v) SEND_GUARD_CACHED_RECEIVER_TYPE(v)
#define PANIC_GUARD_NOP(v)

#define IMPL_SEND_UNARY(CODE, guard, boxer, operation_type, unboxer,           \
                        primitive)                                             \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_SENDCONSTANTS;                                        \
    uint64_t have = HV();                                                      \
    uint64_t below_have = BELOW_HV();                                          \
                                                                               \
    SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                      \
                                                                               \
    gab_value r = PEEK_N(have);                                                \
                                                                               \
    SEND_GUARD_##guard(r);                                                     \
                                                                               \
    operation_type val = unboxer(r);                                           \
                                                                               \
    DROP_N(have + 1 + FRAME_SIZE);                                             \
                                                                               \
    PUSH(boxer(primitive(val)));                                               \
                                                                               \
    SET_HV(below_have + 1);                                                    \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_SEND_BINARY(CODE, guard, a_type, a_unboxer, b_type, b_unboxer,    \
                         c_type, c_boxer, primitive)                           \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_SENDCONSTANTS;                                        \
    uint64_t have = HV();                                                      \
    uint64_t below_have = BELOW_HV();                                          \
                                                                               \
    SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                      \
                                                                               \
    NILPAD_GUARD_ARGS_GTE(2);                                                  \
                                                                               \
    gab_value b = PEEK_N(have - 1);                                            \
    gab_value a = PEEK_N(have);                                                \
                                                                               \
    SEND_GUARD_##guard(a);                                                     \
    PANIC_GUARD_##guard(b);                                                    \
                                                                               \
    a_type val_a = a_unboxer(a);                                               \
    b_type val_b = b_unboxer##2(b);                                            \
                                                                               \
    c_type val_c = primitive(val_a, val_b);                                    \
                                                                               \
    gab_value c = c_boxer(val_c);                                              \
                                                                               \
    DROP_N(have + 1 + FRAME_SIZE);                                             \
                                                                               \
    PUSH(c);                                                                   \
                                                                               \
    SET_HV(below_have + 1);                                                    \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_RETURN_N(n)                                                       \
  CASE_CODE(RETURN_##n) {                                                      \
    RETURN_GUARD_EXACTLY_N(n);                                                 \
                                                                               \
    MICRO_OP_RETURN(n);                                                        \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_TRIM_N(n)                                                         \
  CASE_CODE(TRIM_DOWN##n) {                                                    \
    uint8_t want = READ_BYTE;                                                  \
                                                                               \
    TRIM_GUARD_DOWN_N(want, n);                                                \
                                                                               \
    DROP_N(n);                                                                 \
                                                                               \
    SET_HV(want);                                                              \
                                                                               \
    NEXT();                                                                    \
  }                                                                            \
                                                                               \
  CASE_CODE(TRIM_EXACTLY##n) {                                                 \
    uint8_t want = READ_BYTE;                                                  \
                                                                               \
    TRIM_GUARD_EXACTLY_N(want, n);                                             \
                                                                               \
    NEXT();                                                                    \
  }                                                                            \
                                                                               \
  CASE_CODE(TRIM_UP##n) {                                                      \
    uint8_t want = READ_BYTE;                                                  \
                                                                               \
    TRIM_GUARD_UP_N(want, n);                                                  \
                                                                               \
    for (int i = 0; i < n; i++)                                                \
      PUSH(MICRO_OP_NIL());                                                    \
                                                                               \
    SET_HV(want);                                                              \
                                                                               \
    NEXT();                                                                    \
  }

// TODO @cgab @vm @perf: Handle undefined and record case
CASE_CODE(MATCHTAILSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = HV();

  assert(istail);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  gab_value r = PEEK_N(have);

  // TODO @cgab @vm @perf: Handle undefined and record case
  uint8_t idx = MATCH_HASHT(gab_valtype(GAB(), r));
  SEND_GUARD_TYPE(r, ks[GAB_SEND_KTYPE + idx]);

  MICRO_OP_MATCHTAILCALL_BLOCK(idx, have);

  NEXT();
}

CASE_CODE(MATCHSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = HV();

  assert(!istail);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  gab_value r = PEEK_N(have);

  uint8_t idx = MATCH_HASHT(gab_valtype(GAB(), r));
  SEND_GUARD_TYPE(r, ks[GAB_SEND_KTYPE + idx]);

  MICRO_OP_MATCHCALL_BLOCK(idx, have);

  NEXT();
}

CASE_CODE(LOAD_UPVALUE) {
  uint64_t have = HV();

  PUSH(UPVALUE(READ_BYTE));

  SET_HV(have + 1);

  NEXT();
}

CASE_CODE(NLOAD_UPVALUE) {
  uint8_t n = READ_BYTE;

  PANIC_GUARD_STACKSPACE(n);

  uint64_t have = HV();

  SP()[n] = have + n;

  while (n--)
    PUSH(UPVALUE(READ_BYTE));

  NEXT();
}

CASE_CODE(LOAD_LOCAL) {
  uint64_t have = HV();

  PUSH(LOCAL(READ_BYTE));

  SET_HV(have + 1);

  NEXT();
}

CASE_CODE(NLOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  PANIC_GUARD_STACKSPACE(n);

  uint64_t have = HV();
  uint64_t len = have + n;

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_HV(len);

  NEXT();
}

CASE_CODE(STORE_LOCAL) {
  STORE_LOCAL(READ_BYTE, PEEK());
  NEXT();
}

CASE_CODE(POPSTORE_LOCAL) {
  uint64_t have = HV();

  STORE_LOCAL(READ_BYTE, POP());

  assert(have >= 1);
  SET_HV(have - 1);
  NEXT();
}

CASE_CODE(NPOPSTORE_LOCAL) {
  uint64_t have = HV();

  uint8_t n = READ_BYTE;

  assert(have >= n);
  have -= n;

  while (n--)
    STORE_LOCAL(READ_BYTE, POP());

  SET_HV(have);
  NEXT();
}

CASE_CODE(NPOPSTORE_STORE_LOCAL) {
  uint64_t have = HV();

  uint8_t n = READ_BYTE;

  assert(have >= n);
  have -= n;

  while (n-- > 1)
    STORE_LOCAL(READ_BYTE, POP());

  STORE_LOCAL(READ_BYTE, PEEK());

  SET_HV(have + 1);
  NEXT();
}

CASE_CODE(SEND_NATIVE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_onative *n = GAB_VAL_TO_NATIVE(ks[GAB_SEND_KSPEC]);

  MICRO_OP_CALL_NATIVE(n, have, below_have, true);

  NEXT();
}

CASE_CODE(SEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = HV();
  assert(!istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  MICRO_OP_CALL_BLOCK(b, have);

  NEXT();
}

/*
 * TODO @vm @perf: Specializer tailsends for HV().
 * Maybe specialize each of these even further based on the HV amount -
 * this would allow the gmoved in TAILCALL to be even further optimized
 * by the compiler, as the have argument would be compile-time.
 */
CASE_CODE(TAILSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = HV();
  assert(istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  MICRO_OP_TAILCALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(LOCALSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = HV();

  assert(!istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  MICRO_OP_LOCALCALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(LOCALTAILSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = HV();

  assert(istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  MICRO_OP_LOCALTAILCALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CALL_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = HV();

  assert(!istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(r);

  MICRO_OP_CALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(TAILSEND_PRIMITIVE_CALL_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = HV();

  assert(istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_BLOCK);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(r);

  MICRO_OP_TAILCALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CALL_NATIVE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  // Sanity check
  assert(have > 0 && have < 32);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  PANIC_GUARD_KIND(r, kGAB_NATIVE);

  struct gab_onative *n = GAB_VAL_TO_NATIVE(r);

  MICRO_OP_CALL_NATIVE(n, have, below_have, false);

  NEXT();
}

// float + float = float
IMPL_SEND_BINARY(PRIMITIVE_ADD, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXF_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_ADD);

// float - float = float
IMPL_SEND_BINARY(PRIMITIVE_SUB, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXF_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_SUB);

// float * float = float
IMPL_SEND_BINARY(PRIMITIVE_MUL, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXF_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_MUL);

// float / float = float
IMPL_SEND_BINARY(PRIMITIVE_DIV, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXF_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_DIV);

// int % int = int
IMPL_SEND_BINARY(PRIMITIVE_MOD, ISN, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_MOD);

// float < float = bool
IMPL_SEND_BINARY(PRIMITIVE_LT, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_LT);

// float <= float = bool
IMPL_SEND_BINARY(PRIMITIVE_LTE, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_LTE);

// float >= float = bool
IMPL_SEND_BINARY(PRIMITIVE_GT, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_GT);

// float >= float = bool
IMPL_SEND_BINARY(PRIMITIVE_GTE, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_GTE);

// int | int = int
IMPL_SEND_BINARY(PRIMITIVE_BOR, ISN, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_BOR);

// int & int = int
IMPL_SEND_BINARY(PRIMITIVE_BND, ISN, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_BND);

// Implemented logical and/or for booleans with a binary &/| operation.
// bool | bool = bool
IMPL_SEND_BINARY(PRIMITIVE_LOR, ISB, MICRO_OP_UNBOXB_T, MICRO_OP_UNBOXB,
                 MICRO_OP_UNBOXB_T, MICRO_OP_UNBOXB, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_BOR);

// bool & bool = bool
IMPL_SEND_BINARY(PRIMITIVE_LND, ISB, MICRO_OP_UNBOXB_T, MICRO_OP_UNBOXB,
                 MICRO_OP_UNBOXB_T, MICRO_OP_UNBOXB, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_BND);

// str < str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_LT, ISS, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS,
                 MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_STRLT);

// str <= str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_LTE, ISS, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS,
                 MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_STRLTE);

// str > str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_GT, ISS, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS,
                 MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_STRGT);

// str >= str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_GTE, ISS, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS,
                 MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_STRGTE);
// uint << int = uint
IMPL_SEND_BINARY(PRIMITIVE_LSH, ISN, MICRO_OP_UNBOXU_T, MICRO_OP_UNBOXU,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_LSH);

// uint >> int = uint
IMPL_SEND_BINARY(PRIMITIVE_RSH, ISN, MICRO_OP_UNBOXU_T, MICRO_OP_UNBOXU,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_RSH);

// str + str = str
IMPL_SEND_BINARY(PRIMITIVE_CONCAT, ISS, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV,
                 MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV, MICRO_OP_UNBOXV_T,
                 MICRO_OP_BOXV, MICRO_OP_BINARY_CONCAT);

// val == val = bool
IMPL_SEND_BINARY(PRIMITIVE_EQ, NOP, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV,
                 MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_EQ);

// !bool = bool
IMPL_SEND_UNARY(PRIMITIVE_LIN, ISB, MICRO_OP_BOXB, MICRO_OP_UNBOXB_T,
                MICRO_OP_UNBOXB, MICRO_OP_UNARY_LIN);

// ~int = int
IMPL_SEND_UNARY(PRIMITIVE_BIN, ISN, MICRO_OP_BOXN, MICRO_OP_UNBOXI_T,
                MICRO_OP_UNBOXI, MICRO_OP_UNARY_BIN);

// val? = val
IMPL_SEND_UNARY(PRIMITIVE_TYPE, NOP, MICRO_OP_BOXV, MICRO_OP_UNBOXV_T,
                MICRO_OP_UNBOXV, MICRO_OP_TYPE);

CASE_CODE(SEND_PRIMITIVE_USE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SEND_GUARD_KIND(r, kGAB_STRING);

  MICRO_OP_USE(have);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SHORTCUT_GUARD_ARGS_LT(2);

  gab_value a = PEEK_N(have);

  gab_value b = PEEK_N(have - 1);

  STORE_SP();

  gab_value res = MICRO_OP_CONS(a, b);

  DROP_N(have + 1 + FRAME_SIZE);

  PUSH(res);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS_RECORD) {
  gab_value *ks = READ_SENDCONSTANTS; // Constants
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  SHORTCUT_GUARD_ARGS_LT(2);

  STORE_SP();

  gab_value arg = PEEK_N(have - 1);

  gab_value res = MICRO_OP_CONS_RECORD(r, arg);

  DROP_N(have + 1 + FRAME_SIZE);

  PUSH(res);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATSHAPE) {
  gab_value *ks = READ_SENDCONSTANTS; // Constants
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value s = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_ISSHP(s);

  DROP_N(have + 1 + FRAME_SIZE);

  PANIC_GUARD_STACKSPACE_SPLATSHAPE(s);

  uint64_t len = MICRO_OP_SPLATSHAPE(s);

  SET_HV(below_have + len);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATLIST) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  uint64_t n = gab_shplen(ks[GAB_SEND_KTYPE]);

  r = MICRO_OP_SPILL(r, n - (have + 1));

  DROP_N(have + 1 + FRAME_SIZE);

  PANIC_GUARD_STACKSPACE(n);

  for (uint64_t i = 0; i < n; i++)
    PUSH(MICRO_OP_UVRECAT(r, i));

  SET_HV(below_have + n);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATDICT) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  uint64_t n = gab_shplen(ks[GAB_SEND_KTYPE]);

  DROP_N(have + 1 + FRAME_SIZE);

  PANIC_GUARD_STACKSPACE(n * 2);

  for (uint64_t i = 0; i < n; i++)
    PUSH(MICRO_OP_UKRECAT(r, i)), PUSH(MICRO_OP_UVRECAT(r, i));

  SET_HV(below_have + n);

  NEXT();
}

CASE_CODE(SEND_CONSTANT) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  gab_value spec = MICRO_OP_SENDK();

  DROP_N(have + 1 + FRAME_SIZE);

  PUSH(spec);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PROPERTY) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  r = MICRO_OP_SPILL(r, 0);

  DROP_N(have + 1 + FRAME_SIZE);

  PUSH(MICRO_OP_UVRECAT(r, ks[GAB_SEND_KSPEC]));

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(NOP) { NEXT(); }

CASE_CODE(CONSTANT) {
  uint64_t have = HV();

  PUSH(READ_CONSTANT);

  SET_HV(have + 1);

  NEXT();
}

CASE_CODE(NCONSTANT) {
  uint8_t n = READ_BYTE;

  PANIC_GUARD_STACKSPACE(n);

  uint64_t have = HV() + n;

  while (n--)
    PUSH(READ_CONSTANT);

  SET_HV(have);

  NEXT();
}

CASE_CODE(POP) {
  uint64_t have = HV();

  DROP();

  SET_HV(have - 1);
  NEXT();
}

CASE_CODE(POP_N) {
  uint64_t have = HV();

  uint8_t n = READ_BYTE;
  DROP_N(n);

  SET_HV(have - n);
  NEXT();
}

CASE_CODE(BLOCK) {
  gab_value p = READ_CONSTANT;
  uint64_t have = HV();

  gab_value blk = MICRO_OP_BLOCK(p);

  PUSH(blk);

  SET_HV(have + 1);

  NEXT();
}

CASE_CODE(TUPLE) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  SET_HV(0);

  NEXT();
}

CASE_CODE(NTUPLE) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = HV();
    PUSHTUPLE(have);
    SET_HV(0);
  }

  NEXT();
}

CASE_CODE(TUPLE_CONSTANT) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  PUSH(READ_CONSTANT);

  SET_HV(1);

  NEXT();
}

CASE_CODE(TUPLE_NCONSTANT) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  uint8_t n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(READ_CONSTANT);

  SET_HV(have);

  NEXT();
}

CASE_CODE(TUPLE_LOAD_LOCAL) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  PUSH(LOCAL(READ_BYTE));

  SET_HV(1);

  NEXT();
}

CASE_CODE(TUPLE_NLOAD_LOCAL) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  uint8_t n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_HV(have);

  NEXT();
}

CASE_CODE(NTUPLE_LOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  uint64_t have = HV();
  PUSHTUPLE(have);

  while (--n)
    PUSHTUPLE(0);

  PUSH(LOCAL(READ_BYTE));

  SET_HV(1);

  NEXT();
}

CASE_CODE(NTUPLE_NLOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  uint64_t have = HV();
  PUSHTUPLE(have);

  while (--n)
    PUSHTUPLE(0);

  n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_HV(have);

  NEXT();
}

CASE_CODE(NTUPLE_CONSTANT) {
  uint8_t n = READ_BYTE;

  uint64_t have = HV();
  PUSHTUPLE(have);

  while (--n)
    PUSHTUPLE(0);

  PUSH(READ_CONSTANT);

  SET_HV(1);

  NEXT();
}

CASE_CODE(NTUPLE_NCONSTANT) {
  uint8_t n = READ_BYTE;

  uint64_t have = HV();
  PUSHTUPLE(have);

  while (--n)
    PUSHTUPLE(0);

  n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(READ_CONSTANT);

  SET_HV(have);

  NEXT();
}

IMPL_TRIM_N(0)
IMPL_TRIM_N(1)
IMPL_TRIM_N(2)
IMPL_TRIM_N(3)
IMPL_TRIM_N(4)
IMPL_TRIM_N(5)
IMPL_TRIM_N(6)
IMPL_TRIM_N(7)
IMPL_TRIM_N(8)
IMPL_TRIM_N(9)

CASE_CODE(TRIM) {
  uint8_t want = READ_BYTE;
  uint64_t have = HV();

  MICRO_OP_TRIM(want, have);

  NEXT();
}

IMPL_RETURN_N(1)
IMPL_RETURN_N(2)
IMPL_RETURN_N(3)
IMPL_RETURN_N(4)
IMPL_RETURN_N(5)
IMPL_RETURN_N(6)
IMPL_RETURN_N(7)
IMPL_RETURN_N(8)
IMPL_RETURN_N(9)

CASE_CODE(RETURN) {
  uint64_t have = HV();

  if (have > 0 && have < 10) {
    WRITE_BYTE(1, OP_RETURN + have);
    IP() -= 1;
    NEXT();
  }

  MICRO_OP_RETURN(have);

  NEXT();
}

CASE_CODE(PACK_DICT) {
  uint8_t below = READ_BYTE;
  uint8_t above = READ_BYTE;

  MICRO_OP_PACK_DICT(below, above);

  NEXT();
}

CASE_CODE(PACK_LIST) {
  uint8_t below = READ_BYTE;
  uint8_t above = READ_BYTE;

  MICRO_OP_PACK_LIST(below, above);

  NEXT();
}

CASE_CODE(SEND) {
  uint8_t adjust;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(adjust);
  uint64_t have = HV();

  /* Have can not be 0. We need a receiver. */
  if (__gab_unlikely(!have)) {
    PUSH(MICRO_OP_NIL());
    SET_HV(1);
    have++;
  }

  gab_value r = PEEK_N(have);
  gab_value m = ks[GAB_SEND_KMESSAGE];

  if (BLOCK() && try_setup_localmatch(GAB(), m, ks, BLOCK_PROTO())) {
    WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_MATCHSEND_BLOCK + adjust);
    IP() -= GAB_SEND_CACHE_SIZE;
    NEXT();
  }

  /* Do the expensive lookup */
  struct gab_impl_rest res = gab_impl(GAB(), m, r);

  if (__gab_unlikely(!res.status))
    VM_PANIC3(GAB_SPECIALIZATION_MISSING, m, r, gab_valtype(GAB(), r));

  gab_value spec = res.status == kGAB_IMPL_PROPERTY
                       ? gab_primitive(OP_SEND_PROPERTY)
                       : res.as.spec;

  ks[GAB_SEND_KSPECS] = atomic_load(&EG()->messages_epoch);
  ks[GAB_SEND_KTYPE] = gab_valtype(GAB(), r);
  ks[GAB_SEND_KSPEC] = res.as.spec;

  switch (gab_valkind(spec)) {
  case kGAB_PRIMITIVE: {
    uint8_t op = gab_valtop(spec);

    if (op == OP_SEND_PRIMITIVE_CALL_BLOCK)
      op += adjust;

    WRITE_BYTE(GAB_SEND_CACHE_SIZE, op);

    break;
  }
  case kGAB_BLOCK: {
    struct gab_oblock *b = GAB_VAL_TO_BLOCK(spec);
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

    uint8_t local = (BLOCK() && BLOCK_PROTO()->src == p->src);
    adjust |= (local << 1);

    if (local) {
      ks[GAB_SEND_KOFFSET] = (intptr_t)proto_ip(GAB(), p);
    }

    WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_BLOCK + adjust);

    break;
  }
  case kGAB_NATIVE: {
    WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_NATIVE);
    break;
  }
  default:
    WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_CONSTANT);
    break;
  }

  IP() -= GAB_SEND_CACHE_SIZE;

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_TAKE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value c = PEEK_N(have);

  MICRO_OP_TAKE(c);
}

CASE_CODE(SEND_PRIMITIVE_PUT) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value c = PEEK_N(have);

  MICRO_OP_PUT(c);
}

CASE_CODE(SEND_PRIMITIVE_FIBER) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  NILPAD_GUARD_ARGS_GTE(2);

  gab_value block = PEEK_N(have - 1);

  PANIC_GUARD_KIND(block, kGAB_BLOCK);

  MICRO_OP_FIBER(block);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CHANNEL) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  DROP_N(have + 1 + FRAME_SIZE);

  gab_value chan = MICRO_OP_CHANNEL();

  PUSH(chan);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_RECORD) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  if (__gab_unlikely(len & 1))
    PUSH(MICRO_OP_NIL()), len++, have++;

  gab_value record = MICRO_OP_RECORD(len);

  DROP_N(have + 1 + FRAME_SIZE);

  PUSH(record);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_MAKE_SHAPE) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  gab_value shape = PEEK_N(have);
  uint64_t len = have - 1;

  PANIC_GUARD_SHAPE_LEN(shape, len);

  gab_value record = MICRO_OP_RECORDFROM(shape, len);

  DROP_N(have + 1 + FRAME_SIZE);

  PUSH(record);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SHAPE) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  gab_value shape = MICRO_OP_SHAPE(len);

  DROP_N(have + 1 + FRAME_SIZE);

  PUSH(shape);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_LIST) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  gab_value rec = MICRO_OP_LIST(0, len);

  DROP_N(have + 1 + FRAME_SIZE);

  PUSH(rec);

  SET_HV(below_have + 1);

  NEXT();
}
