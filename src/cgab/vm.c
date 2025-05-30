#include "core.h"
#include "gab.h"
#include <stdint.h>

#define GAB_STATUS_NAMES_IMPL
#include "engine.h"

#include "colors.h"
#include "lexer.h"

#include <stdarg.h>
#include <string.h>

#define OP_HANDLER_ARGS                                                        \
  struct gab_triple __gab, uint8_t *__ip, gab_value *__kb, gab_value *__fb,    \
      gab_value *__sp

typedef union gab_value_pair (*handler)(OP_HANDLER_ARGS);

// Forward declare all our opcode handlers
#define OP_CODE(name) union gab_value_pair OP_##name##_HANDLER(OP_HANDLER_ARGS);
#include "bytecode.h"
#undef OP_CODE

// Plop them all in an array
static handler handlers[] = {
#define OP_CODE(name) OP_##name##_HANDLER,
#include "bytecode.h"
#undef OP_CODE
};

#if cGAB_LOG_VM
#define LOG(op) printf("OP_%s (%lu)\n", gab_opcode_names[op], VAR());
#else
#define LOG(op)
#endif

#define ATTRIBUTES

#define CASE_CODE(name)                                                        \
  ATTRIBUTES union gab_value_pair OP_##name##_HANDLER(OP_HANDLER_ARGS)

#define DISPATCH_ARGS() GAB(), IP(), KB(), FB(), SP()

#define DISPATCH(op)                                                           \
  ({                                                                           \
    if (gab_sigwaiting(GAB())) {                                               \
      STORE_SP();                                                              \
      switch (gab_yield(GAB())) {                                              \
      case sGAB_COLL:                                                          \
        gab_gcepochnext(GAB());                                                \
        gab_sigpropagate(GAB());                                               \
        break;                                                                 \
      case sGAB_TERM:                                                          \
        VM_TERM();                                                             \
      default:                                                                 \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
                                                                               \
    uint8_t o = (op);                                                          \
                                                                               \
    LOG(o)                                                                     \
                                                                               \
    assert(SP() < VM()->sb + cGAB_STACK_MAX);                                  \
    assert(SP() > FB());                                                       \
                                                                               \
    [[clang::musttail]] return handlers[o](DISPATCH_ARGS());                   \
  })

#define NEXT() DISPATCH(*(IP()++));

#define VM_PANIC(status, help, ...)                                            \
  ({                                                                           \
    STORE();                                                                   \
    return vm_error(GAB(), status, help __VA_OPT__(, ) __VA_ARGS__);           \
  })

#define VM_GIVEN(err)                                                          \
  ({                                                                           \
    STORE();                                                                   \
    return vm_givenerr(GAB(), err);                                            \
  })

/*
  Lots of helper macros.
*/
#define GAB() (__gab)
#define EG() (GAB().eg)
#define FIBER() (GAB_VAL_TO_FIBER(gab_thisfiber(GAB())))
#define GC() (GAB().eg->gc)
#define VM() (gab_thisvm(GAB()))
#define SET_BLOCK(b) (FB()[-3] = (uintptr_t)(b));
#define BLOCK() ((struct gab_oblock *)(uintptr_t)FB()[-3])
#define BLOCK_PROTO() (GAB_VAL_TO_PROTOTYPE(BLOCK()->p))
#define IP() (__ip)
#define SP() (__sp)
#define SB() (VM()->sb)
#define VAR() (*SP())
#define FB() (__fb)
#define KB() (__kb)
#define LOCAL(i) (FB()[i])
#define UPVALUE(i) (BLOCK()->upvalues[i])

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

#define WRITE_BYTE(dist, n) (*(IP() - dist) = (n))

#define WRITE_INLINEBYTE(n) (*IP()++ = (n))

#define SKIP_BYTE (IP()++)
#define SKIP_SHORT (IP() += 2)

#define READ_BYTE (*IP()++)
#define READ_SHORT (IP() += 2, (((uint16_t)IP()[-2] << 8) | IP()[-1]))
#define PREVIEW_SHORT (((uint16_t)IP()[0] << 8) | IP()[1])

#define READ_CONSTANT (KB()[READ_SHORT])
#define READ_CONSTANTS (KB() + READ_SHORT)

#define MISS_CACHED_SEND(clause)                                               \
  ({                                                                           \
    IP() -= SEND_CACHE_DIST - 1;                                               \
    [[clang::musttail]] return OP_SEND_HANDLER(DISPATCH_ARGS());               \
  })

#define MISS_CACHED_TRIM()                                                     \
  ({                                                                           \
    IP()--;                                                                    \
    [[clang::musttail]] return OP_TRIM_HANDLER(DISPATCH_ARGS());               \
  })

#define VM_YIELD()                                                             \
  ({                                                                           \
    IP() -= SEND_CACHE_DIST;                                                   \
    STORE();                                                                   \
    return vm_yield(GAB());                                                    \
  })

#define VM_TERM()                                                              \
  ({                                                                           \
    STORE();                                                                   \
    return vm_terminate(GAB(), "$", gab_thisfiber(GAB()));                     \
  })

#define IMPL_SEND_UNARY_NUMERIC(CODE, value_type, operation_type, decoder,     \
                                operation)                                     \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_CONSTANTS;                                            \
    uint64_t have = COMPUTE_TUPLE(READ_BYTE);                                  \
    uint64_t below_have = PEEK_N(have + 1);                                    \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));                             \
    VM_PANIC_GUARD_ISN(PEEK_N(have));                                          \
                                                                               \
    operation_type val = decoder(PEEK_N(have));                                \
                                                                               \
    DROP_N(have + 1);                                                          \
    PUSH(value_type(operation val));                                           \
                                                                               \
    SET_VAR(below_have + 1);                                                   \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_SEND_BINARY_NUMERIC(CODE, value_type, operation_type, decoder,    \
                                 operation)                                    \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_CONSTANTS;                                            \
    uint64_t have = COMPUTE_TUPLE(READ_BYTE);                                  \
    uint64_t below_have = PEEK_N(have + 1);                                    \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));                             \
                                                                               \
    if (__gab_unlikely(have < 2))                                              \
      PUSH(gab_nil), have++;                                                   \
                                                                               \
    VM_PANIC_GUARD_ISN(PEEK_N(have));                                          \
    VM_PANIC_GUARD_ISN(PEEK_N(have - 1));                                      \
                                                                               \
    operation_type val_b = decoder(PEEK_N(have - 1));                          \
    operation_type val_a = decoder(PEEK_N(have));                              \
                                                                               \
    DROP_N(have + 1);                                                          \
    PUSH(value_type(val_a operation val_b));                                   \
                                                                               \
    SET_VAR(below_have + 1);                                                   \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_SEND_BINARY_SHIFT_NUMERIC(CODE, operation, opposite_operation)    \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_CONSTANTS;                                            \
    uint64_t have = COMPUTE_TUPLE(READ_BYTE);                                  \
    uint64_t below_have = PEEK_N(have + 1);                                    \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));                             \
                                                                               \
    if (__gab_unlikely(have < 2))                                              \
      PUSH(gab_nil), have++;                                                   \
                                                                               \
    VM_PANIC_GUARD_ISN(PEEK_N(have));                                          \
    VM_PANIC_GUARD_ISN(PEEK_N(have - 1));                                      \
                                                                               \
    gab_int amount = gab_valtoi(PEEK_N(have - 1));                             \
    gab_uint val_a = gab_valtou(PEEK_N(have));                                 \
                                                                               \
    DROP_N(have + 1);                                                          \
                                                                               \
    if (__gab_unlikely(amount >= GAB_INTWIDTH)) {                              \
      PUSH(gab_number(0));                                                     \
    } else if (__gab_unlikely(amount < 0)) {                                   \
      gab_int res = (val_a opposite_operation((uint32_t)-amount));             \
      assert(gab_valtoi(gab_number(res)) == res);                              \
      PUSH(gab_number(res));                                                   \
    } else {                                                                   \
      gab_int res = (val_a operation(uint32_t) amount);                        \
      assert(gab_valtoi(gab_number(res)) == res);                              \
      PUSH(gab_number(res));                                                   \
    }                                                                          \
                                                                               \
    SET_VAR(below_have + 1);                                                   \
                                                                               \
    NEXT();                                                                    \
  }

