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
extern void putf(double arg);
extern void putg(gab_value arg);
extern void putcs(char *arg);

CASE_CODE(MICRO_OP_LOAD_CONSTANT) {
  gab_value k = STENCIL_ARG0_64(gab_value);

  PUSH(k);

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

IMPL_BINARY_MICROOP(IADD, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_ADD)

IMPL_BINARY_MICROOP(ISUB, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_SUB)

IMPL_BINARY_MICROOP(IMUL, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_MUL)

IMPL_BINARY_MICROOP(IDIV, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_DIV)

IMPL_BINARY_MICROOP(IBOR, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_BOR)

IMPL_BINARY_MICROOP(IBND, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_BND)

IMPL_BINARY_MICROOP(ILT, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_LT)

IMPL_BINARY_MICROOP(ILTE, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_LTE)

IMPL_BINARY_MICROOP(IGT, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_GT)

IMPL_BINARY_MICROOP(IGTE, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_GTE)

IMPL_BINARY_MICROOP(ULSH, PRIMITIVE_UNBOXU_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_LSH)

IMPL_BINARY_MICROOP(URSH, PRIMITIVE_UNBOXU_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_RSH)

IMPL_BINARY_MICROOP(FADD, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_ADD)

IMPL_BINARY_MICROOP(FSUB, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_SUB)

IMPL_BINARY_MICROOP(FMUL, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_MUL)

IMPL_BINARY_MICROOP(FDIV, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_DIV)

IMPL_BINARY_MICROOP(IMOD, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI_T,
                    PRIMITIVE_BINARY_MOD)

IMPL_BINARY_MICROOP(FLT, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_LT)

IMPL_BINARY_MICROOP(FLTE, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_LTE)

IMPL_BINARY_MICROOP(FGT, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_GT)

IMPL_BINARY_MICROOP(FGTE, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_GTE)

IMPL_BINARY_MICROOP(STRLT, PRIMITIVE_UNBOXS_T, PRIMITIVE_UNBOXS_T,
                    PRIMITIVE_BINARY_STRLT)

IMPL_BINARY_MICROOP(STRLTE, PRIMITIVE_UNBOXS_T, PRIMITIVE_UNBOXS_T,
                    PRIMITIVE_BINARY_STRLTE)

IMPL_BINARY_MICROOP(STRGT, PRIMITIVE_UNBOXS_T, PRIMITIVE_UNBOXS_T,
                    PRIMITIVE_BINARY_STRGT)

IMPL_BINARY_MICROOP(STRGTE, PRIMITIVE_UNBOXS_T, PRIMITIVE_UNBOXS_T,
                    PRIMITIVE_BINARY_STRGTE)

IMPL_BINARY_MICROOP(VCONS, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV_T,
                    PRIMITIVE_CONS)

IMPL_BINARY_MICROOP(VCONS_RECORD, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV_T,
                    PRIMITIVE_CONS_RECORD)

IMPL_BINARY_MICROOP(VEQ, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV_T,
                    PRIMITIVE_BINARY_EQ)

IMPL_BINARY_MICROOP(STRCONCAT, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV_T,
                    PRIMITIVE_BINARY_CONCAT)

IMPL_UNARY_MICROOP(BLIN, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNARY_LIN);

IMPL_UNARY_MICROOP(IBIN, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNARY_BIN);

/* VALTOF should be a no-op. Look into why it generates code. */
IMPL_UNARY_MICROOP(UNBOXF, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXF);

IMPL_UNARY_MICROOP(UNBOXI, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXI);

IMPL_UNARY_MICROOP(UNBOXU, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXU);

IMPL_UNARY_MICROOP(UNBOXS, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXS);

IMPL_UNARY_MICROOP(UNBOXB, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXB);

IMPL_UNARY_MICROOP(TYPE, PRIMITIVE_UNBOXV_T, PRIMITIVE_TYPE);

IMPL_UNARY_MICROOP(BOXN, PRIMITIVE_UNBOXF_T, PRIMITIVE_BOXN);

IMPL_UNARY_MICROOP(BOXB, PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB);

IMPL_UNARY_MICROOP(BOXS, PRIMITIVE_UNBOXS_T, PRIMITIVE_BOXV);

CASE_CODE(MICRO_OP_PACK_DICT) {
  uint8_t below = STENCIL_ARG0(uint8_t);
  uint8_t above = STENCIL_ARG1(uint8_t);

  PRIMITIVE_PACKRECORD(below, above);

  NEXT();
}

CASE_CODE(MICRO_OP_PACK_LIST) {
  uint8_t below = STENCIL_ARG0(uint8_t);
  uint8_t above = STENCIL_ARG1(uint8_t);

  PRIMITIVE_PACKLIST(below, above);

  NEXT();
}

CASE_CODE(MICRO_OP_SPLAT_LIST) {
  uint8_t local = STENCIL_ARG0(uint8_t);

  PRIMITIVE_SPLATLIST(LOCAL(local));

  NEXT();
}

CASE_CODE(MICRO_OP_SPLAT_DICT) {
  uint8_t local = STENCIL_ARG0(uint8_t);

  PRIMITIVE_SPLATDICT(LOCAL(local));

  NEXT();
}

CASE_CODE(MICRO_OP_SPLAT_SHAPE) {
  uint8_t local = STENCIL_ARG0(uint8_t);

  PRIMITIVE_SPLATSHAPE(LOCAL(local));

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_KIND) {
  uint8_t n = STENCIL_ARG0(uint8_t);
  uint8_t kind = STENCIL_ARG1(uint8_t);

  SEND_GUARD_KIND(PEEK_N(n), kind);

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_TYPE) {
  uint8_t n = STENCIL_ARG0(uint8_t);
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

  PRIMITIVE_TRIM(want, have);

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_SPECS) {
  uint64_t epoch = STENCIL_ARG0_64(uint64_t);

  SEND_GUARD_CACHED_MESSAGE_SPECS(epoch);

  NEXT();
}

CASE_CODE(MICRO_OP_MATCH_HASHT) {
  // TODO @jit: Implement match
  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_MATCH_TYPE) {
  // TODO @jit: Implement match
  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_STACKSPACE) {
  // TODO @jit: Implement guard stackspace
  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_STACKSPACE_SPLATLIST) {
  // TODO @jit: Implement guard stackspace
  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_STACKSPACE_SPLATDICT) {
  // TODO @jit: Implement guard stackspace
  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_STACKSPACE_SPLATSHAPE) {
  // TODO @jit: Implement guard stackspace
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
  uint8_t *ip = STENCIL_ARG1_64(uint8_t *);

  uint64_t have = STENCIL_HV();
  IP() = STENCIL_IP();

  uint64_t below_have = PEEK_N(have + 1);

  PRIMITIVE_CALL_NATIVE(n, have, below_have, true);

  IP() = ip;

  EXIT();
}

CASE_CODE(MICRO_OP_LOAD_PROPERTY) {
  uint8_t local = STENCIL_ARG0(uint8_t);
  uint64_t offset = STENCIL_ARG1_64(uint64_t);

  uint64_t have = STENCIL_HV();
  uint64_t below_have = PEEK_N(have + 1);

  PRIMITIVE_PROPERTY_RECORD(LOCAL(local), offset, have, below_have);

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_BLOCK) {
  gab_value prototype = STENCIL_ARG0_64(gab_value);

  PUSH(PRIMITIVE_BLOCK(prototype));

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_CHANNEL) {

  PUSH(PRIMITIVE_CHANNEL());

  NEXT();
}

CASE_CODE(MICRO_OP_JIT_EXIT) {
  uintptr_t ip = STENCIL_ARG0_64(uintptr_t);
  uint32_t have = STENCIL_ARG1(uint32_t);

  IP() = (uint8_t *)ip;
  SET_VAR(have);

  EXIT();
}

CASE_CODE(MICRO_OP_LOAD_RECORD) {
  // TODO @jit: Implement load record

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_RECORDFROM) {
  // TODO @jit: Implement load recordfrom

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_SHAPE) {
  // TODO @jit: Implement load shape

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_LIST) {
  // TODO @jit: Implement load list

  NEXT();
}

CASE_CODE(MICRO_OP_TUPLE) {
  uint64_t have = STENCIL_HV();
  PUSHTUPLE(have);

  SET_VAR(0);

  NEXT();
}
