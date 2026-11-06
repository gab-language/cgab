/**
 *  MIT License
 *
 *  Copyright (c) 2023-2026 Teddy Randby
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

#include "cgab.h"

GAB_DYNLIB_NATIVE_FN(binary, at) {
  gab_value bin = gab_arg(0);
  gab_value idx = gab_arg(1);
  gab_value step = gab_arg(2);

  if (gab_valkind(bin) != kGAB_BINARY)
    return gab_pktypemismatch(gab, bin, kGAB_BINARY);

  if (gab_valkind(idx) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, idx, kGAB_NUMBER);

  if (step == gab_nil)
    step = gab_number(1);

  if (gab_valkind(step) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, step, kGAB_NUMBER);

  int64_t index = gab_valtoi(idx);
  uint64_t stp = gab_valtou(step);

  if (gab_valkind(step) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, step, kGAB_NUMBER);

  if (stp > 8)
    return gab_panicf(gab, "Step size cannot exceed 8 bytes: got $", step);

  size_t len = gab_strlen(bin);

  // Go from the back
  if (index < 0)
    index += (len / stp);

  size_t offset = index * stp;

  if (offset + stp > len)
    return gab_vmpush(gab_thisvm(gab), gab_none), gab_union_cvalid(gab_nil);

  const char *begin = gab_strdata(&bin) + (index * stp);

  int64_t result = 0;

  // Cast the char to unsigned - can't shift negative values.
  for (size_t i = 0; i < stp; i++)
    result |= ((unsigned char)begin[i]) << i;

  gab_vmpush(gab_thisvm(gab), gab_ok, gab_number(result));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(binary, len) {
  gab_value result = gab_number(gab_strlen(argv[0]));

  gab_vmpush(gab_thisvm(gab), result);
  return gab_union_cvalid(gab_nil);
};

GAB_DYNLIB_NATIVE_FN(binary, tos) {
  gab_value bin = gab_arg(0);

  gab_value str = gab_bintostr(bin);

  if (str == gab_cinvalid)
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Binary is not valid UTF-8"));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, str);

  return gab_union_cvalid(gab_nil);
};

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_arg(0);
  gab_value t = gab_msgtostr(mod);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              mod,
              t,
          },
          {
              gab_message(gab, "at"),
              t,
              gab_snative(gab, "at", gab_mod_binary_at),
          },
          {
              gab_message(gab, "len"),
              t,
              gab_snative(gab, "len", gab_mod_binary_len),
          },
          {
              gab_message(gab, "as\\string"),
              t,
              gab_snative(gab, "as\\string", gab_mod_binary_tos),
          }, );

  return (union gab_value_pair){
      .status = gab_ok,
      .vresult = mod,
  };
}
