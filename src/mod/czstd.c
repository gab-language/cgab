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

#include "zstd/build/single_file_libs/zstd.c"
#include "zstd/lib/zstd.h"

#include "cgab.h"
#include <stdint.h>

/*
 * TODO @czstd @opt: These functions use the simple compression/decompression API.
 *
 * It is probably best to use the streaming API, which would allow us to occasionally
 * yield this thread back to the gab runtime instead of blocking on compression/decompression
 * for ever.
 */

GAB_DYNLIB_NATIVE_FN(zstd, decode) {
  gab_value bin = gab_arg(0);

  if (gab_valkind(bin) != kGAB_BINARY)
    return gab_pktypemismatch(gab, bin, kGAB_BINARY);

  const char *data = gab_strdata(&bin);
  uint64_t len = gab_strlen(bin);

  unsigned long long size = ZSTD_decompressBound(data, len);

  if (ZSTD_isError(size)) {
    return gab_push(gab, gab_err,
                    gab_string(gab, ZSTD_getErrorName(size))),
           gab_union_cvalid(gab_nil);
  }

  if (size > UINT32_MAX)
    return gab_push(gab, gab_err, gab_string(gab, "Suspiciously large size"), gab_number(size)),
           gab_union_cvalid(gab_nil);

  void *dst = gab_fibmalloc(gab_thisfiber(gab), size);

  size_t decompressedLen = ZSTD_decompress(dst, size, data, len);

  if (ZSTD_isError(decompressedLen)) {
    return gab_push(gab, gab_err,
                    gab_string(gab, ZSTD_getErrorName(decompressedLen))),
           gab_union_cvalid(gab_nil);
  }

  return gab_push(gab, gab_ok, gab_nbinary(gab, decompressedLen, dst)),
         gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(zstd, encode) {
  gab_value bin = gab_arg(0);

  if (gab_valkind(bin) != kGAB_BINARY)
    return gab_pktypemismatch(gab, bin, kGAB_BINARY);

  const char *data = gab_strdata(&bin);
  uint64_t len = gab_strlen(bin);

  size_t size = ZSTD_compressBound(len);
  void *dst = gab_fibmalloc(gab_thisfiber(gab), size);

  size_t compressedLen = ZSTD_compress(dst, size, data, len, 10);

  /* I don't *expect* this to happen much, and the to\<smth> style api's shouldn't error */
  if (ZSTD_isError(compressedLen)) {
    return gab_panicf(gab, "zstd error: $",
                      gab_string(gab, ZSTD_getErrorName(compressedLen))),
           gab_union_cvalid(gab_nil);
  }

  return gab_push(gab, gab_nbinary(gab, compressedLen, dst)),
         gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_def(gab,
          {
              gab_message(gab, "to\\zstd"),
              gab_type(gab, kGAB_BINARY),
              gab_snative(gab, "to\\zstd", gab_mod_zstd_encode),
          },
          {
              gab_message(gab, "as\\zstd"),
              gab_type(gab, kGAB_BINARY),
              gab_snative(gab, "as\\zstd", gab_mod_zstd_decode),
          }, );

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = gab_valarray(gab_ok),
  };
}