// FIXME: This doesn't work
// These boolean sends don't work because there is no longer a boolean type.
// There are just sigils
#define IMPL_SEND_UNARY_BOOLEAN(CODE, value_type, operation_type, operation)   \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_CONSTANTS;                                            \
    uint64_t have = COMPUTE_TUPLE(READ_BYTE);                                  \
    uint64_t below_have = PEEK_N(have + 1);                                    \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));                             \
                                                                               \
    VM_PANIC_GUARD_ISB(PEEK_N(have));                                          \
                                                                               \
    operation_type val = gab_valintob(PEEK_N(have));                           \
                                                                               \
    DROP_N(have + 1);                                                          \
    PUSH(value_type(operation val));                                           \
                                                                               \
    SET_VAR(below_have + 1);                                                   \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_SEND_BINARY_BOOLEAN(CODE, value_type, operation_type, operation)  \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_CONSTANTS;                                            \
    uint64_t have = COMPUTE_TUPLE(READ_BYTE);                                  \
    uint64_t below_have = PEEK_N(have + 1);                                    \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));                             \
                                                                               \
    if (__gab_unlikely(have < 2))                                              \
      PUSH(gab_nil), have++;                                                   \
                                                                               \
    VM_PANIC_GUARD_ISB(PEEK_N(have));                                          \
    VM_PANIC_GUARD_ISB(PEEK_N(have - 1));                                      \
                                                                               \
    operation_type val_b = gab_valintob(PEEK_N(have));                         \
    operation_type val_a = gab_valintob(PEEK_N(have - 1));                     \
                                                                               \
    DROP_N(have + 1);                                                          \
    PUSH(value_type(val_a operation val_b));                                   \
                                                                               \
    SET_VAR(below_have + 1);                                                   \
                                                                               \
    NEXT();                                                                    \
  }

#define TRIM_N(n)                                                              \
  CASE_CODE(TRIM_DOWN##n) {                                                    \
    uint8_t want = READ_BYTE;                                                  \
                                                                               \
    if (__gab_unlikely((VAR() - n) != want))                                   \
      MISS_CACHED_TRIM();                                                      \
                                                                               \
    DROP_N(n);                                                                 \
    VAR() = 0;                                                                 \
                                                                               \
    NEXT();                                                                    \
  }                                                                            \
  CASE_CODE(TRIM_EXACTLY##n) {                                                 \
    SKIP_BYTE;                                                                 \
                                                                               \
    if (__gab_unlikely(VAR() != n))                                            \
      MISS_CACHED_TRIM();                                                      \
                                                                               \
    VAR() = 0;                                                                 \
    NEXT();                                                                    \
  }                                                                            \
  CASE_CODE(TRIM_UP##n) {                                                      \
    uint8_t want = READ_BYTE;                                                  \
                                                                               \
    if (__gab_unlikely((VAR() + n) != want))                                   \
      MISS_CACHED_TRIM();                                                      \
                                                                               \
    for (int i = 0; i < n; i++)                                                \
      PUSH(gab_nil);                                                           \
                                                                               \
    VAR() = 0;                                                                 \
    NEXT();                                                                    \
  }

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

uint64_t encode_fb(struct gab_vm *vm, gab_value *fb, uint64_t have) {
  uint64_t offset = fb - vm->sb;
  assert(offset < UINT32_MAX);
  assert(have < UINT32_MAX);
  return offset << 32 | have;
}

#define PUSH_FRAME(b, have)                                                    \
  ({                                                                           \
    memmove(SP() - have + 3, SP() - have, have * sizeof(gab_value));           \
    SP() += 3;                                                                 \
    SP()[-(int64_t)have - 1] = (uintptr_t)FB();                                \
    SP()[-(int64_t)have - 2] = (uintptr_t)IP();                                \
    SP()[-(int64_t)have - 3] = (uintptr_t)b;                                   \
  })

#define PUSH_VM_PANIC_FRAME(have) ({})

#define STORE_PRIMITIVE_VM_PANIC_FRAME(have)                                   \
  ({                                                                           \
    STORE();                                                                   \
    PUSH_VM_PANIC_FRAME(have);                                                 \
  })

#define RETURN_FB() ((gab_value *)(void *)FB()[-1])
#define RETURN_IP() ((uint8_t *)(void *)FB()[-2])

#define LOAD_FRAME()                                                           \
  ({                                                                           \
    IP() = RETURN_IP();                                                        \
    FB() = RETURN_FB();                                                        \
    KB() = proto_ks(GAB(), BLOCK_PROTO());                                     \
  })

#define SEND_CACHE_DIST 4

static inline uint8_t *proto_srcbegin(struct gab_triple gab,
                                      struct gab_oprototype *p) {
  assert(gab.wkid != 0);
  return p->src->thread_bytecode[gab.wkid - 1].bytecode;
}

static inline uint8_t *proto_ip(struct gab_triple gab,
                                struct gab_oprototype *p) {
  return proto_srcbegin(gab, p) + p->offset;
}

static inline gab_value *proto_ks(struct gab_triple gab,
                                  struct gab_oprototype *p) {
  assert(gab.wkid != 0);
  return p->src->thread_bytecode[gab.wkid - 1].constants;
}

static inline gab_value *frame_parent(gab_value *f) { return (void *)f[-1]; }

static inline struct gab_oblock *frame_block(gab_value *f) {
  return (void *)f[-3];
}

static inline uint8_t *frame_ip(gab_value *f) { return (void *)f[-2]; }

static inline uint64_t compute_token_from_ip(struct gab_triple gab,
                                             struct gab_oblock *b,
                                             uint8_t *ip) {
  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

  assert(ip > proto_srcbegin(gab, p));
  uint64_t offset = ip - proto_srcbegin(gab, p) - 1;

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
    };
  }

  return (struct gab_err_argt){
      .note_fmt = fmt,
      .status = s,
  };
}

