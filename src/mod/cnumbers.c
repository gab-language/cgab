/**
 *  MIT License
 *
 *  Copyright (c) 2023 Teddy Randby
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "gab.h"

// Required for windows
#define _USE_MATH_DEFINES
#include <math.h>

typedef struct {
  uint32_t state[16];
  uint32_t index;
  bool seeded;
} Well512;

static void random_seed(Well512 *well) {
  srand((uint32_t)time(nullptr));
  for (int i = 0; i < 16; i++) {
    well->state[i] = rand();
  }
}

// Code from: http://www.lomont.org/Math/Papers/2008/Lomont_PRNG_2008.pdf
static uint32_t advanceState(Well512 *well) {
  uint32_t a, b, c, d;
  a = well->state[well->index];
  c = well->state[(well->index + 13) & 15];
  b = a ^ c ^ (a << 16) ^ (c << 15);
  c = well->state[(well->index + 9) & 15];
  c ^= (c >> 11);
  a = well->state[well->index] = b ^ c;
  d = a ^ ((a << 5) & 0xda442d24U);

  well->index = (well->index + 15) & 15;
  a = well->state[well->index];
  well->state[well->index] = a ^ b ^ d ^ (a << 2) ^ (b << 18) ^ (c << 28);
  return well->state[well->index];
}

static double random_float() {
  static Well512 well;

  if (!well.seeded) {
    well.seeded = true;
    random_seed(&well);
  }

  // A double has 53 bits of precision in its mantissa, and we'd like to take
  // full advantage of that, so we need 53 bits of random source data.

  // First, start with 32 random bits, shifted to the left 21 bits.
  double result = (double)advanceState(&well) * (1 << 21);

  // Then add another 21 random bits.
  result += (double)(advanceState(&well) & ((1 << 21) - 1));

  // Now we have a number from 0 - (2^53). Divide be the range to get a double
  // from 0 to 1.0 (half-inclusive).
  result /= 9007199254740992.0;

  return result;
}

GAB_DYNLIB_NATIVE_FN(number, between) {
  double min = 0, max = 1;

  switch (argc) {
  case 1:
    break;

  case 2: {
    if (gab_valkind(argv[1]) != kGAB_NUMBER)
      return gab_pktypemismatch(gab, argv[1], kGAB_NUMBER);

    max = gab_valtof(argv[1]);

    break;
  }

  case 3: {
    if (gab_valkind(argv[1]) != kGAB_NUMBER)
      return gab_pktypemismatch(gab, argv[1], kGAB_NUMBER);

    if (gab_valkind(argv[2]) != kGAB_NUMBER)
      return gab_pktypemismatch(gab, argv[2], kGAB_NUMBER);

    min = gab_valtof(argv[1]);
    max = gab_valtof(argv[2]);
    break;
  }

  default:
    return gab_panicf(gab, "Invalid call to gab_numlib_random");
  }

  double range = max - min;

  double num = min + (random_float() * range);

  gab_value res = gab_number(num);

  gab_vmpush(gab_thisvm(gab), res);
  return gab_union_cvalid(gab_nil);
}

#define DEFINE_SINGLE_ARG_MATH_FN(name, f, to_val)                             \
  GAB_DYNLIB_NATIVE_FN(number, name) {                                         \
    gab_value num = gab_arg(0);                                                \
                                                                               \
    if (gab_valkind(num) != kGAB_NUMBER)                                       \
      return gab_pktypemismatch(gab, num, kGAB_NUMBER);                        \
                                                                               \
    gab_value res = to_val(f(gab_valtof(num)));                                \
                                                                               \
    gab_vmpush(gab_thisvm(gab), res);                                          \
    return gab_union_cvalid(gab_nil);                                          \
  }

DEFINE_SINGLE_ARG_MATH_FN(ceil, ceil, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(floor, floor, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(round, round, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(acos, acos, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(asin, asin, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(atan, atan, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(cos, cos, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(sin, sin, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(tan, tan, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(abs, fabs, gab_number);
DEFINE_SINGLE_ARG_MATH_FN(isnan, isnan, gab_bool);
DEFINE_SINGLE_ARG_MATH_FN(isinf, isinf, gab_bool);

#define GAB_DEF_SINGLE_ARG_MATH_FN(name)                                       \
  {                                                                            \
      gab_message(gab, #name),                                                 \
      t,                                                                       \
      gab_snative(gab, #name, gab_mod_number_##name),                          \
  }

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_NUMBER);
  gab_value mod = gab_strtomsg(t);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              mod,
              t,
          },
          {
              gab_message(gab, "Infinity"),
              mod,
              gab_number(INFINITY),
          },
          {
              gab_message(gab, "Pi"),
              mod,
              gab_number(M_PI),
          },
          {
              gab_message(gab, "E"),
              mod,
              gab_number(M_E),
          },
          {
              gab_message(gab, "MaxInt"),
              mod,
              gab_number(GAB_INTMAX),
          },
          GAB_DEF_SINGLE_ARG_MATH_FN(ceil), GAB_DEF_SINGLE_ARG_MATH_FN(floor),
          GAB_DEF_SINGLE_ARG_MATH_FN(round), GAB_DEF_SINGLE_ARG_MATH_FN(acos),
          GAB_DEF_SINGLE_ARG_MATH_FN(asin), GAB_DEF_SINGLE_ARG_MATH_FN(atan),
          GAB_DEF_SINGLE_ARG_MATH_FN(cos), GAB_DEF_SINGLE_ARG_MATH_FN(sin),
          GAB_DEF_SINGLE_ARG_MATH_FN(tan), GAB_DEF_SINGLE_ARG_MATH_FN(abs),
          {
              gab_message(gab, "is\\nan"),
              t,
              gab_snative(gab, "is\\nan", gab_mod_number_isnan),
          },
          {
              gab_message(gab, "is\\inf"),
              t,
              gab_snative(gab, "is\\inf", gab_mod_number_isinf),
          },
          {
              gab_message(gab, "float\\between"),
              mod,
              gab_snative(gab, "float\\between", gab_mod_number_between),
          });

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
