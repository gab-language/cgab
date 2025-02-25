#include "core.h"
#include "gab.h"

#define GAB_STATUS_NAMES_IMPL
#include "engine.h"

#include "colors.h"
#include "lexer.h"

#include <stdarg.h>
#include <string.h>

#define OP_HANDLER_ARGS                                                        \
  struct gab_triple __gab, uint8_t *__ip, gab_value *__kb, gab_value *__fb,    \
      gab_value *__sp

typedef a_gab_value *(*handler)(OP_HANDLER_ARGS);

// Forward declare all our opcode handlers
#define OP_CODE(name) a_gab_value *OP_##name##_HANDLER(OP_HANDLER_ARGS);
#include "bytecode.h"
#undef OP_CODE

// Plop them all in an array
static handler handlers[] = {
#define OP_CODE(name) OP_##name##_HANDLER,
#include "bytecode.h"
#undef OP_CODE
};

#if cGAB_LOG_VM
#define LOG(op) printf("OP_%s: %p\n", gab_opcode_names[op], handlers[op]);
#else
#define LOG(op)
#endif

#define ATTRIBUTES

#define CASE_CODE(name)                                                        \
  ATTRIBUTES a_gab_value *OP_##name##_HANDLER(OP_HANDLER_ARGS)

#define DISPATCH_ARGS() GAB(), IP(), KB(), FB(), SP()

#define DISPATCH(op)                                                           \
  ({                                                                           \
    if (EG()->sig.schedule == GAB().wkid) {                                    \
      STORE_SP();                                                              \
      switch (gab_yield(GAB())) {                                                \
      case sGAB_TERM:                                                          \
        return nullptr;                                                        \
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

/*
  Lots of helper macros.
*/
#define GAB() (__gab)
#define EG() (GAB().eg)
#define FIBER() (GAB_VAL_TO_FIBER(gab_thisfiber(GAB())))
#define GC() (GAB().eg->gc)
#define VM() (gab_thisvm(GAB()))
#define SET_BLOCK(b) (FB()[-3] = (uintptr_t)(b));
#define BLOCK() ((struct gab_obj_block *)(uintptr_t)FB()[-3])
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

#define IMPL_SEND_UNARY_NUMERIC(CODE, value_type, operation_type, operation)   \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_CONSTANTS;                                            \
    uint64_t have = compute_arity(VAR(), READ_BYTE);                           \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));                             \
    VM_PANIC_GUARD_ISN(PEEK_N(have));                                          \
                                                                               \
    operation_type val = gab_valton(PEEK_N(have));                             \
                                                                               \
    DROP_N(have);                                                              \
    PUSH(value_type(operation val));                                           \
                                                                               \
    SET_VAR(1);                                                                \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_SEND_BINARY_NUMERIC(CODE, value_type, operation_type, operation)  \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_CONSTANTS;                                            \
    uint64_t have = compute_arity(VAR(), READ_BYTE);                           \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));                             \
                                                                               \
    if (__gab_unlikely(have < 2))                                              \
      PUSH(gab_nil), have++;                                                   \
                                                                               \
    VM_PANIC_GUARD_ISN(PEEK_N(have));                                          \
    VM_PANIC_GUARD_ISN(PEEK_N(have - 1));                                      \
                                                                               \
    operation_type val_b = gab_valton(PEEK_N(have - 1));                       \
    operation_type val_a = gab_valton(PEEK_N(have));                           \
                                                                               \
    DROP_N(have);                                                              \
    PUSH(value_type(val_a operation val_b));                                   \
                                                                               \
    SET_VAR(1);                                                                \
                                                                               \
    NEXT();                                                                    \
  }

// FIXME: This doesn't work
// These boolean sends don't work because there is no longer a boolean type.
// There are just sigils
#define IMPL_SEND_UNARY_BOOLEAN(CODE, value_type, operation_type, operation)   \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_CONSTANTS;                                            \
    uint64_t have = compute_arity(VAR(), READ_BYTE);                           \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));                             \
                                                                               \
    VM_PANIC_GUARD_ISB(PEEK_N(have));                                          \
                                                                               \
    operation_type val = gab_valintob(PEEK_N(have));                           \
                                                                               \
    DROP_N(have);                                                              \
    PUSH(value_type(operation val));                                           \
                                                                               \
    SET_VAR(1);                                                                \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_SEND_BINARY_BOOLEAN(CODE, value_type, operation_type, operation)  \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_CONSTANTS;                                            \
    uint64_t have = compute_arity(VAR(), READ_BYTE);                           \
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
    DROP_N(have);                                                              \
    PUSH(value_type(val_a operation val_b));                                   \
                                                                               \
    SET_VAR(1);                                                                \
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
                                                                               \
    NEXT();                                                                    \
  }                                                                            \
  CASE_CODE(TRIM_EXACTLY##n) {                                                 \
    SKIP_BYTE;                                                                 \
                                                                               \
    if (__gab_unlikely(VAR() != n))                                            \
      MISS_CACHED_TRIM();                                                      \
                                                                               \
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
    NEXT();                                                                    \
  }