union gab_value_pair vm_yield(struct gab_triple gab) {
  gab_value fiber = gab_thisfiber(gab);
  assert(GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERRUNNING);
  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBER;
  return (union gab_value_pair){{gab_ctimeout}};
}

gab_value sprint_stacktrace(struct gab_triple gab, struct gab_vm *vm,
                            gab_value *f, uint8_t *ip, int s, const char *fmt,
                            va_list va) {
  // TODO: Place reasonable limit on number of frames to sprint.
  // Also, skip middle ones sometimes.
  int nframes = 0;
  gab_value vframes[1024] = {0};

  struct gab_err_argt frame =
      vm_frame_build_err(gab, frame_block(f), ip, s, fmt);
  vframes[nframes++] = gab_vspanicf(gab, va, frame);

  ip = frame_ip(f);
  f = frame_parent(f);

  while (f && frame_parent(f) > vm->sb) {
    frame = vm_frame_build_err(gab, frame_block(f), ip, GAB_NONE, "");
    vframes[nframes++] = gab_vspanicf(gab, va, frame);

    ip = frame_ip(f);
    f = frame_parent(f);
  }

  return gab_list(gab, nframes, vframes);
}

union gab_value_pair vvm_terminate(struct gab_triple gab, const char *fmt,
                                   va_list va) {
  gab_value fiber = gab_thisfiber(gab);

  struct gab_vm *vm = gab_thisvm(gab);

  gab_value *f = vm->fp;
  uint8_t *ip = vm->ip;

  gab_value err = sprint_stacktrace(gab, vm, f, ip, GAB_TERM, fmt, va);

  gab_iref(gab, err);
  gab_egkeep(gab.eg, err);

  union gab_value_pair res = {{gab_cinvalid, err}};

  gab_value p = frame_block(vm->fp)->p;
  gab_value shape = gab_prtshp(p);
  gab_value env =
      gab_recordfrom(gab, shape, 1, gab_shplen(shape), vm->fp, nullptr);
  gab_egkeep(gab.eg, gab_iref(gab, env));

  if (gab.eg->joberr_handler)
    gab.eg->joberr_handler(gab, res.vresult);

  assert(GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERRUNNING);
  GAB_VAL_TO_FIBER(fiber)->res_values = res;
  GAB_VAL_TO_FIBER(fiber)->res_env = env;
  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERDONE;

  return res;
}
union gab_value_pair vm_givenerr(struct gab_triple gab,
                                 union gab_value_pair given) {
  gab_value fiber = gab_thisfiber(gab);

  struct gab_vm *vm = gab_thisvm(gab);

  gab_value p = frame_block(vm->fp)->p;
  gab_value shape = gab_prtshp(p);
  gab_value env =
      gab_recordfrom(gab, shape, 1, gab_shplen(shape), vm->fp, nullptr);
  gab_egkeep(gab.eg, gab_iref(gab, env));

  assert(GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERRUNNING);
  GAB_VAL_TO_FIBER(fiber)->res_values = given;
  GAB_VAL_TO_FIBER(fiber)->res_env = env;
  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERDONE;
  return given;
}

union gab_value_pair vvm_error(struct gab_triple gab, enum gab_status s,
                               const char *fmt, va_list va) {
  gab_value fiber = gab_thisfiber(gab);

  struct gab_vm *vm = gab_thisvm(gab);

  gab_value *f = vm->fp;
  uint8_t *ip = vm->ip;

  gab_value err = sprint_stacktrace(gab, vm, f, ip, s, fmt, va);

  gab_iref(gab, err);
  gab_egkeep(gab.eg, err);

  gab_value vals[] = {gab_err, err};
  a_gab_value *results =
      a_gab_value_create(vals, sizeof(vals) / sizeof(gab_value));

  gab_niref(gab, 1, results->len, results->data);
  gab_negkeep(gab.eg, results->len, results->data);

  union gab_value_pair res = {.status = gab_cvalid, .aresult = results};

  gab_value p = frame_block(vm->fp)->p;
  gab_value shape = gab_prtshp(p);
  gab_value env =
      gab_recordfrom(gab, shape, 1, gab_shplen(shape), vm->fp, nullptr);
  gab_egkeep(gab.eg, gab_iref(gab, env));

  if (gab.eg->joberr_handler)
    gab.eg->joberr_handler(gab, res.aresult->data[1]);

  assert(GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERRUNNING);
  GAB_VAL_TO_FIBER(fiber)->res_values = res;
  GAB_VAL_TO_FIBER(fiber)->res_env = env;
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
  "Sent message " GAB_CYAN "$" GAB_RESET                                       \
  " found an invalid type.\n\n    | " GAB_GREEN "$" GAB_RESET                  \
  "\n\nhas type\n\n    | " GAB_GREEN "$" GAB_RESET                             \
  "\n\nbut expected type\n\n    | " GAB_GREEN "$" GAB_RESET "\n"

#define FMT_MISSINGIMPL                                                        \
  "Sent message " GAB_CYAN "$" GAB_RESET                                       \
  " does not specialize for this receiver.\n\n    | " GAB_CYAN "$" GAB_RESET   \
  "\n\nof type\n\n    "                                                        \
  "| " GAB_CYAN "$" GAB_RESET "\n"

union gab_value_pair gab_vpanicf(struct gab_triple gab, const char *fmt,
                                 va_list va) {
  if (gab_thisfiber(gab) == gab_cinvalid) {
    gab_value err = gab_vspanicf(gab, va,
                                 (struct gab_err_argt){
                                     .status = GAB_PANIC,
                                     .note_fmt = fmt,
                                 });

    gab_iref(gab, err);
    gab_egkeep(gab.eg, err);

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

union gab_value_pair gab_ptypemismatch(struct gab_triple gab, gab_value found,
                                       gab_value texpected) {

  // TODO: Properly set message here.
  return vm_error(gab, GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, gab_nil, found,
                  gab_valtype(gab, found), texpected);
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

#define COMPUTE_TUPLE(flags) (((flags) & fHAVE_VAR) ? POP() : VAR());

static inline bool has_stackspace(gab_value *sp, gab_value *sb,
                                  uint64_t space_needed) {
  if ((sp - sb) + space_needed + 3 >= cGAB_STACK_MAX) {
    return false;
  }

  return true;
}

inline uint64_t gab_nvmpush(struct gab_vm *vm, uint64_t argc,
                            gab_value argv[argc]) {
  if (__gab_unlikely(argc == 0 || !has_stackspace(vm->sp, vm->sb, argc))) {
    return 0;
  }

  for (uint8_t n = 0; n < argc; n++) {
    *vm->sp++ = argv[n];
  }

  return argc;
}

#define OP_TRIM_N(n) ((uint16_t)OP_TRIM << 8 | (n))

#define SET_VAR(n)                                                             \
  ({                                                                           \
    assert(SP() > FB());                                                       \
    VAR() = n;                                                                 \
  })

#define VM_PANIC_GUARD_STACKSPACE(space)                                       \
  if (__gab_unlikely(!has_stackspace(SP(), SB(), space)))                      \
    VM_PANIC(GAB_OVERFLOW, "");

#define CALL_BLOCK(blk, have)                                                  \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
    VM_PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                           \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
    KB() = proto_ks(GAB(), p);                                                 \
    FB() = SP() - have;                                                        \
                                                                               \
    SET_VAR(have);                                                             \
                                                                               \
    NEXT();                                                                    \
  })

#define LOCALCALL_BLOCK(blk, have)                                             \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
    VM_PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                           \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = ((void *)ks[GAB_SEND_KOFFSET]);                                     \
    FB() = SP() - have;                                                        \
                                                                               \
    SET_VAR(have);                                                             \
                                                                               \
    NEXT();                                                                    \
  })

