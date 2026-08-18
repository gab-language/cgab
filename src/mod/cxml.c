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

#define HOXML_IMPLEMENTATION
#include "hoxml/hoxml.h"

#include "cgab.h"

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

gab_value *push_value(struct gab_triple gab, hoxml_context_t *hoxml,
                      const char *content, size_t content_length,
                      hoxml_code_t t, gab_value *sp) {

  if (t < 0) {
    return nullptr;
  }

  switch (t) {
  case HOXML_ATTRIBUTE: {
    size_t len = strlen(hoxml->attribute);
    char buf[len];

    if (!unescape_into(buf, hoxml->attribute, len))
      assert(false && "unreachable");

    *sp++ = gab_string(gab, buf);

    if (hoxml->value) {
      len = strlen(hoxml->value);
      char valbuf[len];

      if (!unescape_into(valbuf, hoxml->value, len))
        assert(false && "unreachable");

      *sp++ = gab_string(gab, valbuf);
    } else {
      *sp++ = gab_true;
    }

    break;
  }
  case HOXML_ELEMENT_BEGIN: {
    gab_value *save = sp;

    *sp++ = gab_string(gab, hoxml->tag);

    hoxml_code_t code;
    while ((code = hoxml_parse(hoxml, content, content_length)) ==
           HOXML_ATTRIBUTE) {
      sp = push_value(gab, hoxml, content, content_length, code, sp);

      if (sp == nullptr)
        return nullptr;
    }

    size_t attrs = sp - (save + 1);
    gab_assert(attrs % 2 == 0, "Should have even elements after attrs");

    sp = save + 1;
    *sp++ = gab_record(gab, 2, attrs / 2, save + 1, save + 2);

    if (code == HOXML_ELEMENT_END)
      goto done;

    sp = push_value(gab, hoxml, content, content_length, code, sp);

    if (sp == nullptr)
      return nullptr;

    while ((code = hoxml_parse(hoxml, content, content_length)) !=
           HOXML_ELEMENT_END) {
      sp = push_value(gab, hoxml, content, content_length, code, sp);

      if (sp == nullptr)
        return nullptr;
    }

  done:
    if (hoxml->content)
      *sp++ = gab_string(gab, hoxml->content);

    size_t children = sp - (save);

    sp = save;
    *sp++ = gab_list(gab, 1, children, save);
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

  hoxml_context_t hoxml;
  // TODO @cxml @bug: Maybe allocating on the stack isn't the safest? Apply a
  // max here?
  char buf[len * 10];
  hoxml_init(&hoxml, buf, len * 10);

  gab_value stack[len];
  gab_value *sp = stack;

  hoxml_code_t code;
  while ((code = hoxml_parse(&hoxml, cstr, len)) != HOXML_END_OF_DOCUMENT) {
    sp = push_value(gab, &hoxml, cstr, len, code, sp);

    if (sp == nullptr) {
      // Encountered an invalid token.
      gab_vmpush(gab_thisvm(gab), gab_err,
                 gab_string(gab, "Invalid XML value"));
      return gab_union_cvalid(gab_nil);
    }
  }

  gab_value res = gab_list(gab, 1, sp - stack, stack);

  gab_vmpush(gab_thisvm(gab), gab_ok, res);
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
