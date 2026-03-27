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
 * @brief This file contains the bytecode operations of the Gab VM.
 *
 * These operations are defined entirely using macros. The purpose of this
 * is to facilitate JIT compilation - A jit compiler can be created by
 * changing the macros implementation.
 */

#define IMPL_SEND_UNARY(CODE, guard, boxer, operation_type, unboxer,           \
                        primitive)                                             \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_SENDCONSTANTS;                                        \
    uint64_t have = COMPUTE_TUPLE();                                           \
    uint64_t below_have = PEEK_N(have + 1);                                    \
                                                                               \
    gab_value r = PEEK_N(have);                                                \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(r);                                        \
                                                                               \
    guard(r);                                                                  \
                                                                               \
    operation_type val = unboxer(r);                                           \
                                                                               \
    DROP_N(have + 1);                                                          \
                                                                               \
    PUSH(boxer(primitive(val)));                                               \
                                                                               \
    SET_VAR(below_have + 1);                                                   \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_SEND_BINARY(CODE, guard, a_type, a_unboxer, b_type, b_unboxer,    \
                         c_type, c_boxer, primitive)                           \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_SENDCONSTANTS;                                        \
    uint64_t have = COMPUTE_TUPLE();                                           \
    uint64_t below_have = PEEK_N(have + 1);                                    \
                                                                               \
    SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                      \
                                                                               \
    NILPAD_GUARD_ARGS_GTE(2);                                                  \
                                                                               \
    gab_value a = PEEK_N(have);                                                \
    gab_value b = PEEK_N(have - 1);                                            \
                                                                               \
    SEND_GUARD_CACHED_RECEIVER_TYPE(a);                                        \
                                                                               \
    guard(a);                                                                  \
    guard(b);                                                                  \
                                                                               \
    a_type val_a = a_unboxer(a);                                               \
    b_type val_b = b_unboxer##2(b);                                               \
                                                                               \
    c_type val_c = primitive(val_a, val_b);                                    \
                                                                               \
    gab_value c = c_boxer(val_c);                                              \
                                                                               \
    DROP_N(have + 1);                                                          \
                                                                               \
    PUSH(c);                                                                   \
                                                                               \
    SET_VAR(below_have + 1);                                                   \
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
    SET_VAR(want);                                                             \
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
      PUSH(gab_nil);                                                           \
                                                                               \
    SET_VAR(want);                                                             \
                                                                               \
    NEXT();                                                                    \
  }

CASE_CODE(MATCHTAILSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = COMPUTE_TUPLE();

  assert(istail);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  gab_value r = PEEK_N(have);

  gab_value t = PRIMITIVE_TYPE(r);

  uint16_t idx = PRIMITIVE_MATCH_HASHT(t);

  // TODO @cgab @vm @perf: Handle undefined and record case
  SEND_GUARD_CACHED_MATCH_TYPE(idx, t);

  PRIMITIVE_MATCHTAILCALL_BLOCK(idx, have);

  NEXT_CHECKED();
}

CASE_CODE(MATCHSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = COMPUTE_TUPLE();

  assert(!istail);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  gab_value r = PEEK_N(have);

  gab_value t = PRIMITIVE_TYPE(r);

  uint16_t idx = PRIMITIVE_MATCH_HASHT(t);

  // TODO @cgab @vm @perf: Handle undefined and record case
  SEND_GUARD_CACHED_MATCH_TYPE(idx, t);

  PRIMITIVE_MATCHCALL_BLOCK(idx, have);

  NEXT_CHECKED();
}

CASE_CODE(LOAD_UPVALUE) {
  uint64_t have = VAR();

  PUSH(UPVALUE(READ_BYTE));

  SET_VAR(have + 1);

  NEXT();
}

CASE_CODE(NLOAD_UPVALUE) {
  uint8_t n = READ_BYTE;

  PANIC_GUARD_STACKSPACE(n);

  uint64_t have = VAR();

  SP()[n] = have + n;

  while (n--)
    PUSH(UPVALUE(READ_BYTE));

  NEXT();
}