#define TAILCALL_BLOCK(blk, have)                                              \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
    VM_PANIC_GUARD_STACKSPACE(p->nslots - have);                               \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    memmove(to, from, have * sizeof(gab_value));                               \
    SP() = to + have;                                                          \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
    KB() = proto_ks(GAB(), p);                                                 \
                                                                               \
    SET_BLOCK(blk);                                                            \
    SET_VAR(have);                                                             \
                                                                               \
    NEXT();                                                                    \
  })

#define LOCALTAILCALL_BLOCK(blk, have)                                         \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
    VM_PANIC_GUARD_STACKSPACE(p->nslots - have);                               \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    memmove(to, from, have * sizeof(gab_value));                               \
    SP() = to + have;                                                          \
                                                                               \
    IP() = ((void *)ks[GAB_SEND_KOFFSET]);                                     \
                                                                               \
    SET_BLOCK(blk);                                                            \
    SET_VAR(have);                                                             \
                                                                               \
    NEXT();                                                                    \
  })

#define PROPERTY_RECORD(r, have, below_have)                                   \
  ({                                                                           \
    DROP_N(have + 1);                                                          \
    PUSH(gab_uvrecat(r, ks[GAB_SEND_KSPEC]));                                  \
                                                                               \
    SET_VAR(below_have + 1);                                                   \
                                                                               \
    NEXT();                                                                    \
  })

#define CALL_NATIVE(native, have, below_have, message)                         \
  ({                                                                           \
    STORE();                                                                   \
                                                                               \
    gab_value *to = SP() - (have + 1);                                         \
                                                                               \
    gab_value *before = SP();                                                  \
                                                                               \
    uint64_t pass = message ? have : have - 1;                                 \
                                                                               \
    union gab_value_pair res = (*native->function)(GAB(), pass, SP() - pass);  \
                                                                               \
    if (__gab_unlikely(res.status != gab_cundefined))                          \
      return res;                                                              \
                                                                               \
    SP() = VM()->sp;                                                           \
                                                                               \
    assert(SP() >= before);                                                    \
    uint64_t have = SP() - before;                                             \
                                                                               \
    if (!have)                                                                 \
      PUSH(gab_nil), have++;                                                   \
                                                                               \
    memmove(to, before, have * sizeof(gab_value));                             \
    SP() = to + have;                                                          \
                                                                               \
    SET_VAR(below_have + have);                                                \
                                                                               \
    NEXT();                                                                    \
  })

static inline gab_value block(struct gab_triple gab, gab_value p,
                              gab_value *locals, gab_value *upvs) {
  gab_value blk = gab_block(gab, p);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(blk);
  struct gab_oprototype *proto = GAB_VAL_TO_PROTOTYPE(p);

  for (int i = 0; i < proto->nupvalues; i++) {
    uint8_t is_local = proto->data[i] & fLOCAL_LOCAL;
    uint8_t index = proto->data[i] >> 1;

    if (is_local)
      b->upvalues[i] = locals[index];
    else
      b->upvalues[i] = upvs[index];
  }

  return blk;
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

  gab_value p = frame_block(VM()->fp)->p;
  gab_value shape = gab_prtshp(p);

  gab_value env =
      gab_recordfrom(GAB(), shape, 1, gab_shplen(shape), VM()->fp, nullptr);
  gab_egkeep(EG(), gab_iref(GAB(), env));

  assert(FIBER()->header.kind = kGAB_FIBERRUNNING);
  FIBER()->res_values = res;
  FIBER()->res_env = env;
  FIBER()->header.kind = kGAB_FIBERDONE;

  return res;
}

union gab_value_pair do_vmexecfiber(struct gab_triple gab, gab_value f) {
  assert(gab_valkind(f) == kGAB_FIBER);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  /*assert(*fiber->vm.sp > 0);*/

  assert(fiber->header.kind != kGAB_FIBERDONE);

  assert(fiber->vm.kb);
  assert(fiber->vm.ip);

  uint8_t *ip = fiber->vm.ip;

  uint8_t op = *ip++;
  // We can't return *to* this frame because it has no block.
  // But we *should* return here so that the environment returned
  // to the fiber is as expected

  if (op == OP_SEND_PRIMITIVE_CALL_BLOCK)
    op = OP_TAILSEND_PRIMITIVE_CALL_BLOCK;

  if (op == OP_SEND_BLOCK)
    op = OP_TAILSEND_BLOCK;

  assert(fiber->header.kind != kGAB_FIBERDONE);
  fiber->header.kind = kGAB_FIBERRUNNING;
  return handlers[op](gab, ip, fiber->vm.kb, fiber->vm.fp, fiber->vm.sp);
};

union gab_value_pair gab_vmexec(struct gab_triple gab, gab_value f) {
  assert(gab_valkind(f) == kGAB_FIBER);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  gab.flags |= fiber->flags;

  return do_vmexecfiber(gab, f);
}

#define VM_PANIC_GUARD_KIND(value, kind)                                       \
  if (__gab_unlikely(gab_valkind(value) != kind)) {                            \
    STORE_PRIMITIVE_VM_PANIC_FRAME(1);                                         \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, ks[GAB_SEND_KMESSAGE],       \
             value, gab_valtype(GAB(), value), gab_type(GAB(), kind));         \
  }

#define VM_PANIC_GUARD_ISB(value)                                              \
  if (__gab_unlikely(!__gab_valisb(value))) {                                  \
    STORE_PRIMITIVE_VM_PANIC_FRAME(have);                                      \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, ks[GAB_SEND_KMESSAGE],       \
             value, gab_valtype(GAB(), value), gab_type(GAB(), kGAB_MESSAGE)); \
  }

#define VM_PANIC_GUARD_ISN(value)                                              \
  if (__gab_unlikely(!__gab_valisn(value))) {                                  \
    STORE_PRIMITIVE_VM_PANIC_FRAME(have);                                      \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, ks[GAB_SEND_KMESSAGE],       \
             value, gab_valtype(GAB(), value), gab_type(GAB(), kGAB_NUMBER));  \
  }

#define VM_PANIC_GUARD_ISS(value)                                              \
  if (__gab_unlikely(gab_valkind(value) != kGAB_STRING &&                      \
                     gab_valkind(value) != kGAB_MESSAGE)) {                    \
    STORE_PRIMITIVE_VM_PANIC_FRAME(have);                                      \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, ks[GAB_SEND_KMESSAGE],       \
             value, gab_valtype(GAB(), value), gab_type(GAB(), kGAB_STRING));  \
  }

