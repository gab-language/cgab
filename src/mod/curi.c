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
#include "furi/code/furi/furi.h"

#include "gab.h"

#define M_URL_SCHEME "uri\\scheme"
#define M_URL_AUTHORITY "uri\\authority"
#define M_URL_PATH "uri\\path"
#define M_URL_QUERY "uri\\query"
#define M_URL_FRAGMENT "uri\\fragment"

gab_value furi_sv_to_value(struct gab_triple gab, furi_sv sv) {
  return gab_nstring(gab, furi_sv_length(sv), sv.begin);
}

gab_value build_path(struct gab_triple gab, furi_sv path) {
  v_gab_value elems = {0};
  for (furi_path_iter iter = furi_make_path_iter_begin(path);
       !furi_path_iter_is_done(iter); furi_path_iter_next(&iter)) {
    v_gab_value_push(&elems,
                     furi_sv_to_value(gab, furi_path_iter_get_value(iter)));
  }
  if (elems.len) {
    gab_value vpath = gab_list(gab, 1, elems.len, elems.data);
    v_gab_value_destroy(&elems);
    return vpath;
  } else {
    return gab_listof(gab);
  }
};

gab_value build_query(struct gab_triple gab, furi_sv query) {
  v_gab_value elems = {0};

  for (furi_query_iter iter = furi_make_query_iter_begin(query);
       !furi_query_iter_is_done(iter); furi_query_iter_next(&iter)) {

    furi_query_iter_value elem = furi_query_iter_get_value(iter);
    v_gab_value_push(&elems, furi_sv_to_value(gab, elem.key));
    v_gab_value_push(&elems, furi_sv_to_value(gab, elem.value));
  }

  if (elems.len) {
    gab_value vquery =
        gab_record(gab, 2, elems.len / 2, elems.data, elems.data + 1);
    v_gab_value_destroy(&elems);
    return vquery;
  } else {
    return gab_listof(gab);
  }
};

gab_value build_url(struct gab_triple gab, const char *ptr, size_t len) {
  furi_uri_split split = furi_split_uri(furi_make_sv(ptr, ptr + len));

  // TODO @cgab @perf: Use the fiber arena instead.
  v_gab_value kvps = {0};

  if (furi_sv_is_null(split.path))
    return gab_cundefined;

  v_gab_value_push(&kvps, gab_message(gab, M_URL_SCHEME));
  v_gab_value_push(&kvps, furi_sv_to_value(gab, split.scheme));

  // TODO @curi @api: Pull this out appropriately
  v_gab_value_push(&kvps, gab_message(gab, M_URL_AUTHORITY));
  v_gab_value_push(&kvps, furi_sv_to_value(gab, split.authority));

  v_gab_value_push(&kvps, gab_message(gab, M_URL_PATH));
  v_gab_value_push(&kvps, build_path(gab, split.path));

  v_gab_value_push(&kvps, gab_message(gab, M_URL_QUERY));
  v_gab_value_push(&kvps, build_query(gab, split.query));

  v_gab_value_push(&kvps, gab_message(gab, M_URL_FRAGMENT));
  v_gab_value_push(&kvps, furi_sv_to_value(gab, split.fragment));

  gab_value url = gab_record(gab, 2, kvps.len / 2, kvps.data, kvps.data + 1);
  v_gab_value_destroy(&kvps);

  return url;
}

GAB_DYNLIB_NATIVE_FN(uri, decode) {
  gab_value vuri = gab_arg(0);

  gab_value res = build_url(gab, gab_strdata(&vuri), gab_strlen(vuri));
  if (res == gab_cundefined)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Failed to parse uri")),
           gab_union_cvalid(gab_nil);
  else
    return gab_vmpush(gab_thisvm(gab), gab_ok, res), gab_union_cvalid(gab_nil);
}