CASE_CODE(LOAD_LOCAL) {
  uint64_t have = VAR();

  PUSH(LOCAL(READ_BYTE));

  SET_VAR(have + 1);

  NEXT();
}

CASE_CODE(NLOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  PANIC_GUARD_STACKSPACE(n);

  uint64_t have = VAR();
  uint64_t len = have + n;

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_VAR(len);

  NEXT();
}

CASE_CODE(STORE_LOCAL) {
  STORE_LOCAL(READ_BYTE, PEEK());
  NEXT();
}

CASE_CODE(POPSTORE_LOCAL) {
  uint64_t have = VAR();

  STORE_LOCAL(READ_BYTE, POP());

  assert(have >= 1);
  SET_VAR(have - 1);
  NEXT();
}

CASE_CODE(NPOPSTORE_LOCAL) {
  uint64_t have = VAR();

  uint8_t n = READ_BYTE;

  assert(have >= n);
  have -= n;

  while (n--)
    STORE_LOCAL(READ_BYTE, POP());

  SET_VAR(have);
  NEXT();
}

CASE_CODE(NPOPSTORE_STORE_LOCAL) {
  uint64_t have = VAR();

  uint8_t n = READ_BYTE;

  assert(have >= n);
  have -= n;

  while (n-- > 1)
    STORE_LOCAL(READ_BYTE, POP());

  STORE_LOCAL(READ_BYTE, PEEK());

  SET_VAR(have + 1);
  NEXT();
}

CASE_CODE(SEND_NATIVE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_onative *n = GAB_VAL_TO_NATIVE(ks[GAB_SEND_KSPEC]);

  PRIMITIVE_CALL_NATIVE(n, have, below_have, true);

  NEXT_CHECKED();
}