#define STORE_SP() (VM()->sp = SP())
#define STORE_FP() (VM()->fp = FB())
#define STORE_IP() (VM()->ip = IP())

#define STORE()                                                                \
  ({                                                                           \
    STORE_SP();                                                                \
    STORE_FP();                                                                \
    STORE_IP();                                                                \
  })

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
    STORE_FP();                                                                \
    STORE_SP();                                                                \
    STORE_IP();                                                                \
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
                                      struct gab_obj_prototype *p) {
  assert(gab.wkid != 0);
  return p->src->thread_bytecode[gab.wkid - 1].bytecode;
}

static inline uint8_t *proto_ip(struct gab_triple gab,
                                struct gab_obj_prototype *p) {
  return proto_srcbegin(gab, p) + p->offset;
}

static inline gab_value *proto_ks(struct gab_triple gab,
                                  struct gab_obj_prototype *p) {
  assert(gab.wkid != 0);
  return p->src->thread_bytecode[gab.wkid - 1].constants;
}

static inline gab_value *frame_parent(gab_value *f) { return (void *)f[-1]; }

static inline struct gab_obj_block *frame_block(gab_value *f) {
  return (void *)f[-3];
}

static inline uint8_t *frame_ip(gab_value *f) { return (void *)f[-2]; }

static inline uint64_t compute_token_from_ip(struct gab_triple gab,
                                             struct gab_obj_block *b,
                                             uint8_t *ip) {
  struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

  uint64_t offset = ip - proto_srcbegin(gab, p) - 1;

  uint64_t token = v_uint64_t_val_at(&p->src->bytecode_toks, offset);

  return token;
}

static inline gab_value compute_message_from_ip(struct gab_obj_block *b,
                                                uint8_t *ip) {
  struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

  uint16_t k = ((uint16_t)ip[-3] << 8) | ip[-2];

  gab_value m = v_gab_value_val_at(&p->src->constants, k);

  return m;
}

struct gab_err_argt vm_frame_build_err(struct gab_triple gab,
                                       struct gab_obj_block *b, uint8_t *ip,
                                       bool has_parent, enum gab_status s,
                                       const char *fmt) {

  gab_value message = has_parent ? compute_message_from_ip(b, ip) : gab_nil;

  if (b) {
    struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

    uint64_t tok = compute_token_from_ip(gab, b, ip);

    return (struct gab_err_argt){
        .tok = tok,
        .src = p->src,
        .note_fmt = fmt,
        .status = s,
        .message = message,
    };
  }

  return (struct gab_err_argt){
      .note_fmt = fmt,
      .status = s,
      .message = message,
  };
}

a_gab_value *vvm_error(struct gab_triple gab, enum gab_status s,
                       const char *fmt, va_list va) {
  gab_value fiber = gab_thisfiber(gab);
  struct gab_vm *vm = gab_thisvm(gab);
  gab_value *f = vm->fp;
  uint8_t *ip = vm->ip;

  while (frame_parent(f) > vm->sb) {
    gab_vfpanic(gab, gab.eg->serr, va,
                vm_frame_build_err(gab, frame_block(f), ip,
                                   frame_parent(f) > vm->sb, GAB_NONE, ""));

    ip = frame_ip(f);
    f = frame_parent(f);
  }

  gab_vfpanic(gab, gab.eg->serr, va,
              vm_frame_build_err(gab, frame_block(f), ip, false, s, fmt));

  gab_value results[] = {
      gab_message(gab, "err"),
      gab_string(gab, gab_status_names[s]),
      gab_fb(gab),
  };

  gab_niref(gab, 1, 2, results);

  a_gab_value *res =
      a_gab_value_create(results, sizeof(results) / sizeof(gab_value));

  gab_niref(gab, 1, res->len, res->data);

  gab_value p = frame_block(vm->fp)->p;
  gab_value shape = GAB_VAL_TO_PROTOTYPE(p)->s;

  assert(GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERRUNNING);
  GAB_VAL_TO_FIBER(fiber)->res_values = res;
  GAB_VAL_TO_FIBER(fiber)->res_env = gab_recordfrom(gab, shape, 1, vm->fp);
  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERDONE;

  return res;
}

a_gab_value *vm_error(struct gab_triple gab, enum gab_status s, const char *fmt,
                      ...) {
  va_list va;
  va_start(va, fmt);

  a_gab_value *res = vvm_error(gab, s, fmt, va);

  va_end(va);

  return res;
}

