#define JSMN_STATIC
#include "jsmn/jsmn.h"

#include "gab.h"

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
         * TODO: Handle unicode escaping as described in JSON spec json.org.
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

gab_value *push_value(struct gab_triple gab, const char *json, gab_value *sp,
                      jsmntok_t *tokens, uint64_t *t) {
  jsmntok_t tok = tokens[*t];
  switch (tok.type) {
  case JSMN_PRIMITIVE: {
    switch (json[tok.start]) {
    case 'n':
      *sp++ = gab_nil;
      *t = *t + 1;
      break;
    case 't':
      *sp++ = gab_true;
      *t = *t + 1;
      break;
    case 'f':
      *sp++ = gab_false;
      *t = *t + 1;
      break;
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      *sp++ = gab_number(atof(json + tok.start));
      *t = *t + 1;
      break;
    default:
      // Format and push some nice error here
      return nullptr;
    }
    break;
  }
  case JSMN_STRING: {
    char buf[tok.end - tok.start + 1];

    if (!unescape_into(buf, json + tok.start, tok.end - tok.start))
      assert(false && "unreachable");

    *sp++ = gab_string(gab, buf);
    *t = *t + 1;
    break;
  }
  case JSMN_OBJECT: {
    gab_value *save = sp;

    *t = *t + 1;

    for (int child = 0; child < tok.size * 2; child++) {
      sp = push_value(gab, json, sp, tokens, t);

      if (sp == nullptr)
        return nullptr;
    }

    sp = save;
    *sp++ = gab_record(gab, 2, tok.size, save, save + 1);
    break;
  }
  case JSMN_ARRAY: {
    gab_value *save = sp;

    *t = *t + 1;

    for (int child = 0; child < tok.size; child++) {
      sp = push_value(gab, json, sp, tokens, t);

      if (sp == nullptr)
        return nullptr;
    }

    sp = save;
    *sp++ = gab_list(gab, tok.size, save);
    break;
  }
  case JSMN_UNDEFINED:
    exit(1);
    break;
  }

  return sp;
}

GAB_DYNLIB_NATIVE_FN(json, decode) {
  gab_value str = gab_arg(0);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *cstr = gab_strdata(&str);
  uint64_t len = gab_strlen(str);

  jsmn_parser jsmn;
  // Maybe allocating on the stack isn't the safest? Apply a max here?
  jsmntok_t tokens[len];

  jsmn_init(&jsmn);

  int ntokens = jsmn_parse(&jsmn, cstr, len, tokens, len);

  if (ntokens <= 0) {
    switch (ntokens) {
    case 0:
    case JSMN_ERROR_PART:
      gab_vmpush(gab_thisvm(gab), gab_err,
                 gab_string(gab, "Incomplete JSON value"));
      break;
    case JSMN_ERROR_NOMEM:
      gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Internal Error"));
      break;
    case JSMN_ERROR_INVAL:
      gab_vmpush(gab_thisvm(gab), gab_err,
                 gab_string(gab, "Invalid character"));
      break;
    }
    return gab_union_cvalid(gab_nil);
  }

  uint64_t token = 0;
  gab_value stack[len];

  gab_value *sp = push_value(gab, cstr, stack, tokens, &token);

  if (sp == nullptr) {
    // Encountered an invalid token.
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Invalid JSON value"));
    return gab_union_cvalid(gab_nil);
  }

  gab_value res = *(--sp);

  gab_vmpush(gab_thisvm(gab), gab_ok, res);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_def(gab, {
                   gab_message(gab, "as\\json"),
                   gab_type(gab, kGAB_STRING),
                   gab_snative(gab, "as\\json", gab_mod_json_decode),
               });

  gab_value res[] = {gab_ok};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