CASE_CODE(SEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = COMPUTE_TUPLE();
  assert(!istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  uint8_t* ip = IP();

  PRIMITIVE_CALL_BLOCK(b, have);

  // PRIMITIVE_JIT_TICK(b, ip, ks);

  NEXT_CHECKED();
}

CASE_CODE(TAILSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = COMPUTE_TUPLE();
  assert(istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  uint8_t* ip = IP();

  PRIMITIVE_TAILCALL_BLOCK(b, have);

  // PRIMITIVE_JIT_TICK(b, ip, ks);

  NEXT_CHECKED();
}

CASE_CODE(LOCALSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = COMPUTE_TUPLE();

  assert(!istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  uint8_t* ip = IP();

  PRIMITIVE_LOCALCALL_BLOCK(b, have);

  //PRIMITIVE_JIT_TICK(b, ip, ks);

  NEXT_CHECKED();
}

CASE_CODE(LOCALTAILSEND_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = COMPUTE_TUPLE();

  assert(istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  uint8_t* ip = IP();

  PRIMITIVE_LOCALTAILCALL_BLOCK(b, have);

  // PRIMITIVE_JIT_TICK(b, ip, ks);

  NEXT_CHECKED();
}

CASE_CODE(SEND_PRIMITIVE_CALL_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = COMPUTE_TUPLE();

  assert(!istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  PANIC_GUARD_KIND(r, kGAB_BLOCK);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(r);

  uint8_t* ip = IP();

  PRIMITIVE_CALL_BLOCK(b, have);

  // PRIMITIVE_JIT_TICK(b, ip, ks);

  NEXT_CHECKED();
}

CASE_CODE(TAILSEND_PRIMITIVE_CALL_BLOCK) {
  bool istail;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(istail);
  uint64_t have = COMPUTE_TUPLE();

  assert(istail);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  PANIC_GUARD_KIND(r, kGAB_BLOCK);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(r);

  uint8_t* ip = IP();

  PRIMITIVE_TAILCALL_BLOCK(b, have);

  // PRIMITIVE_JIT_TICK(b, ip, ks);

  NEXT_CHECKED();
}

CASE_CODE(SEND_PRIMITIVE_CALL_NATIVE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  // Sanity check
  assert(have > 0 && have < 32);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  PANIC_GUARD_KIND(r, kGAB_NATIVE);

  struct gab_onative *n = GAB_VAL_TO_NATIVE(r);

  PRIMITIVE_CALL_NATIVE(n, have, below_have, false);

  NEXT();
}

// float + float = float
IMPL_SEND_BINARY(PRIMITIVE_ADD, PANIC_GUARD_ISN, PRIMITIVE_UNBOXF_T,
                 PRIMITIVE_UNBOXF, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF,
                 PRIMITIVE_UNBOXF_T, PRIMITIVE_BOXN, PRIMITIVE_BINARY_ADD);

// float - float = float
IMPL_SEND_BINARY(PRIMITIVE_SUB, PANIC_GUARD_ISN, PRIMITIVE_UNBOXF_T,
                 PRIMITIVE_UNBOXF, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF,
                 PRIMITIVE_UNBOXF_T, PRIMITIVE_BOXN, PRIMITIVE_BINARY_SUB);

// float * float = float
IMPL_SEND_BINARY(PRIMITIVE_MUL, PANIC_GUARD_ISN, PRIMITIVE_UNBOXF_T,
                 PRIMITIVE_UNBOXF, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF,
                 PRIMITIVE_UNBOXF_T, PRIMITIVE_BOXN, PRIMITIVE_BINARY_MUL);

// float / float = float
IMPL_SEND_BINARY(PRIMITIVE_DIV, PANIC_GUARD_ISN, PRIMITIVE_UNBOXF_T,
                 PRIMITIVE_UNBOXF, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF,
                 PRIMITIVE_UNBOXF_T, PRIMITIVE_BOXN, PRIMITIVE_BINARY_DIV);

// int % int = int
IMPL_SEND_BINARY(PRIMITIVE_MOD, PANIC_GUARD_ISN, PRIMITIVE_UNBOXI_T,
                 PRIMITIVE_UNBOXI, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI,
                 PRIMITIVE_UNBOXI_T, PRIMITIVE_BOXN, PRIMITIVE_BINARY_MOD);

// float < float = bool
IMPL_SEND_BINARY(PRIMITIVE_LT, PANIC_GUARD_ISN, PRIMITIVE_UNBOXF_T,
                 PRIMITIVE_UNBOXF, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_LT);

// float <= float = bool
IMPL_SEND_BINARY(PRIMITIVE_LTE, PANIC_GUARD_ISN, PRIMITIVE_UNBOXF_T,
                 PRIMITIVE_UNBOXF, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_LTE);

// float >= float = bool
IMPL_SEND_BINARY(PRIMITIVE_GT, PANIC_GUARD_ISN, PRIMITIVE_UNBOXF_T,
                 PRIMITIVE_UNBOXF, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_GT);

// float >= float = bool
IMPL_SEND_BINARY(PRIMITIVE_GTE, PANIC_GUARD_ISN, PRIMITIVE_UNBOXF_T,
                 PRIMITIVE_UNBOXF, PRIMITIVE_UNBOXF_T, PRIMITIVE_UNBOXF,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_GTE);

// int | int = int
IMPL_SEND_BINARY(PRIMITIVE_BOR, PANIC_GUARD_ISN, PRIMITIVE_UNBOXI_T,
                 PRIMITIVE_UNBOXI, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI,
                 PRIMITIVE_UNBOXI_T, PRIMITIVE_BOXN, PRIMITIVE_BINARY_BOR);

// int & int = int
IMPL_SEND_BINARY(PRIMITIVE_BND, PANIC_GUARD_ISN, PRIMITIVE_UNBOXI_T,
                 PRIMITIVE_UNBOXI, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI,
                 PRIMITIVE_UNBOXI_T, PRIMITIVE_BOXN, PRIMITIVE_BINARY_BND);

// Implemented logical and/or for booleans with a binary &/| operation.
// bool | bool = bool
IMPL_SEND_BINARY(PRIMITIVE_LOR, PANIC_GUARD_ISB, PRIMITIVE_UNBOXB_T,
                 PRIMITIVE_UNBOXB, PRIMITIVE_UNBOXB_T, PRIMITIVE_UNBOXB,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_BOR);

// bool & bool = bool
IMPL_SEND_BINARY(PRIMITIVE_LND, PANIC_GUARD_ISB, PRIMITIVE_UNBOXB_T,
                 PRIMITIVE_UNBOXB, PRIMITIVE_UNBOXB_T, PRIMITIVE_UNBOXB,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_BND);

// str < str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_LT, PANIC_GUARD_ISS, PRIMITIVE_UNBOXS_T,
                 PRIMITIVE_UNBOXS, PRIMITIVE_UNBOXS_T, PRIMITIVE_UNBOXS,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_STRLT);

// str <= str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_LTE, PANIC_GUARD_ISS, PRIMITIVE_UNBOXS_T,
                 PRIMITIVE_UNBOXS, PRIMITIVE_UNBOXS_T, PRIMITIVE_UNBOXS,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_STRLTE);

// str > str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_GT, PANIC_GUARD_ISS, PRIMITIVE_UNBOXS_T,
                 PRIMITIVE_UNBOXS, PRIMITIVE_UNBOXS_T, PRIMITIVE_UNBOXS,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_STRGT);

// str >= str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_GTE, PANIC_GUARD_ISS, PRIMITIVE_UNBOXS_T,
                 PRIMITIVE_UNBOXS, PRIMITIVE_UNBOXS_T, PRIMITIVE_UNBOXS,
                 PRIMITIVE_UNBOXB_T, PRIMITIVE_BOXB, PRIMITIVE_BINARY_STRGTE);
// uint << int = uint
IMPL_SEND_BINARY(PRIMITIVE_LSH, PANIC_GUARD_ISN, PRIMITIVE_UNBOXU_T,
                 PRIMITIVE_UNBOXU, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI,
                 PRIMITIVE_UNBOXI_T, PRIMITIVE_BOXN, PRIMITIVE_BINARY_LSH);

// uint >> int = uint
IMPL_SEND_BINARY(PRIMITIVE_RSH, PANIC_GUARD_ISN, PRIMITIVE_UNBOXU_T,
                 PRIMITIVE_UNBOXU, PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI,
                 PRIMITIVE_UNBOXI_T, PRIMITIVE_BOXN, PRIMITIVE_BINARY_RSH);

// str + str = str
IMPL_SEND_BINARY(PRIMITIVE_CONCAT, PANIC_GUARD_ISS, PRIMITIVE_UNBOXV_T,
                 PRIMITIVE_UNBOXV, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV,
                 PRIMITIVE_UNBOXV_T, PRIMITIVE_BOXV, PRIMITIVE_BINARY_CONCAT);

// val == val = bool
IMPL_SEND_BINARY(PRIMITIVE_EQ, GUARD_NOP, PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV,
                 PRIMITIVE_UNBOXV_T, PRIMITIVE_UNBOXV, PRIMITIVE_UNBOXB_T,
                 PRIMITIVE_BOXB, PRIMITIVE_BINARY_EQ);

// !bool = bool
IMPL_SEND_UNARY(PRIMITIVE_LIN, PANIC_GUARD_ISB, PRIMITIVE_BOXB,
                PRIMITIVE_UNBOXB_T, PRIMITIVE_UNBOXB, PRIMITIVE_UNARY_LIN);

// ~int = int
IMPL_SEND_UNARY(PRIMITIVE_BIN, PANIC_GUARD_ISN, PRIMITIVE_BOXN,
                PRIMITIVE_UNBOXI_T, PRIMITIVE_UNBOXI, PRIMITIVE_UNARY_BIN);

// val? = val
IMPL_SEND_UNARY(PRIMITIVE_TYPE, GUARD_NOP, PRIMITIVE_BOXV, PRIMITIVE_UNBOXV_T,
                PRIMITIVE_UNBOXV, PRIMITIVE_TYPE);

CASE_CODE(SEND_PRIMITIVE_USE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SEND_GUARD_KIND(r, kGAB_STRING);

  PRIMITIVE_USE(have);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SHORTCUT_GUARD_ARGS_LT(2);

  gab_value a = PEEK_N(have);

  gab_value b = PEEK_N(have - 1);

  STORE_SP();

  gab_value res = PRIMITIVE_CONS(a, b);

  DROP_N(have + 1);

  PUSH(res);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS_RECORD) {
  gab_value *ks = READ_SENDCONSTANTS; // Constants
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  SHORTCUT_GUARD_ARGS_LT(2);

  STORE_SP();

  gab_value arg = PEEK_N(have - 1);

  gab_value res = PRIMITIVE_CONS_RECORD(r, arg);

  DROP_N(have + 1);

  PUSH(res);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATSHAPE) {
  gab_value *ks = READ_SENDCONSTANTS; // Constants
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value s = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_ISSHP(s);

  DROP_N(have + 1);

  PANIC_GUARD_STACKSPACE_SPLATSHAPE(s);

  uint64_t len = PRIMITIVE_SPLATSHAPE(s);

  SET_VAR(below_have + len);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATLIST) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_ISREC(r);

  DROP_N(have + 1);

  PANIC_GUARD_STACKSPACE_SPLATLIST(r);

  uint64_t n = PRIMITIVE_SPLATLIST(r);

  SET_VAR(below_have + n);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATDICT) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_ISREC(r);

  DROP_N(have + 1);

  PANIC_GUARD_STACKSPACE_SPLATDICT(r);

  uint64_t n = PRIMITIVE_SPLATDICT(r);

  SET_VAR(below_have + n);

  NEXT();
}