#define FMT_TYPEMISMATCH                                                       \
  "found:\n\n >> $\n\nof type:\n\n >> $\n\nbut expected type:\n\n >> $"

#define FMT_MISSINGIMPL                                                        \
  "$ does not specialize for:\n\n >> $\n\nof type:\n\n >> $"

/*a_gab_value *gab_nfpanic(struct gab_triple gab, const char *fmt, uint64_t
 * argc, gab_value argv[static argc]) {*/
/*  if (!gab_vm(gab)) {*/
/*}*/

a_gab_value *gab_fpanic(struct gab_triple gab, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  if (!gab_thisvm(gab)) {
    gab_vfpanic(gab, stderr, va,
                (struct gab_err_argt){
                    .status = GAB_PANIC,
                    .note_fmt = fmt,
                });

    va_end(va);

    return nullptr;
  }

  a_gab_value *res = vvm_error(gab, GAB_PANIC, fmt, va);

  va_end(va);

  return res;
}

a_gab_value *gab_ptypemismatch(struct gab_triple gab, gab_value found,
                               gab_value texpected) {

  return vm_error(gab, GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, found,
                  gab_valtype(gab, found), texpected);
}

gab_value gab_vmframe(struct gab_triple gab, uint64_t depth) {
  // uint64_t frame_count = gab_vm(gab)->fp - gab_vm(gab)->sb;
  //
  // if (depth >= frame_count)
  return gab_undefined;

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
  // struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(f->b->p);
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

static inline uint64_t compute_arity(uint64_t var, uint8_t have) {
  return var * (have & fHAVE_VAR) + (have >> 2);
}

static inline bool has_callspace(gab_value *sp, gab_value *sb,
                                 uint64_t space_needed) {
  if ((sp - sb) + space_needed + 3 >= cGAB_STACK_MAX) {
    return false;
  }

  return true;
}

inline uint64_t gab_nvmpush(struct gab_vm *vm, uint64_t argc,
                            gab_value argv[argc]) {
  if (__gab_unlikely(argc == 0 || !has_callspace(vm->sp, vm->sb, argc))) {
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

#define CALL_BLOCK(blk, have)                                                  \
  ({                                                                           \
    struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                \
                                                                               \
    if (__gab_unlikely(!has_callspace(SP(), SB(), p->nslots - have)))          \
      VM_PANIC(GAB_OVERFLOW, "");                                              \
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
    struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                \
                                                                               \
    if (__gab_unlikely(!has_callspace(SP(), SB(), 3 + p->nslots - have)))      \
      VM_PANIC(GAB_OVERFLOW, "");                                              \
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
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    memmove(to, from, have * sizeof(gab_value));                               \
    SP() = to + have;                                                          \
                                                                               \
    struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                \
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

#define PROPERTY_RECORD(r, have)                                               \
  ({                                                                           \
    switch (have) {                                                            \
    case 1:                                                                    \
      PEEK() = gab_uvrecat(r, ks[GAB_SEND_KSPEC]);                             \
      break;                                                                   \
    default:                                                                   \
      DROP_N((have) - 1);                                                      \
    case 2: {                                                                  \
      STORE_SP();                                                              \
      gab_value value = gab_urecput(GAB(), r, ks[GAB_SEND_KSPEC], PEEK());     \
      DROP();                                                                  \
      PEEK() = value;                                                          \
      break;                                                                   \
    }                                                                          \
    }                                                                          \
                                                                               \
    SET_VAR(1);                                                                \
                                                                               \
    NEXT();                                                                    \
  })

#define CALL_NATIVE(native, have, message)                                     \
  ({                                                                           \
    STORE();                                                                   \
                                                                               \
    gab_value *to = SP() - have;                                               \
                                                                               \
    gab_value *before = SP();                                                  \
                                                                               \
    uint64_t pass = message ? have : have - 1;                                 \
                                                                               \
    a_gab_value *res = (*native->function)(GAB(), pass, SP() - pass);          \
                                                                               \
    if (__gab_unlikely(res))                                                   \
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
    SET_VAR(have);                                                             \
                                                                               \
    NEXT();                                                                    \
  })

static inline gab_value block(struct gab_triple gab, gab_value p,
                              gab_value *locals, gab_value *upvs) {
  gab_value blk = gab_block(gab, p);

  struct gab_obj_block *b = GAB_VAL_TO_BLOCK(blk);
  struct gab_obj_prototype *proto = GAB_VAL_TO_PROTOTYPE(p);

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

a_gab_value *ok(OP_HANDLER_ARGS) {
  uint64_t have = *VM()->sp;
  gab_value *from = VM()->sp - have;

  a_gab_value *results = a_gab_value_empty(have + 1);
  results->data[0] = gab_ok;
  memcpy(results->data + 1, from, have * sizeof(gab_value));

  gab_niref(GAB(), 1, results->len, results->data);
  gab_negkeep(EG(), results->len, results->data);

  VM()->sp = VM()->sb;

  gab_value p = frame_block(VM()->fp)->p;
  gab_value shape = GAB_VAL_TO_PROTOTYPE(p)->s;

  gab_value env = gab_recordfrom(GAB(), shape, 1, VM()->fp);
  gab_egkeep(EG(), gab_iref(GAB(), env));

  assert(FIBER()->header.kind = kGAB_FIBERRUNNING);
  FIBER()->res_values = results;
  FIBER()->res_env = env;
  FIBER()->header.kind = kGAB_FIBERDONE;

  return results;
}

a_gab_value *do_vmexecfiber(struct gab_triple gab, gab_value f,
                            struct gab_impl_rest res) {
  assert(gab_valkind(f) == kGAB_FIBER);
  struct gab_obj_fiber *fiber = GAB_VAL_TO_FIBER(f);

  assert(*fiber->vm.sp > 0);

  gab_value message = fiber->data[0];
  gab_value receiver = fiber->data[1];

  assert(fiber->header.kind != kGAB_FIBERDONE);
  if (res.status == kGAB_IMPL_NONE)
    return fiber->header.kind = kGAB_FIBERDONE, nullptr;

  assert(fiber->header.kind != kGAB_FIBERDONE);
  if (res.status == kGAB_IMPL_PROPERTY)
    return fiber->header.kind = kGAB_FIBERDONE, nullptr;

  switch (gab_valkind(res.as.spec)) {
  case kGAB_PRIMITIVE: {
    struct gab_vm *vm = &fiber->vm;
    uint8_t op = gab_valtop(res.as.spec);

    uint8_t ip[] = {0, 0, 3, OP_RETURN, 1};
    gab_value ks[] = {
        message, fiber->messages, gab_valtype(gab, receiver), res.as.spec, 0, 0,
        0,
    };

    assert(op == OP_SEND_PRIMITIVE_CALL_BLOCK);
    if (op == OP_SEND_PRIMITIVE_CALL_BLOCK)
      op = OP_TAILSEND_PRIMITIVE_CALL_BLOCK;

    assert(fiber->header.kind != kGAB_FIBERDONE);
    fiber->header.kind = kGAB_FIBERRUNNING;

    assert((*vm->sp) > 0);
    return handlers[op](gab, ip, ks, vm->fp, vm->sp);
  }
  case kGAB_NATIVE: {
    struct gab_vm *vm = &fiber->vm;

    uint8_t ip[] = {0, 0, 1, OP_RETURN, 1};
    gab_value ks[] = {
        message,
        fiber->messages,
        gab_valtype(gab, receiver),
        (uintptr_t)GAB_VAL_TO_NATIVE(res.as.spec),
        0,
        0,
        0,
    };

    assert(fiber->header.kind != kGAB_FIBERDONE);
    fiber->header.kind = kGAB_FIBERRUNNING;
    return OP_SEND_NATIVE_HANDLER(gab, ip, ks, vm->fp, vm->sp);
  }
  case kGAB_BLOCK: {
    struct gab_vm *vm = &fiber->vm;

    struct gab_obj_block *b = GAB_VAL_TO_BLOCK(res.as.spec);
    struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

    vm->ip = proto_ip(gab, p);
    uint8_t *ip = vm->ip;
    uint8_t op = *ip++;

    assert(fiber->header.kind != kGAB_FIBERDONE);
    fiber->header.kind = kGAB_FIBERRUNNING;
    return handlers[op](gab, ip, p->src->constants.data, vm->fp, vm->sp);
  }
  default: {
    a_gab_value *results =
        a_gab_value_create((gab_value[]){gab_ok, res.as.spec}, 2);

    fiber->res_values = results;

    assert(fiber->header.kind != kGAB_FIBERDONE);
    fiber->header.kind = kGAB_FIBERDONE;

    return results;
  }
  }
};

a_gab_value *gab_vmexec(struct gab_triple gab, gab_value f) {
  assert(gab_valkind(f) == kGAB_FIBER);
  struct gab_obj_fiber *fiber = GAB_VAL_TO_FIBER(f);

  gab_value receiver = fiber->data[1];
  gab_value message = fiber->data[0];

  gab.flags |= fiber->flags;

  struct gab_impl_rest res = gab_impl(gab, message, receiver);

  return do_vmexecfiber(gab, f, res);
}

#define VM_PANIC_GUARD_KIND(value, kind)                                       \
  if (__gab_unlikely(gab_valkind(value) != kind)) {                            \
    STORE_PRIMITIVE_VM_PANIC_FRAME(1);                                         \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, value,                       \
             gab_valtype(GAB(), value), gab_type(GAB(), kind));                \
  }

#define VM_PANIC_GUARD_ISB(value)                                              \
  if (__gab_unlikely(!__gab_valisb(value))) {                                  \
    STORE_PRIMITIVE_VM_PANIC_FRAME(have);                                      \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, value,                       \
             gab_valtype(GAB(), value), gab_type(GAB(), kGAB_MESSAGE));        \
  }

#define VM_PANIC_GUARD_ISN(value)                                              \
  if (__gab_unlikely(!__gab_valisn(value))) {                                  \
    STORE_PRIMITIVE_VM_PANIC_FRAME(have);                                      \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, value,                       \
             gab_valtype(GAB(), value), gab_type(GAB(), kGAB_NUMBER));         \
  }

#define VM_PANIC_GUARD_ISS(value)                                              \
  if (__gab_unlikely(gab_valkind(value) != kGAB_STRING &&                      \
                     gab_valkind(value) != kGAB_MESSAGE)) {                    \
    STORE_PRIMITIVE_VM_PANIC_FRAME(have);                                      \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, value,                       \
             gab_valtype(GAB(), value), gab_type(GAB(), kGAB_STRING));         \
  }

#define SEND_GUARD(clause)                                                     \
  if (__gab_unlikely(!(clause)))                                               \
  MISS_CACHED_SEND(clause)

#define SEND_GUARD_KIND(r, k) SEND_GUARD(gab_valkind(r) == k)

#define SEND_GUARD_ISC(r)                                                      \
  SEND_GUARD(gab_valkind(c) >= kGAB_CHANNEL &&                                 \
             gab_valkind(c) <= kGAB_CHANNELCLOSED)

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
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);
  gab_value t = gab_valtype(GAB(), r);

  SEND_GUARD_CACHED_MESSAGE_SPECS();

  uint8_t idx = GAB_SEND_HASH(t) * GAB_SEND_CACHE_SIZE;

  // TODO: Handle undefined and record case
  if (__gab_unlikely(ks[GAB_SEND_KTYPE + idx] != t))
    MISS_CACHED_SEND();

  struct gab_obj_block *b = (void *)ks[GAB_SEND_KSPEC + idx];

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
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);
  gab_value t = gab_valtype(GAB(), r);

  SEND_GUARD_CACHED_MESSAGE_SPECS();

  uint8_t idx = GAB_SEND_HASH(t) * GAB_SEND_CACHE_SIZE;

  // TODO: Handle undefined and record case
  if (__gab_unlikely(ks[GAB_SEND_KTYPE + idx] != t))
    MISS_CACHED_SEND();

  struct gab_obj_block *blk = (void *)ks[GAB_SEND_KSPEC + idx];

  PUSH_FRAME(blk, have);

  struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);

  if (__gab_unlikely(!has_callspace(SP(), SB(), p->nslots - have)))
    VM_PANIC(GAB_OVERFLOW, "");

  IP() = (void *)ks[GAB_SEND_KOFFSET + idx];
  FB() = SP() - have;

  SET_VAR(have);

  NEXT();
}

