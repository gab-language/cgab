/**
 *  MIT License
 *
 *  Copyright (c) 2023 Teddy Randby
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
#include "core.h"
#include "engine.h"
#include "gab.h"
#include "stdint.h"

// #define gmove(dst, src, n) \
//   ({ \
//     gab_value *D = dst; \
//     gab_value *S = src; \
//     uint64_t N = n; \
//     if (D != S) \
//       while (N--) \
//         *D++ = *S++; \
//   })
#define gmove(dst, src, n) memmove(dst, src, n * sizeof(gab_value))

// #define gmoveb(dst, src, n) \
//   ({ \
//     gab_value *D = dst; \
//     gab_value *S = src; \
//     uint64_t N = n; \
//     D += N; \
//     S += N; \
//     while (N--) \
//       *--D = *--S; \
//   })
#define gmoveb(dst, src, n) memmove(dst, src, n * sizeof(gab_value))

/*
 * In x86, the maximum number of ponter arguments that are passed
 * in registers is 6.
 *
 * On arm, it is 8.
 *
 * we *cannot* go above this limit and pass args on the stack here.
 */
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

static const char *gab_opcode_names[] = {
#define OP_CODE(name) #name,
#include "bytecode.h"
#undef OP_CODE
#undef GAB_OPCODE_NAMES_IMPL
};

#define GAB() (*__gab)
#define EG() (GAB().eg)
#define FIBER() (GAB_VAL_TO_FIBER(gab_thisfiber(GAB())))
#define REENTRANT() (FIBER()->reentrant)
#define RESET_REENTRANT() (FIBER()->reentrant = 0)
#define RESET_BUMP() (FIBER()->allocator.len = 0)
#define GC() (GAB().eg->gc)
#define VM() (__vm)
#define SET_BLOCK(b) (FB()[-(1 + FRAME_BK)] = (uintptr_t)(b));
#define BLOCK() ((struct gab_oblock *)(uintptr_t)FB()[-(1 + FRAME_BK)])
#define BLOCK_PROTO() (GAB_VAL_TO_PROTOTYPE(BLOCK()->p))
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
  return (void *)f[-(FRAME_BK + 1)];
}

static inline uint8_t *frame_ip(gab_value *f) {
  return (void *)f[-(FRAME_IP + 1)];
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
    return gab_list(gab, nframes, vframes);
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
             "Terminating fiber must be running, not: %d. Terminating.",
             gab_valkind(fiber));

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
  // gab_fprintf(stderr, "($) VMTERM finished fiber $.\n", gab_number(gab.wkid),
  //             __gab_obj(fiber));

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
             "Terminating fiber must be running, not: %d. Given err.",
             gab_valkind(fiber));

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

  return given;
}

static const char *gab_status_names[] = {
#define STATUS(name, message) #name,
#include "status_code.h"
#undef STATUS
};

union gab_value_pair vvm_error(struct gab_triple gab, enum gab_status s,
                               const char *fmt, va_list va) {
  gab_value fiber = gab_thisfiber(gab);

  gab_assert(gab_valkind(fiber) == kGAB_FIBERRUNNING,
             "Terminating fiber must be running, not: %d. Error status %s.",
             gab_valkind(fiber), gab_status_names[s]);

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
  // gab_fprintf(stderr, "($) VVMERR finished fiber $.\n", gab_number(gab.wkid),
  //             __gab_obj(fiber));

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
    return vm_error(GAB(), status, FMT_MISSINGIMPL, SP()[2], SP()[3], SP()[4],
                    SP()[5], SP()[6]);
  default:
    assert(false && "Unreachable");
  }
}

