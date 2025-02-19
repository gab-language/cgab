#define JSMN_STATIC
#include "../vendor/jsmn/jsmn.h"

#include "gab.h"

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
      assert(false && "unreachable");
    }
    break;
  }
  case JSMN_STRING:
    *sp++ = gab_nstring(gab, tok.end - tok.start, json + tok.start);
    *t = *t + 1;
    break;
  case JSMN_OBJECT: {
    gab_value *save = sp;

    *t = *t + 1;

    for (int child = 0; child < tok.size * 2; child++)
      sp = push_value(gab, json, sp, tokens, t);

    sp = save;
    *sp++ = gab_record(gab, 2, tok.size, save, save + 1);
    break;
  }
  case JSMN_ARRAY: {
    gab_value *save = sp;

    *t = *t + 1;

    for (int child = 0; child < tok.size; child++)
      sp = push_value(gab, json, sp, tokens, t);

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

a_gab_value *gab_jsonlib_decode(struct gab_triple gab, uint64_t argc,
                                gab_value argv[static argc]) {
  gab_value str = gab_arg(0);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *cstr = gab_strdata(&str);
  uint64_t len = gab_strlen(str);

  jsmn_parser jsmn;
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
      gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Invalid character"));
      break;
    }
    return nullptr;
  }

  uint64_t token = 0;
  gab_value stack[len];

  gab_value *sp = push_value(gab, cstr, stack, tokens, &token);

  gab_value res = *(--sp);

  gab_vmpush(gab_thisvm(gab), gab_ok, res);
  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  gab_def(gab, {
                   gab_message(gab, "json\\decode"),
                   gab_type(gab, kGAB_STRING),
                   gab_snative(gab, "json\\decode", gab_jsonlib_decode),
               });

  return a_gab_value_one(gab_ok);
}