static inline bool try_setup_localmatch(struct gab_triple gab, gab_value m,
                                        gab_value *ks,
                                        struct gab_obj_prototype *p) {
  gab_value specs = gab_thisfibmsgrec(gab, m);

  if (specs == gab_undefined)
    return false;

  if (gab_reclen(specs) > 4 || gab_reclen(specs) < 2)
    return false;

  uint64_t len = gab_reclen(specs);

  for (uint64_t i = 0; i < len; i++) {
    gab_value spec = gab_uvrecat(specs, i);

    if (gab_valkind(spec) != kGAB_BLOCK)
      return false;

    struct gab_obj_block *b = GAB_VAL_TO_BLOCK(spec);
    struct gab_obj_prototype *spec_p = GAB_VAL_TO_PROTOTYPE(b->p);

    if (spec_p->src != p->src)
      return false;

    gab_value t = gab_ukrecat(specs, i);

    uint8_t idx = GAB_SEND_HASH(t) * GAB_SEND_CACHE_SIZE;

    // We have a collision - no point in messing about with this.
    if (ks[GAB_SEND_KSPEC + idx] != gab_undefined)
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
  PUSH(UPVALUE(READ_BYTE));

  NEXT();
}

CASE_CODE(NLOAD_UPVALUE) {
  uint8_t n = READ_BYTE;

  while (n--)
    PUSH(UPVALUE(READ_BYTE));

  NEXT();
}

CASE_CODE(LOAD_LOCAL) {
  PUSH(LOCAL(READ_BYTE));

  NEXT();
}

CASE_CODE(NLOAD_LOCAL) {
  uint8_t n = READ_BYTE;

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

  NEXT();
}

CASE_CODE(POPSTORE_LOCAL) {
  LOCAL(READ_BYTE) = POP();

  NEXT();
}

CASE_CODE(NPOPSTORE_LOCAL) {
  uint8_t n = READ_BYTE;

  while (n--)
    LOCAL(READ_BYTE) = POP();

  NEXT();
}

CASE_CODE(SEND_NATIVE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_obj_native *n = (void *)ks[GAB_SEND_KSPEC];

  CALL_NATIVE(n, have, true);
}

CASE_CODE(SEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_obj_block *b = (void *)ks[GAB_SEND_KSPEC];

  CALL_BLOCK(b, have);
}

CASE_CODE(TAILSEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_obj_block *b = (void *)ks[GAB_SEND_KSPEC];

  TAILCALL_BLOCK(b, have);
}

CASE_CODE(LOCALSEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_obj_block *b = (void *)ks[GAB_SEND_KSPEC];

  LOCALCALL_BLOCK(b, have);
}

CASE_CODE(LOCALTAILSEND_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_obj_block *b = (void *)ks[GAB_SEND_KSPEC];

  LOCALTAILCALL_BLOCK(b, have);
}

CASE_CODE(SEND_PRIMITIVE_CALL_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  VM_PANIC_GUARD_KIND(r, kGAB_BLOCK);

  struct gab_obj_block *blk = GAB_VAL_TO_BLOCK(r);

  CALL_BLOCK(blk, have);
}

CASE_CODE(TAILSEND_PRIMITIVE_CALL_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  VM_PANIC_GUARD_KIND(r, kGAB_BLOCK);

  struct gab_obj_block *blk = GAB_VAL_TO_BLOCK(r);

  TAILCALL_BLOCK(blk, have);
}

CASE_CODE(SEND_PRIMITIVE_CALL_NATIVE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  VM_PANIC_GUARD_KIND(r, kGAB_NATIVE);

  struct gab_obj_native *n = GAB_VAL_TO_NATIVE(r);

  CALL_NATIVE(n, have, false);
}

IMPL_SEND_UNARY_NUMERIC(PRIMITIVE_BIN, gab_number, uint64_t, ~);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_ADD, gab_number, double, +);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_SUB, gab_number, double, -);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_MUL, gab_number, double, *);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_DIV, gab_number, double, /);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_MOD, gab_number, uint64_t, %);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_BOR, gab_number, uint64_t, |);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_BND, gab_number, uint64_t, &);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_LSH, gab_number, uint64_t, <<);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_RSH, gab_number, uint64_t, >>);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_LT, gab_bool, double, <);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_LTE, gab_bool, double, <=);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_GT, gab_bool, double, >);
IMPL_SEND_BINARY_NUMERIC(PRIMITIVE_GTE, gab_bool, double, >=);
IMPL_SEND_UNARY_BOOLEAN(PRIMITIVE_LIN, gab_bool, bool, !);
IMPL_SEND_BINARY_BOOLEAN(PRIMITIVE_LOR, gab_bool, bool, ||);
IMPL_SEND_BINARY_BOOLEAN(PRIMITIVE_LND, gab_bool, bool, &&);

