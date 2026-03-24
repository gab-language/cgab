#include "gab.h"
#include "engine.h"
#include "vm_ops.h"

/*
 * This file contains stencil version of the micro-op codes.
 *
 */

extern char hole[8], hole2[8];

#define STENCIL_ARG0(t) (t)((uintptr_t)(&hole))
#define STENCIL_ARG1(t) (t)((uintptr_t)(&hole2))

ATTRIBUTES extern union gab_value_pair jump(OP_HANDLER_ARGS);
ATTRIBUTES extern union gab_value_pair bail(OP_HANDLER_ARGS);

#undef NEXT
#define NEXT() [[clang::musttail]] return jump(DISPATCH_ARGS());

// TODO: Update this to bake in the IP argument, and update the call to bail.
// Woah, could I literally just bail out of the jit by tail-calling to the
// OP-HANDLER that I want?
#undef MISS_CACHED_SEND
#define MISS_CACHED_SEND(reason) return bail(DISPATCH_ARGS());

#undef MISS_CACHED_TRIM
#define MISS_CACHED_TRIM(reason) return bail(DISPATCH_ARGS());

CASE_CODE(MICRO_OP_LOAD_LOCAL) {
  PUSH(LOCAL(STENCIL_ARG0(uint8_t)));

  NEXT();
}

CASE_CODE(MICRO_OP_STORE_LOCAL) {
  STORE_LOCAL(STENCIL_ARG0(uint8_t), POP());

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_UPVALUE) {
  PUSH(UPVALUE(STENCIL_ARG0(uint8_t)));

  NEXT();
}

// TODO: Figure out how to get this to emit a FP add without a bunch of gunk
// The data in the addresses is already a valid float and doesn't need
// conversion.
#define IMPL_BINARY_MICROOP(name, lt, rt, op)                                  \
  CASE_CODE(MICRO_OP_##name##K) {                                              \
    lt l = (lt)(uintptr_t)POP();                                               \
    rt r = STENCIL_ARG0(rt);                                                   \
                                                                               \
    PUSH(op(l, r));                                                            \
                                                                               \
    NEXT();                                                                    \
  }                                                                            \
                                                                               \
  CASE_CODE(MICRO_OP_##name) {                                                 \
    lt l = (lt)(uintptr_t)POP();                                               \
    rt r = (rt)(uintptr_t)POP();                                               \
                                                                               \
    PUSH(op(l, r));                                                            \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_UNARY_MICROOP(name, t, op)                                        \
  CASE_CODE(MICRO_OP_##name) {                                                 \
    PEEK() = op((t)(uintptr_t)PEEK());                                         \
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

IMPL_BINARY_MICROOP(FADD, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_ADD)

IMPL_BINARY_MICROOP(FSUB, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_SUB)

IMPL_BINARY_MICROOP(FMUL, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_MUL)

IMPL_BINARY_MICROOP(FDIV, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF_T,
                    PRIMITIVE_BINARY_DIV)

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

IMPL_BINARY_MICROOP(VCONSRECORD, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV_T,
                    PRIMITIVE_CONS_RECORD)

IMPL_BINARY_MICROOP(VEQ, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV_T,
                    PRIMITIVE_BINARY_EQ)

IMPL_BINARY_MICROOP(STRCONCAT, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV_T,
                    PRIMITIVE_BINARY_CONCAT)

IMPL_UNARY_MICROOP(BLIN, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNARY_LIN);

IMPL_UNARY_MICROOP(IBIN, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNARY_BIN);

/* VALTOF should be a no-op. Look into why it generates code. */
IMPL_UNARY_MICROOP(UNBOXF, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF);

IMPL_UNARY_MICROOP(UNBOXI, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI);

IMPL_UNARY_MICROOP(UNBOXU, PRIMITIVE_UNBOXU_T, PRIMITIVE_UNBOXU);

IMPL_UNARY_MICROOP(TYPE, PRIMITIVE_UNBOXV_T, PRIMITIVE_TYPE);

IMPL_UNARY_MICROOP(BOXN, PRIMITIVE_UNBOXV_T, PRIMITIVE_BOXN);

IMPL_UNARY_MICROOP(BOXB, PRIMITIVE_UNBOXV_T, PRIMITIVE_BOXB);

CASE_CODE(MICRO_OP_PACK_RECORD) {
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

CASE_CODE(MICRO_OP_SPLAT_RECORD) {
  uint8_t local = STENCIL_ARG0(uint8_t);

  PRIMITIVE_SPLATDICT(LOCAL(local));

  NEXT();
}

/*
 * How should these guards work?
 *
 * BC -> IR -> STENCIL
 * stack -> ssa -> stack
 */
CASE_CODE(MICRO_OP_GUARD_KIND) {
  uint8_t local = STENCIL_ARG0(uint8_t);
  uint8_t kind = STENCIL_ARG1(uint8_t);

  SEND_GUARD_KIND(LOCAL(local), kind);

  NEXT();
}

CASE_CODE(MICRO_OP_GUARD_TYPE) {
  uint8_t local = STENCIL_ARG0(uint8_t);
  gab_value type = STENCIL_ARG1(gab_value);

  SEND_GUARD_TYPE(LOCAL(local), type);

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

CASE_CODE(MICRO_OP_CALL_NATIVE) {
  struct gab_onative *n = STENCIL_ARG0(struct gab_onative *);
  bool message = STENCIL_ARG1(bool);
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  PRIMITIVE_CALL_NATIVE(n, have, below_have, message);

  NEXT();
}

CASE_CODE(MICRO_OP_LOAD_PROPERTY) {
  uint8_t local = STENCIL_ARG0(uint8_t);
  uint64_t offset = STENCIL_ARG1(uint64_t);

  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  PRIMITIVE_PROPERTY_RECORD(LOCAL(local), offset, have, below_have);

  NEXT();
}