#define _______ "\0\0\0\0"
static const char uri_encode_tbl[ sizeof(int32_t) * 0x100 ] = {
/*  0       1       2       3       4       5       6       7       8       9       a       b       c       d       e       f                        */
    "%00\0" "%01\0" "%02\0" "%03\0" "%04\0" "%05\0" "%06\0" "%07\0" "%08\0" "%09\0" "%0A\0" "%0B\0" "%0C\0" "%0D\0" "%0E\0" "%0F\0"  /* 0:   0 ~  15 */
    "%10\0" "%11\0" "%12\0" "%13\0" "%14\0" "%15\0" "%16\0" "%17\0" "%18\0" "%19\0" "%1A\0" "%1B\0" "%1C\0" "%1D\0" "%1E\0" "%1F\0"  /* 1:  16 ~  31 */
    "%20\0" "%21\0" "%22\0" "%23\0" "%24\0" "%25\0" "%26\0" "%27\0" "%28\0" "%29\0" "%2A\0" "%2B\0" "%2C\0" _______ _______ "%2F\0"  /* 2:  32 ~  47 */
    _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ "%3A\0" "%3B\0" "%3C\0" "%3D\0" "%3E\0" "%3F\0"  /* 3:  48 ~  63 */
    "%40\0" _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______  /* 4:  64 ~  79 */
    _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ "%5B\0" "%5C\0" "%5D\0" "%5E\0" _______  /* 5:  80 ~  95 */
    "%60\0" _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______  /* 6:  96 ~ 111 */
    _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ "%7B\0" "%7C\0" "%7D\0" _______ "%7F\0"  /* 7: 112 ~ 127 */
    "%80\0" "%81\0" "%82\0" "%83\0" "%84\0" "%85\0" "%86\0" "%87\0" "%88\0" "%89\0" "%8A\0" "%8B\0" "%8C\0" "%8D\0" "%8E\0" "%8F\0"  /* 8: 128 ~ 143 */
    "%90\0" "%91\0" "%92\0" "%93\0" "%94\0" "%95\0" "%96\0" "%97\0" "%98\0" "%99\0" "%9A\0" "%9B\0" "%9C\0" "%9D\0" "%9E\0" "%9F\0"  /* 9: 144 ~ 159 */
    "%A0\0" "%A1\0" "%A2\0" "%A3\0" "%A4\0" "%A5\0" "%A6\0" "%A7\0" "%A8\0" "%A9\0" "%AA\0" "%AB\0" "%AC\0" "%AD\0" "%AE\0" "%AF\0"  /* A: 160 ~ 175 */
    "%B0\0" "%B1\0" "%B2\0" "%B3\0" "%B4\0" "%B5\0" "%B6\0" "%B7\0" "%B8\0" "%B9\0" "%BA\0" "%BB\0" "%BC\0" "%BD\0" "%BE\0" "%BF\0"  /* B: 176 ~ 191 */
    "%C0\0" "%C1\0" "%C2\0" "%C3\0" "%C4\0" "%C5\0" "%C6\0" "%C7\0" "%C8\0" "%C9\0" "%CA\0" "%CB\0" "%CC\0" "%CD\0" "%CE\0" "%CF\0"  /* C: 192 ~ 207 */
    "%D0\0" "%D1\0" "%D2\0" "%D3\0" "%D4\0" "%D5\0" "%D6\0" "%D7\0" "%D8\0" "%D9\0" "%DA\0" "%DB\0" "%DC\0" "%DD\0" "%DE\0" "%DF\0"  /* D: 208 ~ 223 */
    "%E0\0" "%E1\0" "%E2\0" "%E3\0" "%E4\0" "%E5\0" "%E6\0" "%E7\0" "%E8\0" "%E9\0" "%EA\0" "%EB\0" "%EC\0" "%ED\0" "%EE\0" "%EF\0"  /* E: 224 ~ 239 */
    "%F0\0" "%F1\0" "%F2\0" "%F3\0" "%F4\0" "%F5\0" "%F6\0" "%F7\0" "%F8\0" "%F9\0" "%FA\0" "%FB\0" "%FC\0" "%FD\0" "%FE\0" "%FF"    /* F: 240 ~ 255 */
};
#undef _______

#define __ 0xFF
static const unsigned char hexval[0x100] = {
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* 00-0F */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* 10-1F */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* 20-2F */
   0, 1, 2, 3, 4, 5, 6, 7, 8, 9,__,__,__,__,__,__, /* 30-3F */
  __,10,11,12,13,14,15,__,__,__,__,__,__,__,__,__, /* 40-4F */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* 50-5F */
  __,10,11,12,13,14,15,__,__,__,__,__,__,__,__,__, /* 60-6F */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* 70-7F */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* 80-8F */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* 90-9F */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* A0-AF */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* B0-BF */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* C0-CF */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* D0-DF */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* E0-EF */
  __,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__, /* F0-FF */
};
#undef __

size_t uri_encode (const char *src, const size_t len, char *dst)
{
  size_t i = 0, j = 0;
  while (i < len)
  {
    const char octet = src[i++];
    const int32_t code = ((int32_t*)uri_encode_tbl)[ (unsigned char)octet ];
    if (code) {
        *((int32_t*)&dst[j]) = code;
        j += 3;
    }
    else dst[j++] = octet;
  }
  dst[j] = '\0';
  return j;
}

size_t uri_decode (const char *src, const size_t len, char *dst)
{
  size_t i = 0, j = 0;
  while(i < len)
  {
    int copy_char = 1;
    if(src[i] == '%' && i + 2 < len)
    {
      const unsigned char v1 = hexval[ (unsigned char)src[i+1] ];
      const unsigned char v2 = hexval[ (unsigned char)src[i+2] ];

      /* skip invalid hex sequences */
      if ((v1 | v2) != 0xFF)
      {
        dst[j] = (v1 << 4) | v2;
        j++;
        i += 3;
        copy_char = 0;
      }
    }
    if (copy_char)
    {
      dst[j] = src[i];
      i++;
      j++;
    }
  }
  dst[j] = '\0';
  return j;
}

GAB_DYNLIB_NATIVE_FN(uri, encode_pe) {
  gab_value vuri = gab_arg(0);

  size_t len = gab_strlen(vuri);

  // Upperbound for string encoding size.
  char buf[len * 3 + 1] = {};

  len = uri_encode(gab_strdata(&vuri), len, buf);

  gab_value res = gab_nstring(gab, len, buf);
  return gab_vmpush(gab_thisvm(gab), res), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(uri, decode_pe) {
  gab_value vuri = gab_arg(0);

  size_t len = gab_strlen(vuri);

  // Upperbound for string decoding size.
  char buf[len + 1] = {};

  len = uri_decode(gab_strdata(&vuri), len, buf);

  gab_value res = gab_nstring(gab, len, buf);
  return gab_vmpush(gab_thisvm(gab), res), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "uri");

  gab_def(gab,
          {
              gab_message(gab, "as\\uri"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "as\\uri", gab_mod_uri_decode),
          },
          {
              gab_message(gab, "to\\uri\\encoded"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "to\\uri\\encoded", gab_mod_uri_encode_pe),
          },
          {
              gab_message(gab, "as\\uri\\encoded"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "as\\uri\\encoded", gab_mod_uri_decode_pe),
          });

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