CASE_CODE(SEND_PRIMITIVE_EQ) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  if (__gab_unlikely(have < 2))
    PUSH(gab_nil), have++;

  gab_value a = PEEK_N(have);
  gab_value b = PEEK_N(have - 1);

  DROP_N(have);
  PUSH(gab_bool(gab_valeq(a, b)));

  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONCAT) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  if (__gab_unlikely(have < 2))
    PUSH(gab_nil), have++;

  VM_PANIC_GUARD_ISS(PEEK_N(have));
  VM_PANIC_GUARD_ISS(PEEK_N(have - 1));

  gab_value val_a = PEEK_N(have);
  gab_value val_b = PEEK_N(have - 1);

  STORE_SP();
  gab_value val_ab = gab_strcat(GAB(), val_a, val_b);

  DROP_N(have);
  PUSH(val_ab);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_USE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SEND_GUARD_KIND(r, kGAB_STRING);

  STORE();

  a_gab_value *mod = gab_use(GAB(), r);

  DROP_N(have);

  if (!mod)
    VM_PANIC(GAB_PANIC, "Couldn't locate module $.", r);

  for (uint64_t i = 1; i < mod->len; i++)
    PUSH(mod->data[i]);

  SET_VAR(mod->len - 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  if (__gab_unlikely(have < 2))
    SET_VAR(1), NEXT();

  gab_value a = PEEK_N(have);
  gab_value b = PEEK_N(have - 1);

  STORE_SP();
  gab_value res = (gab_listof(GAB(), a, b));

  DROP_N(have);
  PUSH(res);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS_RECORD) {
  SKIP_SHORT;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  if (__gab_unlikely(have < 2))
    SET_VAR(1), NEXT();

  gab_value arg = PEEK_N(have - 1);

  STORE_SP();
  gab_value res = (gab_lstpush(GAB(), r, arg));

  DROP_N(have);
  PUSH(res);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLAT) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  DROP_N(have);

  uint64_t len = gab_reclen(r);

  assert(VM()->sp + len < VM()->sp + cGAB_STACK_MAX);

  for (uint64_t i = 0; i < len; i++)
    PUSH(gab_uvrecat(r, i));

  SET_VAR(len);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATKEYS) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  DROP_N(have);

  uint64_t len = gab_reclen(r);

  assert(VM()->sp + len < VM()->sp + cGAB_STACK_MAX);

  for (uint64_t i = 0; i < len; i++)
    PUSH(gab_ukrecat(r, i));

  SET_VAR(len);

  NEXT();
}