CASE_CODE(SEND_CONSTANT) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  gab_value spec = ks[GAB_SEND_KSPEC];

  DROP_N(have + 1);
  PUSH(spec);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PROPERTY) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  PRIMITIVE_PROPERTY_RECORD(r, ks[GAB_SEND_KSPEC], have, below_have);

  NEXT_CHECKED();
}

CASE_CODE(RETURN) {
  PRIMITIVE_RETURN();

  NEXT();
}

CASE_CODE(NOP) { NEXT(); }

CASE_CODE(CONSTANT) {
  uint64_t have = VAR();

  PUSH(READ_CONSTANT);

  SET_VAR(have + 1);

  NEXT();
}

CASE_CODE(NCONSTANT) {
  uint8_t n = READ_BYTE;

  PANIC_GUARD_STACKSPACE(n);

  uint64_t have = VAR() + n;

  while (n--)
    PUSH(READ_CONSTANT);

  SET_VAR(have);

  NEXT();
}

CASE_CODE(POP) {
  uint64_t have = VAR();

  DROP();

  SET_VAR(have - 1);
  NEXT();
}

CASE_CODE(POP_N) {
  uint64_t have = VAR();

  uint8_t n = READ_BYTE;
  DROP_N(n);

  SET_VAR(have - n);
  NEXT();
}

