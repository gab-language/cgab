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
#include "vm_ops.h"

ATTRIBUTES
typedef union gab_value_pair (*handler)(OP_HANDLER_ARGS);

// Forward declare all our opcode handlers
#define OP_CODE(name)                                                          \
  ATTRIBUTES union gab_value_pair OP_##name##_HANDLER(OP_HANDLER_ARGS);
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

uint64_t encode_fb(struct gab_vm *vm, gab_value *fb, uint64_t have) {
  uint64_t offset = fb - vm->sb;
  assert(offset < UINT32_MAX);
  assert(have < UINT32_MAX);
  return offset << 32 | have;
}

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

static inline gab_value *frame_parent(gab_value *f) {
  int64_t delta = f[-(FRAME_SIZE + 1)] >> 32;
  return delta ? f - delta : nullptr;
}

static inline struct gab_oblock *frame_block(gab_value *f) {
  return (void *)f[-FRAME_BK];
}

static inline uint8_t *frame_ip(gab_value *f) { return (void *)f[-FRAME_IP]; }

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
             "Terminating fiber must be running or done, not: %d",
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
             "Terminating fiber must be running or done, not: %d",
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

union gab_value_pair vvm_error(struct gab_triple gab, enum gab_status s,
                               const char *fmt, va_list va) {
  gab_value fiber = gab_thisfiber(gab);

  gab_assert(gab_valkind(fiber) == kGAB_FIBERRUNNING,
             "Terminating fiber must be running or done, not: %d",
             gab_valkind(fiber));

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
  uint8_t *__ip = vm->ip - SEND_CACHE_DIST + 1;
  gab_value *__kb = vm->kb;
  gab_value *ks = READ_SENDCONSTANTS;
  return ks[GAB_SEND_KMESSAGE];
}

gab_value gab_vmspec(struct gab_vm *vm) {
  uint8_t *__ip = vm->ip - SEND_CACHE_DIST + 1;
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

static inline uint64_t get_stackspace(gab_value *sp, gab_value *sb) {
  return (sp - sb) + 3;
}

static inline bool has_stackspace(gab_value *sp, gab_value *sb,
                                  uint64_t space_needed) {
  return get_stackspace(sp, sb) + space_needed < cGAB_STACK_MAX;
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
  if (__gab_unlikely(argc == 0 || !has_stackspace(vm->sp, vm->sb, argc))) {
    return 0;
  }

  uint64_t have = *vm->sp;

  for (uint8_t n = 0; n < argc; n++) {
    *vm->sp++ = argv[n];
  }

  *vm->sp = have + argc;

  return argc;
}

union gab_value_pair vm_ok(OP_HANDLER_ARGS) {
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

  gab_assert(fiber->header.kind == kGAB_FIBERRUNNING,
             "Terminating fiber must be running or done, not: %d",
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

  return res;
}

union gab_value_pair do_vmexecfiber(struct gab_triple gab, gab_value f) {
  assert(gab_valkind(f) == kGAB_FIBER);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  assert(fiber->header.kind != kGAB_FIBERDONE);

  assert(*fiber->vm.sb == 0);
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

ATTRIBUTES
union gab_value_pair gab_jtexit(struct gab_triple *__gab, struct gab_vm *__vm,
                                uint8_t *__ip, gab_value *__kb, gab_value *__fb,
                                gab_value *__sp) {
  NEXT();
}

#include "ops.h"