CASE_CODE(SEND_CONSTANT) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS();
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  gab_value spec = ks[GAB_SEND_KSPEC];

  DROP_N(have);

  PUSH(spec);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PROPERTY) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  PROPERTY_RECORD(r, have);
}

CASE_CODE(RETURN) {
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value *from = SP() - have;
  gab_value *to = FB() - 3;

  if (__gab_unlikely(RETURN_FB() == nullptr))
    return STORE(), SET_VAR(have), ok(DISPATCH_ARGS());

  assert(RETURN_IP() != nullptr);

  LOAD_FRAME();
  memmove(to, from, have * sizeof(gab_value));
  SP() = to + have;
  SET_VAR(have);

  assert(FB() >= VM()->sb + 3);
  assert(BLOCK()->header.kind == kGAB_BLOCK);
  assert(BLOCK_PROTO()->header.kind == kGAB_PROTOTYPE);

  NEXT();
}

CASE_CODE(NOP) { NEXT(); }

CASE_CODE(CONSTANT) {
  PUSH(READ_CONSTANT);

  NEXT();
}

CASE_CODE(NCONSTANT) {
  uint8_t n = READ_BYTE;

  while (n--)
    PUSH(READ_CONSTANT);

  NEXT();
}

CASE_CODE(SHIFT) {
  uint8_t n = READ_BYTE;

  gab_value tmp = PEEK();

  memmove(SP() - (n - 1), SP() - n, (n - 1) * sizeof(gab_value));

  PEEK_N(n) = tmp;

  NEXT();
}

