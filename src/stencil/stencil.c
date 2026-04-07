#include "engine.h"
#include "gab.h"
#include "vm_ops.h"

/*
 * This file contains stenciled version of the micro-op codes.
 */
extern unsigned char _jit_arg1[8];
extern unsigned char _jit_arg2[8];
extern unsigned char _jit_arg1_large[100000];
extern unsigned char _jit_arg2_large[100000];
extern unsigned char _jit_arg_ip[100000];
extern unsigned char _jit_arg_hv[100000];

#define STENCIL_ARG0(t)                                                        \
  ({                                                                           \
    static_assert(sizeof(t) <= 4);                                             \
    (t)((uintptr_t)(&_jit_arg1));                                              \
  })

#define STENCIL_ARG1(t)                                                        \
  ({                                                                           \
    static_assert(sizeof(t) <= 4);                                             \
    (t)((uintptr_t)(&_jit_arg2));                                              \
  })

#define STENCIL_ARG0_64(t) (t)((uintptr_t)(&_jit_arg1_large))
#define STENCIL_ARG1_64(t) (t)((uintptr_t)(&_jit_arg2_large))

#define STENCIL_IP() (uint8_t *)(uintptr_t)(&_jit_arg_ip)
#define STENCIL_HV() (uintptr_t)(&_jit_arg_hv)

ATTRIBUTES extern union gab_value_pair _jit_next(OP_HANDLER_ARGS);
ATTRIBUTES extern union gab_value_pair _jit_exit(OP_HANDLER_ARGS);
ATTRIBUTES extern union gab_value_pair _jit_bail(OP_HANDLER_ARGS);

#undef NEXT
#define NEXT() ({ [[clang::musttail]] return _jit_next(DISPATCH_ARGS()); })

#define EXIT() [[clang::musttail]] return _jit_exit(DISPATCH_ARGS());

// TODO: Update this to bake in the IP argument, and update the call to bail.
// Woah, could I literally just bail out of the jit by tail-calling to the
// OP-HANDLER that I want?
#undef MISS_CACHED_SEND
#define MISS_CACHED_SEND(reason)                                               \
  ({                                                                           \
    IP() = STENCIL_IP();                                                       \
    SET_VAR(STENCIL_HV());                                                     \
    [[clang::musttail]] return _jit_bail(DISPATCH_ARGS());                     \
  })

#undef MISS_CACHED_TRIM
#define MISS_CACHED_TRIM(reason)                                               \
  ({                                                                           \
    IP() = STENCIL_IP();                                                       \
    SET_VAR(STENCIL_HV());                                                     \
    [[clang::musttail]] return _jit_bail(DISPATCH_ARGS());                     \
  })

extern void putl(uintptr_t arg);
extern void putp(uintptr_t arg);
extern void puti(int64_t arg);
extern void putf(double arg);
extern void putg(gab_value arg);
extern void putcs(char *arg);
extern void puta(char arg);

CASE_CODE(MICRO_OP_LOAD_CONSTANT) {
  gab_value k = STENCIL_ARG0_64(gab_value);

  PUSH(k);

  NEXT();
}

CASE_CODE(MICRO_OP_SPILL) {
  int32_t n = STENCIL_ARG0(int32_t);
  int16_t to = STENCIL_ARG1(int16_t);

  SP()[to] = PEEK_N(n - to);

  NEXT();
}

CASE_CODE(MICRO_OP_DROP_N) {
  uint8_t n = STENCIL_ARG0(uint8_t);

  DROP_N(n);

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_LOCAL) {
  uint8_t local = STENCIL_ARG0(uint8_t);

  PUSH(LOCAL(local));

  NEXT();
}

CASE_CODE(MICRO_OP_STORE_LOCAL) {
  STORE_LOCAL(STENCIL_ARG0(uint8_t), POP());
  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_UPVALUE) {
  gab_value upv = UPVALUE(STENCIL_ARG0(uint8_t));
  PUSH(upv);
  NEXT();
}