cGAB_VM_OPCODE_ATTRIBUTES union gab_value_pair vm_ok(OP_HANDLER_ARGS) {
  uint64_t have = *VM()->sp;
  gab_value *from = VM()->sp - have;

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

  if (fiber->header.kind == kGAB_FIBERDONE) {
    // gab_fprintf(stderr, "($) OK'd finished fiber $.\n",
    // gab_number(GAB().wkid),
    //             __gab_obj(fiber));
    // gab_fprintf(stderr, "STATUS: $\n", fiber->res_values.status);
    // gab_fprintf(stderr, "VRESULT: $\n", fiber->res_values.vresult);
  }
  gab_assert(fiber->header.kind == kGAB_FIBERRUNNING,
             "(%i) Terminating fiber must be running, not: %d. OK!", GAB().wkid,
             fiber->header.kind);

  gab_assert(fiber->res_env == gab_cinvalid,
             "Terminating fiber res_env shall be uninitialized.");

  gab_assert(fiber->res_values.status == 0,
             "Terminating fiber res shall be uninitialized.");

  fiber->res_values = res;

  if (frame_block(VM()->fp)) {
    gab_value p = frame_block(VM()->fp)->p;
    gab_value shape = gab_prtshp(p);

    gab_value env =
        gab_recordfrom(GAB(), shape, 1, gab_shplen(shape), VM()->fp, nullptr);

    gab_egkeep(EG(), gab_iref(GAB(), env));

    fiber->res_env = env;
  }

  fiber->header.kind = kGAB_FIBERDONE;
  // gab_fprintf(stderr, "($) VMOK finished fiber $.\n", gab_number(GAB().wkid),
  //             __gab_obj(fiber));

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
                                                                               \
    SET_HV(have);                                                              \
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
    gmove(to, from, have);                                                     \
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
    gmove(to, from, have);                                                     \
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
    gmove(to, from, have);                                                     \
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
    gab_value *to = SP() - (have + 1 + FRAME_SIZE);                            \
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
    gmove(to, before, have);                                                   \
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
      gmove(SP(), SP() + have + 1 + FRAME_SIZE, len);                          \
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
                                                                               \
      IP() -= 2;                                                               \
                                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    if (have > want && have - want < 10) {                                     \
      WRITE_BYTE(2, OP_TRIM_DOWN1 - 1 + (have - want));                        \
                                                                               \
      IP() -= 2;                                                               \
                                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    if (want > have && want - have < 10) {                                     \
      WRITE_BYTE(2, OP_TRIM_UP1 - 1 + (want - have));                          \
                                                                               \
      IP() -= 2;                                                               \
                                                                               \
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
       * This pulling in of args/values to pass on to use'd module             \
       * is a little scuffed. I'd rather do it a different way.                \
       */                                                                      \
      gab_value shp = gab_prtshp(BLOCK()->p);                                  \
                                                                               \
      gab_value rec =                                                          \
          gab_recordfrom(GAB(), shp, 1, gab_shplen(shp), FB() + 1, nullptr);   \
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
    gab_list(GAB(), (len), SP() - ((n) + (len)));                              \
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
    gab_value rec = gab_list(GAB(), len, ap - len);                            \
                                                                               \
    DROP_N(len - 1);                                                           \
                                                                               \
    gmove(ap - len + 1, ap, above);                                            \
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
    gmove(ap - len + 1, ap, above);                                            \
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
    PUSH(FRAME_BK);                                                            \
    PUSH(FRAME_IP);                                                            \
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
      [[clang::musttail]] return vm_ok(DISPATCH_ARGS());                       \
    }                                                                          \
                                                                               \
    assert(RETURN_IP() != nullptr);                                            \
                                                                               \
    LOAD_FRAME();                                                              \
                                                                               \
    gmove(to, from, have);                                                     \
                                                                               \
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

CASE_CODE(RETURN) {
  uint64_t have = HV();

  MICRO_OP_RETURN(have);

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

CASE_CODE(FRAME) {
  uint64_t have = HV();
  SP() += FRAME_SIZE;

  SP()[-1] = FRAME_IP;
  SP()[-2] = FRAME_BK;

  SET_HV(have);

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

  while (n--) {
    uint64_t have = HV();
    PUSHTUPLE(have);
    SET_HV(0);
  }

  PUSH(LOCAL(READ_BYTE));

  SET_HV(1);

  NEXT();
}

CASE_CODE(NTUPLE_NLOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = HV();
    PUSHTUPLE(have);
    SET_HV(0);
  }

  n = READ_BYTE;

  uint8_t have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_HV(have);

  NEXT();
}

CASE_CODE(NTUPLE_CONSTANT) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = HV();
    PUSHTUPLE(have);
    SET_HV(0);
  }

  PUSH(READ_CONSTANT);

  SET_HV(1);

  NEXT();
}

CASE_CODE(NTUPLE_NCONSTANT) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = HV();
    PUSHTUPLE(have);
    SET_HV(0);
  }

  n = READ_BYTE;

  uint8_t have = n;

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

    uint8_t local = (GAB_VAL_TO_PROTOTYPE(b->p)->src == BLOCK_PROTO()->src);
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