CASE_CODE(BLOCK) {
  gab_value p = READ_CONSTANT;
  uint64_t have = VAR();

  STORE_SP();

  gab_value blk = PRIMITIVE_BLOCK(p);

  PUSH(blk);

  SET_VAR(have + 1);

  NEXT();
}

CASE_CODE(TUPLE) {
  uint64_t have = VAR();

  PUSHTUPLE(have);

  SET_VAR(0);

  NEXT();
}

CASE_CODE(NTUPLE) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = VAR();

    PUSHTUPLE(have);

    SET_VAR(0);
  }

  NEXT();
}

CASE_CODE(TUPLE_CONSTANT) {
  uint64_t have = VAR();

  PUSHTUPLE(have);

  PUSH(READ_CONSTANT);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(TUPLE_NCONSTANT) {
  uint64_t have = VAR();

  PUSHTUPLE(have);

  uint8_t n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(READ_CONSTANT);

  SET_VAR(have);

  NEXT();
}

CASE_CODE(TUPLE_LOAD_LOCAL) {
  uint64_t have = VAR();

  PUSHTUPLE(have);

  PUSH(LOCAL(READ_BYTE));

  SET_VAR(1);

  NEXT();
}

CASE_CODE(TUPLE_NLOAD_LOCAL) {
  uint64_t have = VAR();

  PUSHTUPLE(have);

  uint8_t n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_VAR(have);

  NEXT();
}

CASE_CODE(NTUPLE_LOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = VAR();

    PUSHTUPLE(have);

    SET_VAR(0);
  }

  PUSH(LOCAL(READ_BYTE));

  SET_VAR(1);

  NEXT();
}

