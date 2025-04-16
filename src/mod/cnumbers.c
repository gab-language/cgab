#include "core.h"
#include "gab.h"
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

union gab_value_pair gab_numlib_between(struct gab_triple gab, uint64_t argc,
                                        gab_value argv[argc]) {
  double min = 0, max = 1;

  switch (argc) {
  case 1:
    break;

  case 2: {
    if (gab_valkind(argv[1]) != kGAB_NUMBER)
      return gab_panicf(gab, "Invalid call to gab_numlib_random");

    max = gab_valtof(argv[1]);

    break;
  }

  case 3: {
    if (gab_valkind(argv[1]) != kGAB_NUMBER ||
        gab_valkind(argv[2]) != kGAB_NUMBER) {
      return gab_panicf(gab, "Invalid call to gab_numlib_random");
    }

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

union gab_value_pair gab_numlib_floor(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_value num = gab_arg(0);

  if (gab_valkind(num) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, num, kGAB_NUMBER);

  gab_value res = gab_number(floor(gab_valtof(num)));

  gab_vmpush(gab_thisvm(gab), res);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_numlib_isnan(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_value num = gab_arg(0);

  if (gab_valkind(num) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, num, kGAB_NUMBER);

  gab_value res = gab_bool(isnan(gab_valtof(num)));

  gab_vmpush(gab_thisvm(gab), res);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_numlib_isinf(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_value num = gab_arg(0);

  if (gab_valkind(num) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, num, kGAB_NUMBER);

  gab_value res = gab_bool(isinf(gab_valtof(num)));

  gab_vmpush(gab_thisvm(gab), res);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_NUMBER);
  gab_value mod = gab_strtomsg(t);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "floor"),
              t,
              gab_snative(gab, "floor", gab_numlib_floor),
          },
          {
              gab_message(gab, "is\\nan"),
              t,
              gab_snative(gab, "is\\nan", gab_numlib_isnan),
          },
          {
              gab_message(gab, "is\\inf"),
              t,
              gab_snative(gab, "is\\inf", gab_numlib_isinf),
          },
          {
              gab_message(gab, "float\\between"),
              mod,
              gab_snative(gab, "float\\between", gab_numlib_between),
          });

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