#define IMPL_BINARY_MICROOP(name, lt, rt, op)                                  \
  CASE_CODE(MICRO_OP_##name##K) {                                              \
    lt l = (lt)(uintptr_t)POP();                                               \
    rt r = STENCIL_ARG0_64(rt);                                                \
                                                                               \
    PUSH((uintptr_t)op(l, r));                                                 \
                                                                               \
    NEXT();                                                                    \
  }                                                                            \
                                                                               \
  CASE_CODE(MICRO_OP_##name) {                                                 \
    rt r = (rt)(uintptr_t)(POP());                                             \
    lt l = (lt)(uintptr_t)(POP());                                             \
                                                                               \
    uintptr_t v = op(l, r);                                                    \
    PUSH(v);                                                                   \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_UNARY_MICROOP(name, t, op)                                        \
  CASE_CODE(MICRO_OP_##name) {                                                 \
    t v = (t)(uintptr_t)POP();                                                 \
    PUSH((uintptr_t)op(v));                                                    \
    NEXT();                                                                    \
  }                                                                            \
  CASE_CODE(MICRO_OP_##name##2) {                                              \
    t v = (t)(uintptr_t)PEEK2();                                               \
    PEEK2() = (uintptr_t)op(v);                                                \
    NEXT();                                                                    \
  }

IMPL_BINARY_MICROOP(IADD, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_ADD)

IMPL_BINARY_MICROOP(ISUB, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_SUB)

IMPL_BINARY_MICROOP(IMUL, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_MUL)

IMPL_BINARY_MICROOP(IDIV, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_DIV)

IMPL_BINARY_MICROOP(IBOR, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_BOR)

IMPL_BINARY_MICROOP(IBND, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_BND)

IMPL_BINARY_MICROOP(ILT, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_LT)

IMPL_BINARY_MICROOP(ILTE, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_LTE)

IMPL_BINARY_MICROOP(IGT, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_GT)

IMPL_BINARY_MICROOP(IGTE, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_GTE)

IMPL_BINARY_MICROOP(ULSH, MICRO_OP_UNBOXU_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_LSH)

IMPL_BINARY_MICROOP(URSH, MICRO_OP_UNBOXU_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_RSH)

IMPL_BINARY_MICROOP(FADD, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF_T,
                    MICRO_OP_BINARY_ADD)

IMPL_BINARY_MICROOP(FSUB, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF_T,
                    MICRO_OP_BINARY_SUB)

IMPL_BINARY_MICROOP(FMUL, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF_T,
                    MICRO_OP_BINARY_MUL)

IMPL_BINARY_MICROOP(FDIV, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF_T,
                    MICRO_OP_BINARY_DIV)

IMPL_BINARY_MICROOP(IMOD, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI_T,
                    MICRO_OP_BINARY_MOD)

IMPL_BINARY_MICROOP(FLT, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF_T,
                    MICRO_OP_BINARY_LT)

IMPL_BINARY_MICROOP(FLTE, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF_T,
                    MICRO_OP_BINARY_LTE)

IMPL_BINARY_MICROOP(FGT, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF_T,
                    MICRO_OP_BINARY_GT)

IMPL_BINARY_MICROOP(FGTE, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF_T,
                    MICRO_OP_BINARY_GTE)

IMPL_BINARY_MICROOP(STRLT, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS_T,
                    MICRO_OP_BINARY_STRLT)

IMPL_BINARY_MICROOP(STRLTE, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS_T,
                    MICRO_OP_BINARY_STRLTE)

IMPL_BINARY_MICROOP(STRGT, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS_T,
                    MICRO_OP_BINARY_STRGT)

IMPL_BINARY_MICROOP(STRGTE, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS_T,
                    MICRO_OP_BINARY_STRGTE)

IMPL_BINARY_MICROOP(VCONS, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV_T, MICRO_OP_CONS)

IMPL_BINARY_MICROOP(VCONS_RECORD, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV_T,
                    MICRO_OP_CONS_RECORD)

IMPL_BINARY_MICROOP(VEQ, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV_T,
                    MICRO_OP_BINARY_EQ)

IMPL_BINARY_MICROOP(STRCONCAT, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV_T,
                    MICRO_OP_BINARY_CONCAT)

IMPL_UNARY_MICROOP(BLIN, MICRO_OP_UNBOXV_T, MICRO_OP_UNARY_LIN);

IMPL_UNARY_MICROOP(IBIN, MICRO_OP_UNBOXV_T, MICRO_OP_UNARY_BIN);

/* VALTOF should be a no-op. Look into why it generates code. */
IMPL_UNARY_MICROOP(UNBOXF, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXF);

IMPL_UNARY_MICROOP(UNBOXI, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXI);

IMPL_UNARY_MICROOP(UNBOXU, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXU);

IMPL_UNARY_MICROOP(UNBOXS, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXS);

IMPL_UNARY_MICROOP(UNBOXB, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXB);

IMPL_UNARY_MICROOP(TYPE, MICRO_OP_UNBOXV_T, MICRO_OP_TYPE);

IMPL_UNARY_MICROOP(BOXN, MICRO_OP_UNBOXF_T, MICRO_OP_BOXN);

IMPL_UNARY_MICROOP(BOXB, MICRO_OP_UNBOXB_T, MICRO_OP_BOXB);

IMPL_UNARY_MICROOP(BOXS, MICRO_OP_UNBOXS_T, MICRO_OP_BOXV);

CASE_CODE(MICRO_OP_PACK_LIST) {
  uint8_t below = STENCIL_ARG0(uint8_t);
  uint8_t above = STENCIL_ARG1(uint8_t);

  MICRO_OP_PACK_LIST(below, above);

  NEXT();
}

CASE_CODE(MICRO_OP_PACK_DICT) {
  uint8_t below = STENCIL_ARG0(uint8_t);
  uint8_t above = STENCIL_ARG1(uint8_t);

  MICRO_OP_PACK_DICT(below, above);

  NEXT();
}

CASE_CODE(MICRO_OP_UKRECAT) {
  int32_t n = STENCIL_ARG0(int32_t);
  uint64_t i = STENCIL_ARG1_64(uint64_t);

  gab_value r = PEEK_N(n);

  PUSH(MICRO_OP_UKRECAT(r, i));

  NEXT();
}

CASE_CODE(MICRO_OP_UVRECAT) {
  int32_t n = STENCIL_ARG0(int32_t);
  uint64_t i = STENCIL_ARG1_64(uint64_t);

  gab_value r = PEEK_N(n);

  PUSH(MICRO_OP_UVRECAT(r, i));

  NEXT();
}

CASE_CODE(MICRO_OP_ENTER) { NEXT(); }

CASE_CODE(MICRO_OP_SPLAT_SHAPE) {
  uint8_t local = STENCIL_ARG0(uint8_t);

  MICRO_OP_SPLATSHAPE(LOCAL(local));

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_KIND) {
  int32_t n = STENCIL_ARG0(int32_t);
  uint8_t kind = STENCIL_ARG1(uint8_t);

  SEND_GUARD_KIND(PEEK_N(n), kind);

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_TYPE) {
  int32_t n = STENCIL_ARG0(uint32_t);
  gab_value type = STENCIL_ARG1_64(gab_value);

  SEND_GUARD_TYPE(PEEK_N(n), type);

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_NILPAD) {
  uint16_t n = STENCIL_ARG0(uint16_t);

  uint64_t have = COMPUTE_TUPLE();

  NILPAD_GUARD_ARGS_GTE(n);

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_TRIM_EXACTLY_N) {
  uint8_t want = STENCIL_ARG0(uint8_t);

  TRIM_GUARD_EXACTLY_N(want, want)

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_TRIM_UP_N) {
  uint8_t want = STENCIL_ARG0(uint8_t);
  uint8_t n = STENCIL_ARG1(uint8_t);

  TRIM_GUARD_UP_N(want, n)

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_TRIM_DOWN_N) {
  uint8_t want = STENCIL_ARG0(uint8_t);
  uint8_t n = STENCIL_ARG1(uint8_t);

  TRIM_GUARD_DOWN_N(want, n)

  NEXT();
}

CASE_CODE(MICRO_OP_TRIM) {
  uint8_t want = STENCIL_ARG0(uint8_t);
  uint64_t have = COMPUTE_TUPLE();

  MICRO_OP_TRIM(want, have);

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_SPECS) {
  uint64_t epoch = STENCIL_ARG0_64(uint64_t);

  SEND_GUARD_CACHED_MESSAGE_SPECS(epoch);

  NEXT();
}

CASE_CODE(MICRO_OP_MATCH_HASHT) {
  // TODO @jit: Implement match
  gab_assert(false, "TODO");
  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_MATCH_TYPE) {
  // TODO @jit: Implement match
  gab_assert(false, "TODO");
  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_STACKSPACE) {
  uint64_t n = STENCIL_ARG0_64(uint64_t);

  PANIC_GUARD_STACKSPACE(n);

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_STACKSPACE_SPLATSHAPE) {
  // TODO @jit: Implement guard stackspace
  gab_assert(false, "TODO");
  NEXT();
}

CASE_CODE(MICRO_OP_SEND_CONSTANT) {
  gab_value k = STENCIL_ARG0_64(gab_value);

  uint64_t have = STENCIL_HV();
  uint64_t below_have = PEEK_N(have + 1);

  DROP_N(have + 1);

  PUSH(k);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(MICRO_OP_CALL_NATIVE) {
  struct gab_onative *n = STENCIL_ARG0_64(struct gab_onative *);

  uint64_t have = STENCIL_HV();

  uint64_t below_have = PEEK_N(have + 1);

  MICRO_OP_CALL_NATIVE(n, have, below_have, true);

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_BLOCK) {
  gab_value prototype = POP();

  PUSH(MICRO_OP_BLOCK(prototype));

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_CHANNEL) {

  PUSH(MICRO_OP_CHANNEL());

  NEXT();
}

CASE_CODE(MICRO_OP_JIT_EXIT) {
  uintptr_t ip = STENCIL_ARG0_64(uintptr_t);
  uint32_t have = STENCIL_ARG1(uint32_t);

  IP() = (uint8_t *)ip;

  if (have)
    SET_VAR(have);

  EXIT();
}

CASE_CODE(MICRO_OP_LOAD_RECORD) {
  uint64_t len = STENCIL_ARG1_64(uint64_t);

  gab_value shape = MICRO_OP_RECORD(len);

  DROP_N(len + 2);

  PUSH(shape);

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_RECORDFROM) {
  uint64_t len = STENCIL_ARG1_64(uint64_t);

  gab_value shape = STENCIL_ARG1_64(gab_value);
  
  gab_value record = MICRO_OP_RECORDFROM(shape, len);

  DROP_N(len + 2);

  PUSH(record);

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_SHAPE) {
  uint64_t len = STENCIL_ARG1_64(uint64_t);

  gab_value shape = MICRO_OP_SHAPE(len);

  DROP_N(len + 2);

  PUSH(shape);

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_LIST) {
  int32_t n = STENCIL_ARG0(int32_t);
  uint64_t len = STENCIL_ARG1_64(uint64_t);

  gab_value list = MICRO_OP_LIST(n, len);

  DROP_N(len + 2);

  PUSH(list);

  NEXT();
}

CASE_CODE(MICRO_OP_TUPLE) {
  uint64_t have = STENCIL_HV();
  PUSHTUPLE(have);

  SET_VAR(0);

  NEXT();
}
