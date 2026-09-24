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
#include <stdint.h>

GAB_DYNLIB_NATIVE_FN(blk, make) {
  gab_value bindings = gab_arg(1);
  gab_value ast = gab_arg(2);

  if (gab_valkind(bindings) != kGAB_RECORD)
    return gab_pktypemismatch(gab, bindings, kGAB_RECORD);

  if (gab_valkind(ast) != kGAB_RECORD)
    return gab_pktypemismatch(gab, ast, kGAB_RECORD);

  struct gab_src *src = gab_source(gab, ast, 0, nullptr);

  gab_value self = gab_listof(gab, gab_binary(gab, (uint8_t *)"self"));

  if (self == gab_cinvalid)
    return gab_union_ctimeout(gab_nil);

  gab_value env = gab_listof(
      gab, gab_recordof(gab, gab_binary(gab, (uint8_t *)"self"), gab_nil));

  if (env == gab_cinvalid)
    return gab_union_ctimeout(gab_nil);

  bindings = gab_lstcat(gab, self, bindings);

  if (bindings == gab_cinvalid)
    return gab_union_ctimeout(gab_nil);

  union gab_value_pair res = gab_compile(gab, src,
                                         (struct gab_compile_argt){
                                             .flags = fGAB_WITHOLD_ERR, // Prevent error from publishing
                                             .env = env,
                                             .ast = ast,
                                             .bindings = bindings,
                                         });

  gab_srccomplete(gab, src);

  if (res.status != gab_cvalid)
    return gab_push(gab, gab_err, res.vresult), gab_union_cvalid(gab_nil);

  gab_value b = gab_block(gab, res.vresult);
  return gab_push(gab, gab_ok, b), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_BLOCK);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "make"),
              gab_strtomsg(t),
              gab_snative(gab, "make", gab_mod_blk_make),
          });

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = gab_valarray(gab_ok, gab_strtomsg(t)),
  };
}