#define SEND_GUARD(clause)                                                     \
  if (__gab_unlikely(!(clause)))                                               \
  MISS_CACHED_SEND(clause)

#define SEND_GUARD_KIND(r, k) SEND_GUARD(gab_valkind(r) == k)

#define SEND_GUARD_ISC(r)                                                      \
  SEND_GUARD(gab_valkind(r) >= kGAB_CHANNEL &&                                 \
             gab_valkind(r) <= kGAB_CHANNELCLOSED)

#define SEND_GUARD_ISSHP(r)                                                    \
  SEND_GUARD(gab_valkind(r) == kGAB_SHAPE || gab_valkind(r) == kGAB_SHAPELIST)

#define SEND_GUARD_CACHED_MESSAGE_SPECS()                                      \
  SEND_GUARD(gab_valeq(gab_thisfibmsgrec(GAB(), ks[GAB_SEND_KMESSAGE]),        \
                       ks[GAB_SEND_KSPECS]))

#define SEND_GUARD_CACHED_RECEIVER_TYPE(r)                                     \
  SEND_GUARD(gab_valisa(GAB(), r, ks[GAB_SEND_KTYPE]))

#define SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m)                                \
  SEND_GUARD(gab_valeq(gab_thisfibmsgrec(GAB(), m),                            \
                       ks[GAB_SEND_KGENERIC_CALL_SPECS]))

CASE_CODE(MATCHTAILSEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value r = PEEK_N(have);
  gab_value t = gab_valtype(GAB(), r);

  SEND_GUARD_CACHED_MESSAGE_SPECS();

  uint8_t idx = GAB_SEND_HASH(t) * GAB_SEND_CACHE_SIZE;

  // TODO: Handle undefined and record case
  if (__gab_unlikely(ks[GAB_SEND_KTYPE + idx] != t))
    MISS_CACHED_SEND();

  struct gab_oblock *b = (void *)ks[GAB_SEND_KSPEC + idx];

  gab_value *from = SP() - have;
  gab_value *to = FB();

  memmove(to, from, have * sizeof(gab_value));

  IP() = (void *)ks[GAB_SEND_KOFFSET + idx];
  SP() = to + have;

  SET_BLOCK(b);
  SET_VAR(have);

  NEXT();
}

CASE_CODE(MATCHSEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value r = PEEK_N(have);
  gab_value t = gab_valtype(GAB(), r);

  SEND_GUARD_CACHED_MESSAGE_SPECS();

  uint8_t idx = GAB_SEND_HASH(t) * GAB_SEND_CACHE_SIZE;

  // TODO: Handle undefined and record case
  if (__gab_unlikely(ks[GAB_SEND_KTYPE + idx] != t))
    MISS_CACHED_SEND();

  struct gab_oblock *blk = (void *)ks[GAB_SEND_KSPEC + idx];

  PUSH_FRAME(blk, have);

  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);

  if (__gab_unlikely(!has_stackspace(SP(), SB(), p->nslots - have)))
    VM_PANIC(GAB_OVERFLOW, "");

  IP() = (void *)ks[GAB_SEND_KOFFSET + idx];
  FB() = SP() - have;

  SET_VAR(have);

  NEXT();
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

  ks[GAB_SEND_KSPECS] = specs;
  return true;
}

CASE_CODE(LOAD_UPVALUE) {
  uint64_t have = VAR();

  PUSH(UPVALUE(READ_BYTE));

  VAR() = have + 1;

  NEXT();
}

CASE_CODE(NLOAD_UPVALUE) {
  uint8_t n = READ_BYTE;

  VM_PANIC_GUARD_STACKSPACE(n);

  uint64_t have = VAR();

  SP()[n] = have + n;

  while (n--)
    PUSH(UPVALUE(READ_BYTE));

  NEXT();
}

CASE_CODE(LOAD_LOCAL) {
  uint64_t have = VAR();

  PUSH(LOCAL(READ_BYTE));

  VAR() = have + 1;

  NEXT();
}

CASE_CODE(NLOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  VM_PANIC_GUARD_STACKSPACE(n);

  uint64_t have = VAR();
  SP()[n] = have + n;

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  NEXT();
}

CASE_CODE(STORE_LOCAL) {
  LOCAL(READ_BYTE) = PEEK();

  NEXT();
}

CASE_CODE(NPOPSTORE_STORE_LOCAL) {
  uint8_t n = READ_BYTE - 1;

  while (n--)
    LOCAL(READ_BYTE) = POP();

  LOCAL(READ_BYTE) = PEEK();
  VAR() = 1;

  NEXT();
}

CASE_CODE(POPSTORE_LOCAL) {
  LOCAL(READ_BYTE) = POP();
  VAR() = 0;

  NEXT();
}

CASE_CODE(NPOPSTORE_LOCAL) {
  uint8_t n = READ_BYTE;

  while (n--)
    LOCAL(READ_BYTE) = POP();

  VAR() = 0;

  NEXT();
}

CASE_CODE(SEND_NATIVE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_onative *n = (void *)ks[GAB_SEND_KSPEC];

  CALL_NATIVE(n, have, below_have, true);
}

CASE_CODE(SEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = (void *)ks[GAB_SEND_KSPEC];

  CALL_BLOCK(b, have);
}

CASE_CODE(TAILSEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = (void *)ks[GAB_SEND_KSPEC];

  TAILCALL_BLOCK(b, have);
}

CASE_CODE(LOCALSEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = (void *)ks[GAB_SEND_KSPEC];

  LOCALCALL_BLOCK(b, have);
}

CASE_CODE(LOCALTAILSEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = (void *)ks[GAB_SEND_KSPEC];

  LOCALTAILCALL_BLOCK(b, have);
}

CASE_CODE(SEND_PRIMITIVE_CALL_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  VM_PANIC_GUARD_KIND(r, kGAB_BLOCK);

  struct gab_oblock *blk = GAB_VAL_TO_BLOCK(r);

  CALL_BLOCK(blk, have);
}

CASE_CODE(TAILSEND_PRIMITIVE_CALL_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  VM_PANIC_GUARD_KIND(r, kGAB_BLOCK);

  struct gab_oblock *blk = GAB_VAL_TO_BLOCK(r);

  TAILCALL_BLOCK(blk, have);
}

CASE_CODE(SEND_PRIMITIVE_CALL_NATIVE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  VM_PANIC_GUARD_KIND(r, kGAB_NATIVE);

  struct gab_onative *n = GAB_VAL_TO_NATIVE(r);

  CALL_NATIVE(n, have, below_have, false);
}

IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_ADD, gab_number, gab_float, gab_valtof, +);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_SUB, gab_number, gab_float, gab_valtof, -);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_MUL, gab_number, gab_float, gab_valtof, *);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_DIV, gab_number, gab_float, gab_valtof, /);

IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_LT, gab_bool, gab_float, gab_valtof, <);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_LTE, gab_bool, gab_float, gab_valtof, <=);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_GT, gab_bool, gab_float, gab_valtof, >);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_GTE, gab_bool, gab_float, gab_valtof, >=);

