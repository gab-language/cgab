/*
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

#include "yxml/yxml.h"

#include "cgab.h"

#define T char
#include "vector.h"

struct xml_elem {
  v_char content;
  gab_value *base;
  uint64_t nattrs;
};

#define T struct xml_elem
#define NAME xml
#include "vector.h"

bool unescape_into(char *buf, const char *str, size_t len) {
  size_t buflen = 0;

  for (size_t i = 0; i < len; i++) {
    int8_t c = str[i];

    if (c == '\\') {
      switch (str[++i]) {
      case 'b':
        buf[buflen++] = '\b';
        break;
      case 'f':
        buf[buflen++] = '\f';
        break;
      case 'r':
        buf[buflen++] = '\r';
        break;
      case 'n':
        buf[buflen++] = '\n';
        break;
      case 't':
        buf[buflen++] = '\t';
        break;
      case '"':
        buf[buflen++] = '"';
        break;
      case '\\':
        buf[buflen++] = '\\';
        break;
      case '/':
        buf[buflen++] = '/';
        break;
        /**
         * TODO @cjson @bug: Handle unicode escaping as described in JSON spec
         * json.org.
         */
      // case 'u':
      //   i += 2;
      //
      //   if (str[i] != '[') {
      //     return nullptr;
      //   }
      //
      //   i++;
      //
      //   uint8_t cpl = 0;
      //   char codepoint[8] = {0};
      //
      //   while (str[i] != ']') {
      //
      //     if (cpl == 7)
      //       return nullptr;
      //
      //     codepoint[cpl++] = str[i++];
      //   }
      //
      //   i++;
      //
      //   long cp = strtol(codepoint, nullptr, 16);
      //
      //   int result = encode_codepoint(buf + buf_end, cp);
      //
      //   buf_end += result;
      //
      //   break;
      default:
        // Unrecognized escape sequence
        return false;
      }
    } else {
      buf[buflen++] = c;
    }
  }

  buf[buflen++] = '\0';
  return true;
}

gab_value *push_value(struct gab_triple gab, yxml_t *yxml, yxml_ret_t t,
                      v_char *data, v_xml *elems, gab_value *sp) {

  if (t < 0) {
    return nullptr;
  }

  switch (t) {
  case YXML_OK:
    break;
  case YXML_ELEMSTART: {
    v_xml_push(elems, (struct xml_elem){
                          .base = sp,
                      });
    *sp++ = gab_string(gab, yxml->elem);
    break;
  }
  case YXML_ATTRSTART: {
    *sp++ = gab_string(gab, yxml->attr);
    data->len = 0;
    break;
  }
  case YXML_ATTRVAL: {
    v_char_push(data, yxml->data[0]);
    break;
  }
  case YXML_ATTREND: {
    v_xml_ref_at(elems, elems->len - 1)->nattrs++;
    *sp++ = gab_nstring(gab, data->len, data->data);
    data->len = 0;
    break;
  }
  case YXML_CONTENT: {
    v_char *content = &v_xml_ref_at(elems, elems->len - 1)->content;
    v_char_push(content, yxml->data[0]);
    break;
  }
  case YXML_ELEMEND: {
    struct xml_elem elem = v_xml_pop(elems);

    uint64_t stkspace = sp - elem.base;

    uint64_t nchildren = stkspace - 1 - (elem.nattrs * 2);

    gab_value attrs =
        gab_record(gab, 2, elem.nattrs, elem.base + 1, elem.base + 2);

    memcpy(elem.base + 3, sp - nchildren, nchildren * sizeof(gab_value));
    // gab_value children = gab_list(gab, 1, nchildren, sp - nchildren);

    sp = elem.base + 1;

    *sp++ = attrs;

    *sp++ = gab_nstring(gab, elem.content.len, elem.content.data);

    sp = elem.base;
    *sp++ = gab_list(gab, 1, 3 + nchildren, elem.base);

    v_char_destroy(&elem.content);
    break;
  }
  default:
    gab_unreachable("Reached kind %d\n", t);
    return nullptr;
  }

  return sp;
}

GAB_DYNLIB_NATIVE_FN(xml, decode) {
  gab_value str = gab_arg(0);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *cstr = gab_strdata(&str);
  uint64_t len = gab_strlen(str);

  yxml_t yxml;
  // TODO @cxml @bug: Maybe allocating on the stack isn't the safest? Apply a
  // max here?
  char buf[len * 8];
  yxml_init(&yxml, buf, len * 8);

  v_char data = {};
  v_xml elems = {};

  gab_value stack[len];
  gab_value *sp = stack;

  yxml_ret_t code;
  for (uint64_t i = 0; i < len; i++) {
    code = yxml_parse(&yxml, cstr[i]);
    sp = push_value(gab, &yxml, code, &data, &elems, sp);

    // Encountered an invalid token.
    if (sp == nullptr)
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Invalid XML value: $"),
                        gab_number(code)),
             gab_union_cvalid(gab_nil);
  }

  code = yxml_eof(&yxml);
  if (code < 0)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid XML value: $"),
                      gab_number(code)),
           gab_union_cvalid(gab_nil);

  gab_vmpush(gab_thisvm(gab), gab_ok, stack[0]);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_def(gab, {
                   gab_message(gab, "as\\xml"),
                   gab_type(gab, kGAB_STRING),
                   gab_snative(gab, "as\\xml", gab_mod_xml_decode),
               });

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = gab_valarray(gab_ok),
  };
}
