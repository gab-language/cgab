#define BENCODE_IMPL
#include "bencode-c/bencode.h"

#include "gab.h"

int push_value(struct gab_triple gab, struct bencode *bncd,
               v_gab_value *stack) {
  int tok = bencode_next(bncd);

  switch (tok) {
  case BENCODE_INTEGER:
    /* Parse integer and push number */
    uint64_t i = atoll(bncd->tok);
    v_gab_value_push(stack, gab_number(i));
    break;
  case BENCODE_STRING:
    /* Create string and push */
    gab_value str = gab_nstring(gab, bncd->toklen, bncd->tok);
    v_gab_value_push(stack, str);
    break;
  case BENCODE_LIST_BEGIN: {
    size_t i = 0, save = stack->len;

    // TODO: Handle errors here.
    while (push_value(gab, bncd, stack) != BENCODE_LIST_END)
      i++;

    stack->len -= i;

    v_gab_value_push(stack, gab_list(gab, i, stack->data + save));
    break;
  }
  case BENCODE_DICT_BEGIN: {
    size_t i = 0, save = stack->len, res = 0;

    // TODO: Handle errors here.
    for (;;)
      switch ((res = push_value(gab, bncd, stack))) {
      case BENCODE_DICT_END:
        goto fin;
      case BENCODE_ERROR_BAD_KEY:
      case BENCODE_ERROR_INVALID:
      case BENCODE_ERROR_EOF:
      case BENCODE_ERROR_OOM:
          return res;
      default:
        i++;
      }

  fin:
    stack->len -= i;

    assert(i % 2 == 0);

    v_gab_value_push(stack, gab_record(gab, 2, i / 2, stack->data + save,
                                       stack->data + save + 1));
    break;
  }
  case BENCODE_ERROR_BAD_KEY:
  case BENCODE_ERROR_INVALID:
  case BENCODE_ERROR_EOF:
  case BENCODE_ERROR_OOM:
    break;
  }

  return tok;
}

GAB_DYNLIB_NATIVE_FN(bencode, decode) {
  gab_value str = gab_arg(0);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *cstr = gab_strdata(&str);
  uint64_t len = gab_strlen(str);

  struct bencode bncd;
  bencode_init(&bncd, cstr, len);

  v_gab_value stack = {};
  gab_gclock(gab);

  switch (push_value(gab, &bncd, &stack)) {
  case BENCODE_ERROR_BAD_KEY:
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Bad key"));
    break;
  case BENCODE_ERROR_INVALID:
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Invalid bencode"));
    break;
  case BENCODE_ERROR_EOF:
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Unexpected EOF"));
    break;
  case BENCODE_ERROR_OOM:
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Out of memory"));
    break;
  default:
    gab_vmpush(gab_thisvm(gab), gab_ok, v_gab_value_pop(&stack));
  }

  bencode_free(&bncd);
  v_gab_value_destroy(&stack);
  gab_gcunlock(gab);

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_def(gab, {
                   gab_message(gab, "as\\bencode"),
                   gab_type(gab, kGAB_STRING),
                   gab_snative(gab, "as\\bencode", gab_mod_bencode_decode),
               });

  gab_value res[] = {gab_ok};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