IMPL_SEND_UNARY_NUMERIC(PRIMITIVE_BIN, gab_number, gab_int, gab_valtoi, ~);

IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_BOR, gab_number, gab_int, gab_valtoi, |);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_BND, gab_number, gab_int, gab_valtoi, &);

IMPL_SEND_BINARY_SHIFT_NUMERIC(PRIMITIVE_LSH, <<, >>);
IMPL_SEND_BINARY_SHIFT_NUMERIC(PRIMITIVE_RSH, >>, <<);

IMPL_SEND_UNARY_BOOLEAN(PRIMITIVE_LIN, gab_bool, bool, !);

// Implemented with binary or/and, because there is no short-circuiting
// necessary
IMPL_SEND_BINARY_BOOLEAN(PRIMITIVE_LOR, gab_bool, bool, |);
IMPL_SEND_BINARY_BOOLEAN(PRIMITIVE_LND, gab_bool, bool, &);

CASE_CODE(SEND_PRIMITIVE_MOD) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  if (__gab_unlikely(have < 2))
    PUSH(gab_nil), have++;

  VM_PANIC_GUARD_ISN(PEEK_N(have));
  VM_PANIC_GUARD_ISN(PEEK_N(have - 1));

  gab_int val_b = gab_valtoi(PEEK_N(have - 1));
  gab_int val_a = gab_valtoi(PEEK_N(have));

  DROP_N(have + 1);

  if (__gab_unlikely(val_b == 0))
    PUSH(gab_number(0.0 / 0.0));
  else
    PUSH(gab_number(val_a % val_b));

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_EQ) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  if (__gab_unlikely(have < 2))
    PUSH(gab_nil), have++;

  gab_value a = PEEK_N(have);
  gab_value b = PEEK_N(have - 1);

  DROP_N(have + 1);
  PUSH(gab_bool(gab_valeq(a, b)));

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONCAT) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  if (__gab_unlikely(have < 2))
    PUSH(gab_nil), have++;

  VM_PANIC_GUARD_ISS(PEEK_N(have));
  VM_PANIC_GUARD_ISS(PEEK_N(have - 1));

  gab_value val_a = PEEK_N(have);
  gab_value val_b = PEEK_N(have - 1);

  STORE_SP();
  gab_value val_ab = gab_strcat(GAB(), val_a, val_b);

  DROP_N(have + 1);
  PUSH(val_ab);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_USE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SEND_GUARD_KIND(r, kGAB_STRING);

  STORE();

  /*
   * This pulling in of args/values to pass on to use'd module
   * is a little scuffed. I'd rather do it a different way.
   */
  gab_value shp = gab_prtshp(BLOCK()->p);
  gab_value svargs[32];
  const char *sargs[32];

  size_t len = gab_shplen(shp);
  assert(len < 32);
  for (int i = 0; i < len; i++) {
    svargs[i] = gab_ushpat(shp, i);
    sargs[i] = gab_strdata(svargs + i);
  }

  union gab_value_pair mod =
      gab_use(GAB(), (struct gab_use_argt){
                         .vname = r,
                         .len = BLOCK_PROTO()->narguments,
                         .argv = FB() + 1, // To skip self local
                         .sargv = sargs,
                     });

  DROP_N(have + 1);

  if (mod.status != gab_cvalid)
    VM_GIVEN(mod);

  if (mod.aresult->data[0] != gab_ok)
    VM_GIVEN(mod);

  for (uint64_t i = 1; i < mod.aresult->len; i++)
    PUSH(mod.aresult->data[i]);

  SET_VAR(below_have + mod.aresult->len - 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  if (__gab_unlikely(have < 2))
    SET_VAR(below_have + 1), NEXT();

  gab_value a = PEEK_N(have);
  gab_value b = PEEK_N(have - 1);

  STORE_SP();
  gab_value res = gab_listof(GAB(), a, b);

  DROP_N(have + 1);
  PUSH(res);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS_RECORD) {
  SKIP_SHORT;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  if (__gab_unlikely(have < 2))
    SET_VAR(below_have + 1), NEXT();

  gab_value arg = PEEK_N(have - 1);

  STORE_SP();
  gab_value res = gab_lstpush(GAB(), r, arg);

  DROP_N(have + 1);
  PUSH(res);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATSHAPE) {
  SKIP_SHORT; // Constants
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value s = PEEK_N(have);

  SEND_GUARD_ISSHP(s);

  DROP_N(have + 1);

  uint64_t len = gab_shplen(s);

  VM_PANIC_GUARD_STACKSPACE(len);

  for (uint64_t i = 0; i < len; i++)
    PUSH(gab_ushpat(s, i));

  SET_VAR(below_have + len);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATLIST) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  DROP_N(have + 1);

  uint64_t len = gab_reclen(r);

  VM_PANIC_GUARD_STACKSPACE(len);

  for (uint64_t i = 0; i < len; i++)
    PUSH(gab_uvrecat(r, i));

  SET_VAR(below_have + len);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATDICT) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  DROP_N(have + 1);

  uint64_t len = gab_reclen(r);

  VM_PANIC_GUARD_STACKSPACE(len * 2);

  for (uint64_t i = 0; i < len; i++)
    PUSH(gab_ukrecat(r, i)), PUSH(gab_uvrecat(r, i));

  SET_VAR(below_have + len * 2);

  NEXT();
}

CASE_CODE(SEND_CONSTANT) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  gab_value spec = ks[GAB_SEND_KSPEC];

  DROP_N(have + 1);
  PUSH(spec);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PROPERTY) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_RECORD);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  PROPERTY_RECORD(r, have, below_have);
}

CASE_CODE(RETURN) {
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = FB()[-4];

  gab_value *from = SP() - have;
  gab_value *to = FB() - 4;
  // The target 'to' ptr should be the len of our consing tuple (below_have)
  assert(*to == below_have);

  if (__gab_unlikely(RETURN_FB() == nullptr))
    return STORE(), SET_VAR(have), vm_ok(DISPATCH_ARGS());

  assert(RETURN_IP() != nullptr);

  LOAD_FRAME();
  memmove(to, from, have * sizeof(gab_value));
  SP() = to + have;
  SET_VAR(have + below_have);

  assert(FB() >= VM()->sb + 3);
  assert(BLOCK()->header.kind == kGAB_BLOCK);
  assert(BLOCK_PROTO()->header.kind == kGAB_PROTOTYPE);

  NEXT();
}

CASE_CODE(NOP) { NEXT(); }

CASE_CODE(CONSTANT) {
  uint64_t have = VAR();

  PUSH(READ_CONSTANT);

  VAR() = have + 1;

  NEXT();
}

CASE_CODE(NCONSTANT) {
  uint8_t n = READ_BYTE;

  VM_PANIC_GUARD_STACKSPACE(n);

  uint64_t have = VAR();

  SP()[n] = have + n;

  while (n--)
    PUSH(READ_CONSTANT);

  NEXT();
}

CASE_CODE(POP) {
  DROP();
  VAR() = 0;
  NEXT();
}