CASE_CODE(POP) {
  DROP();

  NEXT();
}

CASE_CODE(POP_N) {
  DROP_N(READ_BYTE);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_TYPE) {
  SKIP_SHORT;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  PEEK_N(have) = gab_valtype(GAB(), PEEK_N(have));

  DROP_N(have - 1);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(BLOCK) {
  gab_value p = READ_CONSTANT;

  STORE_SP();

  gab_value blk = block(GAB(), p, FB(), BLOCK()->upvalues);

  PUSH(blk);

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
  VAR() = have + nulls;

  while (nulls--)
    PEEK_N(nulls + 1) = gab_nil;

  NEXT();
}

CASE_CODE(PACK_RECORD) {
  uint64_t have = compute_arity(VAR(), READ_BYTE);
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
  uint64_t have = compute_arity(VAR(), READ_BYTE);
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
  uint64_t have = compute_arity(VAR(), have_byte);

  uint8_t adjust = (have_byte & fHAVE_TAIL) >> 1;

  gab_value r = PEEK_N(have);
  gab_value m = ks[GAB_SEND_KMESSAGE];

  if (try_setup_localmatch(GAB(), m, ks, BLOCK_PROTO())) {
    WRITE_BYTE(SEND_CACHE_DIST, OP_MATCHSEND_BLOCK + adjust);
    IP() -= SEND_CACHE_DIST;
    NEXT();
  }

  /* Do the expensive lookup */
  struct gab_impl_rest res = gab_impl(GAB(), m, r);

  if (__gab_unlikely(!res.status)) {
    STORE();
    VM_PANIC(GAB_IMPLEMENTATION_MISSING, FMT_MISSINGIMPL, m, r,
             gab_valtype(GAB(), r));
  }

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
    struct gab_obj_block *b = GAB_VAL_TO_BLOCK(spec);
    struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

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
    struct gab_obj_native *n = GAB_VAL_TO_NATIVE(spec);

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
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  memmove(SP() - (have), SP() - (have - 1), (have - 1) * sizeof(gab_value));
  have--;
  DROP();

  PROPERTY_RECORD(r, have);
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  memmove(SP() - (have), SP() - (have - 1), (have - 1) * sizeof(gab_value));
  have--;
  DROP();

  struct gab_obj_block *spec = (void *)ks[GAB_SEND_KSPEC];

  CALL_BLOCK(spec, have);
}

CASE_CODE(TAILSEND_PRIMITIVE_CALL_MESSAGE_BLOCK) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  memmove(SP() - (have), SP() - (have - 1), (have - 1) * sizeof(gab_value));
  have--;
  DROP();

  struct gab_obj_block *spec = (void *)ks[GAB_SEND_KSPEC];

  TAILCALL_BLOCK(spec, have);
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE_NATIVE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  memmove(SP() - (have), SP() - (have - 1), (have - 1) * sizeof(gab_value));
  have--;
  DROP();

  struct gab_obj_native *spec = (void *)ks[GAB_SEND_KSPEC];

  CALL_NATIVE(spec, have, true);
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE_PRIMITIVE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

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
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);

  SEND_GUARD_KIND(m, kGAB_MESSAGE);
  SEND_GUARD_CACHED_GENERIC_CALL_SPECS(m);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  gab_value spec = ks[GAB_SEND_KSPEC];

  DROP_N(have);

  PUSH(spec);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CALL_MESSAGE) {
  gab_value *ks = READ_CONSTANTS;
  uint8_t have_byte = READ_BYTE;
  uint64_t have = compute_arity(VAR(), have_byte);

  gab_value m = PEEK_N(have);
  gab_value r = PEEK_N(have - 1);
  gab_value t = gab_valtype(GAB(), r);

  VM_PANIC_GUARD_KIND(m, kGAB_MESSAGE);

  struct gab_impl_rest res = gab_impl(GAB(), m, r);

  if (__gab_unlikely(!res.status)) {
    STORE();
    VM_PANIC(GAB_IMPLEMENTATION_MISSING, FMT_MISSINGIMPL, m, r,
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
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value c = PEEK_N(have);

  SEND_GUARD_ISC(c);

  STORE_SP();
  gab_value v = gab_chntake(GAB(), c);

  DROP_N(have);

  if (__gab_unlikely(v == gab_undefined)) {
    PUSH(gab_none);
    SET_VAR(1);
    NEXT();
  }

  PUSH(gab_ok);
  PUSH(v);
  SET_VAR(2);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_PUT) {
  SKIP_SHORT;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  gab_value c = PEEK_N(have);

  SEND_GUARD_ISC(c);

  if (__gab_unlikely(have < 2))
    PUSH(gab_nil), have++;

  gab_value v = PEEK_N(have - 1);

  STORE_SP();
  gab_chnput(GAB(), c, v);

  DROP_N(have - 1);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_FIBER) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  gab_value block = PEEK_N(have - 1);

  VM_PANIC_GUARD_KIND(block, kGAB_BLOCK);

  STORE_SP();
  gab_value fib = gab_arun(GAB(), (struct gab_run_argt){
                                      .main = block,
                                      .flags = GAB().flags,
                                  });

  DROP_N(have);
  PUSH(fib);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CHANNEL) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  STORE_SP();
  gab_value chan = gab_channel(GAB());

  DROP_N(have);
  PUSH(chan);
  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_RECORD) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  if (__gab_unlikely(len % 2 == 1))
    PUSH(gab_nil), len++, have++; // Should we just error here?

  STORE_SP();
  gab_value record = gab_record(GAB(), 2, len / 2, SP() - len, SP() + 1 - len);

  DROP_N(have);
  PUSH(record);
  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_MAKE_SHAPE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  gab_value shape = PEEK_N(have);
  uint64_t len = have - 1;

  if (__gab_unlikely(gab_shplen(shape) != len))
    VM_PANIC(GAB_PANIC, "Expected $ arguments, got $",
             gab_number(gab_shplen(shape)), gab_number(len));

  STORE_SP();
  gab_value record = gab_recordfrom(GAB(), shape, 1, SP() - len);

  DROP_N(have);
  PUSH(record);
  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SHAPE) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  STORE_SP();
  gab_value shape = gab_shape(GAB(), 1, len, SP() - len);

  DROP_N(have);
  PUSH(shape);
  SET_VAR(1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_LIST) {
  gab_value *ks = READ_CONSTANTS;
  uint64_t have = compute_arity(VAR(), READ_BYTE);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  STORE_SP();
  gab_value rec = gab_list(GAB(), len, SP() - len);

  DROP_N(have);
  PUSH(rec);
  SET_VAR(1);

  NEXT();
}

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef IMPL_SEND_BINARY_PRIMITIVE