CASE_CODE(NTUPLE_NLOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = VAR();

    PUSHTUPLE(have);

    SET_VAR(0);
  }

  n = READ_BYTE;

  uint8_t have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_VAR(have);

  NEXT();
}

CASE_CODE(NTUPLE_CONSTANT) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = VAR();

    PUSHTUPLE(have);

    SET_VAR(0);
  }

  PUSH(READ_CONSTANT);

  SET_VAR(1);

  NEXT();
}

CASE_CODE(NTUPLE_NCONSTANT) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = VAR();

    PUSHTUPLE(have);

    SET_VAR(0);
  }

  n = READ_BYTE;

  uint8_t have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(READ_CONSTANT);

  SET_VAR(have);

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
  uint64_t have = VAR();

  PRIMITIVE_TRIM(want, have);

  NEXT();
}

CASE_CODE(PACK_DICT) {
  uint8_t below = READ_BYTE;
  uint8_t above = READ_BYTE;

  PRIMITIVE_PACKRECORD(below, above);

  NEXT();
}

CASE_CODE(PACK_LIST) {
  uint8_t below = READ_BYTE;
  uint8_t above = READ_BYTE;

  PRIMITIVE_PACKLIST(below, above);

  NEXT();
}

CASE_CODE(SEND) {
  uint8_t adjust;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(adjust);
  uint64_t have = COMPUTE_TUPLE();

  PRIMITIVE_SEND(have);
}

CASE_CODE(SEND_PRIMITIVE_TAKE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value c = PEEK_N(have);

  PRIMITIVE_TAKE(c);
}

CASE_CODE(SEND_PRIMITIVE_PUT) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  gab_value c = PEEK_N(have);

  PRIMITIVE_PUT(c);
}

CASE_CODE(SEND_PRIMITIVE_FIBER) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  NILPAD_GUARD_ARGS_GTE(2);

  gab_value block = PEEK_N(have - 1);

  PANIC_GUARD_KIND(block, kGAB_BLOCK);

  PRIMITIVE_FIBER(block);
}

CASE_CODE(SEND_PRIMITIVE_CHANNEL) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  STORE_SP();
  gab_value chan = PRIMITIVE_CHANNEL();

  DROP_N(have + 1);
  PUSH(chan);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_RECORD) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  gab_value record = PRIMITIVE_RECORD(len);

  DROP_N(have + 1);

  PUSH(record);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_MAKE_SHAPE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  gab_value shape = PEEK_N(have);
  uint64_t len = have - 1;

  PANIC_GUARD_SHAPE_LEN(shape, len);

  gab_value record = PRIMITIVE_RECORDFROM(shape, len);

  DROP_N(have + 1);

  PUSH(record);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SHAPE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();
  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  gab_value shape = PRIMITIVE_SHAPE(len);

  DROP_N(have + 1);

  PUSH(shape);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_LIST) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = COMPUTE_TUPLE();

  uint64_t below_have = PEEK_N(have + 1);

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  gab_value rec = PRIMITIVE_LIST(len);

  DROP_N(have + 1);
  PUSH(rec);

  SET_VAR(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_JIT_ENTER) {
  gab_value *ks = READ_SENDCONSTANTS;

  handler code = (void *)(uintptr_t)ks[GAB_SEND_KSPEC];

  PRIMITIVE_JIT_ENTER(code);

  NEXT();
}