CASE_CODE(POP_N) {
  DROP_N(READ_BYTE);
  VAR() = 0;
  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_TYPE) {
  SKIP_SHORT;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value type = gab_valtype(GAB(), PEEK_N(have));

  DROP_N(have + 1);
  PUSH(type);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(BLOCK) {
  gab_value p = READ_CONSTANT;
  uint64_t have = VAR();

  STORE_SP();

  gab_value blk = block(GAB(), p, FB(), BLOCK()->upvalues);

  PUSH(blk);

  VAR() = have + 1;

  NEXT();
}

CASE_CODE(TUPLE) {
  uint64_t have = VAR();

  PUSH(have);

  VAR() = 0;

  NEXT();
}

CASE_CODE(CONS) {
  uint64_t have = VAR();
  uint64_t below_have = PEEK_N(have + 1);

  // Move the given tuple down one.
  memmove(SP() - (have + 1), SP() - have, have * sizeof(gab_value));

  // Drop the tuple len for the below tuple.
  DROP();

  // Combine length of each tuple.
  VAR() = have + below_have;

  NEXT();
}

TRIM_N(0)
TRIM_N(1)
TRIM_N(2)
TRIM_N(3)
TRIM_N(4)
TRIM_N(5)
TRIM_N(6)
TRIM_N(7)
TRIM_N(8)
TRIM_N(9)

CASE_CODE(TRIM) {
  uint8_t want = READ_BYTE;
  uint64_t have = VAR();
  uint64_t nulls = 0;

  if (have == want && want < 10) {
    WRITE_BYTE(2, OP_TRIM_EXACTLY0 + want);

    IP() -= 2;

    NEXT();
  }

  if (have > want && have - want < 10) {
    WRITE_BYTE(2, OP_TRIM_DOWN1 - 1 + (have - want));

    IP() -= 2;

    NEXT();
  }

  if (want > have && want - have < 10) {
    WRITE_BYTE(2, OP_TRIM_UP1 - 1 + (want - have));

    IP() -= 2;

    NEXT();
  }

  SP() -= have;

  if (__gab_unlikely(have != want && want != VAR_EXP)) {
    if (have > want) {
      have = want;
    } else {
      nulls = want - have;
    }
  }

  SP() += have + nulls;

  while (nulls--)
    PEEK_N(nulls + 1) = gab_nil;

  VAR() = 0;

  NEXT();
}

CASE_CODE(PACK_RECORD) {
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint8_t below = READ_BYTE;
  uint8_t above = READ_BYTE;

  uint64_t want = below + above;

  while (have < want)
    PUSH(gab_nil), have++;

  assert(have >= want);
  int64_t len = have - want;

  gab_value *ap = SP() - above;

  STORE_SP();

  gab_value rec = gab_record(GAB(), 2, len / 2, ap - len, ap - len + 1);

  DROP_N(len - 1);

  memmove(ap - len + 1, ap, above * sizeof(gab_value));

  PEEK_N(above + 1) = rec;

  SET_VAR(want + 1);

  NEXT();
}
CASE_CODE(PACK_LIST) {
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint8_t below = READ_BYTE;
  uint8_t above = READ_BYTE;

  uint64_t want = below + above;

  while (have < want)
    PUSH(gab_nil), have++;

  assert(have >= want);
  int64_t len = have - want;

  gab_value *ap = SP() - above;

  STORE_SP();

  gab_value rec = gab_list(GAB(), len, ap - len);

  DROP_N(len - 1);

  memmove(ap - len + 1, ap, above * sizeof(gab_value));

  PEEK_N(above + 1) = rec;

  SET_VAR(want + 1);

  NEXT();
}

CASE_CODE(SEND) {
  gab_value *ks = READ_CONSTANTS;
  uint8_t have_byte = READ_BYTE;
  uint64_t have = COMPUTE_TUPLE(have_byte);

  uint8_t adjust = (have_byte & fHAVE_TAIL) >> 1;

  gab_value r = PEEK_N(have);
  gab_value m = ks[GAB_SEND_KMESSAGE];

  if (BLOCK() && try_setup_localmatch(GAB(), m, ks, BLOCK_PROTO())) {
    WRITE_BYTE(SEND_CACHE_DIST, OP_MATCHSEND_BLOCK + adjust);
    IP() -= SEND_CACHE_DIST;
    NEXT();
  }

  /* Do the expensive lookup */
  struct gab_impl_rest res = gab_impl(GAB(), m, r);

  if (__gab_unlikely(!res.status))
    VM_PANIC(GAB_SPECIALIZATION_MISSING, FMT_MISSINGIMPL, m, r,
             gab_valtype(GAB(), r));

  gab_value spec = res.status == kGAB_IMPL_PROPERTY
                       ? gab_primitive(OP_SEND_PROPERTY)
                       : res.as.spec;

  ks[GAB_SEND_KSPECS] = gab_thisfibmsgrec(GAB(), m);
  ks[GAB_SEND_KTYPE] = gab_valtype(GAB(), r);
  ks[GAB_SEND_KSPEC] = res.as.spec;

  switch (gab_valkind(spec)) {
  case kGAB_PRIMITIVE: {
    uint8_t op = gab_valtop(spec);

    if (op == OP_SEND_PRIMITIVE_CALL_BLOCK)
      op += adjust;

    WRITE_BYTE(SEND_CACHE_DIST, op);

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

    ks[GAB_SEND_KSPEC] = (intptr_t)b;
    WRITE_BYTE(SEND_CACHE_DIST, OP_SEND_BLOCK + adjust);

    break;
  }
  case kGAB_NATIVE: {
    struct gab_onative *n = GAB_VAL_TO_NATIVE(spec);

    ks[GAB_SEND_KSPEC] = (intptr_t)n;
    WRITE_BYTE(SEND_CACHE_DIST, OP_SEND_NATIVE);

    break;
  }
  default:
    ks[GAB_SEND_KSPEC] = spec;
    WRITE_BYTE(SEND_CACHE_DIST, OP_SEND_CONSTANT);
    break;
  }

  IP() -= SEND_CACHE_DIST;

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE_PROPERTY) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  memmove(SP() - (have), SP() - (have - 1), (have - 1) * sizeof(gab_value));
  have--;
  DROP();

  PROPERTY_RECORD(r, have, below_have);
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  memmove(SP() - (have), SP() - (have - 1), (have - 1) * sizeof(gab_value));
  have--;
  DROP();

  struct gab_oblock *spec = (void *)ks[GAB_SEND_KSPEC];

  CALL_BLOCK(spec, have);
}

CASE_CODE(TAILSEND_PRIMITIVE_CALL_MESSAGE_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  memmove(SP() - (have), SP() - (have - 1), (have - 1) * sizeof(gab_value));
  have--;
  DROP();

  struct gab_oblock *spec = (void *)ks[GAB_SEND_KSPEC];

  TAILCALL_BLOCK(spec, have);
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE_NATIVE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  memmove(SP() - (have), SP() - (have - 1), (have - 1) * sizeof(gab_value));
  have--;
  DROP();

  struct gab_onative *spec = (void *)ks[GAB_SEND_KSPEC];

  CALL_NATIVE(spec, have, below_have, true);
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE_PRIMITIVE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  memmove(SP() - (have), SP() - (have - 1), (have - 1) * sizeof(gab_value));
  PEEK() = gab_nil;

  uint8_t spec = ks[GAB_SEND_KSPEC];

  uint8_t op = gab_valtop(spec);

  IP() -= SEND_CACHE_DIST - 1;

  DISPATCH(op);
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE_CONSTANT) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  gab_value spec = ks[GAB_SEND_KSPEC];

  DROP_N(have + 1);
  PUSH(spec);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE) {
  gab_value *ks = READ_CONSTANTS;
  uint8_t have_byte = READ_BYTE;
  uint64_t have = COMPUTE_TUPLE(have_byte);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);
  gab_value t = gab_valtype(GAB(), r);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);

  struct gab_impl_rest res = gab_impl(GAB(), m, r);

  if (__gab_unlikely(!res.status)) {
    STORE();
    VM_PANIC(GAB_SPECIALIZATION_MISSING, FMT_MISSINGIMPL, m, r,
             gab_valtype(GAB(), r));
  }

  ks[GAB_SEND_KTYPE] = t;
  ks[GAB_SEND_KSPEC] = res.as.spec;
  ks[GAB_SEND_KGENERIC_CALL_SPECS] = gab_thisfibmsgrec(GAB(), m);
  // ks[GAB_SEND_KGENERIC_CALL_MESSAGE] = m;

  if (res.status == kGAB_IMPL_PROPERTY) {
    WRITE_BYTE(SEND_CACHE_DIST, OP_SEND_PRIMITIVE_CALL_MESSAGE_PROPERTY);
  } else {
    gab_value spec = res.as.spec;

    switch (gab_valkind(spec)) {
    case kGAB_PRIMITIVE: {
      ks[GAB_SEND_KSPEC] = gab_valtop(spec);

      WRITE_BYTE(SEND_CACHE_DIST, OP_SEND_PRIMITIVE_CALL_MESSAGE_PRIMITIVE);
      break;
    }
    case kGAB_BLOCK: {
      ks[GAB_SEND_KSPEC] = (uintptr_t)GAB_VAL_TO_BLOCK(spec);

      uint8_t adjust = (have_byte & fHAVE_TAIL) >> 1;

      WRITE_BYTE(SEND_CACHE_DIST,
                 OP_SEND_PRIMITIVE_CALL_MESSAGE_BLOCK + adjust);
      break;
    }
    case kGAB_NATIVE: {
      ks[GAB_SEND_KSPEC] = (uintptr_t)GAB_VAL_TO_NATIVE(spec);

      WRITE_BYTE(SEND_CACHE_DIST, OP_SEND_PRIMITIVE_CALL_MESSAGE_NATIVE);
      break;
    }
    default: {
      ks[GAB_SEND_KSPEC] = spec;

      WRITE_BYTE(SEND_CACHE_DIST, OP_SEND_PRIMITIVE_CALL_MESSAGE_CONSTANT);
      break;
    }
    }
  }

  IP() -= SEND_CACHE_DIST;
  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_TAKE) {
  SKIP_SHORT;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value c = PEEK_N(have);

  SEND_GUARD_ISC(c);

  STORE_SP();

  // TODO: Fix the hardcoding of len here
  assert(has_stackspace(SP(), SB(), cGAB_FRAMES_MAX));
  gab_value v = gab_ntchntake(GAB(), c, cGAB_FRAMES_MAX, SP() - have,
                              cGAB_VM_CHANNEL_TAKE_TIMEOUT_MS);

  switch (v) {
  case gab_ctimeout:
    VM_YIELD();
  case gab_cinvalid:
    VM_TERM();
  case gab_cundefined:
    DROP_N(have + 1);
    PUSH(gab_none);

    SET_VAR(below_have + 1);
    NEXT();
  default:
    gab_int i = gab_valtoi(v);

    if (i < 0)
      VM_PANIC(GAB_PANIC, "Channel take failed, insufficient stack space.");

    PEEK_N(have + 1) = gab_ok;
    SP() += (gab_valtoi(v) - (int64_t)have);

    SET_VAR(below_have + gab_valtou(v) + 1);
    NEXT();
  }
}

CASE_CODE(SEND_PRIMITIVE_PUT) {
  SKIP_SHORT;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  gab_value c = PEEK_N(have);

  SEND_GUARD_ISC(c);

  STORE_SP();

  // All values *but* the channel are put into the channel.
  gab_value r = gab_ntchnput(GAB(), c, have - 1, SP() - (have - 1),
                             cGAB_VM_CHANNEL_PUT_TIMEOUT_MS);

  switch (r) {
  case gab_ctimeout:
    VM_YIELD();
  case gab_cinvalid:
    VM_TERM();
  default:
    gab_value ch = PEEK_N(have);
    // Return the channel

    DROP_N(have + 1);
    PUSH(ch);

    SET_VAR(below_have + 1);

    NEXT();
  }
}

CASE_CODE(SEND_PRIMITIVE_FIBER) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  gab_value block = gab_nil;

  if (have >= 2)
    block = PEEK_N(have - 1);

  VM_PANIC_GUARD_KIND(block, kGAB_BLOCK);

  STORE_SP();

  union gab_value_pair fb = gab_tarun(GAB(), cGAB_VM_CHANNEL_PUT_TIMEOUT_MS,
                                      (struct gab_run_argt){
                                          .flags = GAB().flags,
                                          .main = block,
                                      });

  if (fb.status == gab_ctimeout)
    VM_YIELD();

  assert(fb.status == gab_cvalid);

  DROP_N(have + 1);
  PUSH(fb.vresult);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CHANNEL) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  STORE_SP();
  gab_value chan = gab_channel(GAB());

  DROP_N(have + 1);
  PUSH(chan);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_RECORD) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  if (__gab_unlikely(len % 2 == 1))
    PUSH(gab_nil), len++, have++; // Should we just error here?

  STORE_SP();
  gab_value record = gab_record(GAB(), 2, len / 2, SP() - len, SP() + 1 - len);

  DROP_N(have + 1);
  PUSH(record);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_MAKE_SHAPE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  gab_value shape = PEEK_N(have);
  uint64_t len = have - 1;

  if (__gab_unlikely(gab_shplen(shape) != len))
    VM_PANIC(GAB_PANIC, "Expected $ arguments, got $",
             gab_number(gab_shplen(shape)), gab_number(len));

  STORE_SP();
  gab_value record = gab_recordfrom(GAB(), shape, 1, len, SP() - len, nullptr);

  DROP_N(have + 1);
  PUSH(record);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SHAPE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  STORE_SP();
  gab_value shape = gab_shape(GAB(), 1, len, SP() - len, nullptr);

  DROP_N(have + 1);
  PUSH(shape);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_LIST) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = COMPUTE_TUPLE(READ_BYTE);

  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  STORE_SP();
  gab_value rec = gab_list(GAB(), len, SP() - len);

  DROP_N(have + 1);
  PUSH(rec);

  SET_VAR(below_have + 1);

  NEXT();
}

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef IMPL_SEND_BINARY_PRIMITIVE
