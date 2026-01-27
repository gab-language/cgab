#include "core.h"
#include "engine.h"
#include "gab.h"
#include <stddef.h>
#include <stdint.h>

#define FMT_EXPECTED_EXPRESSION                                                \
  "Expected a value - one of:\n\n"                                             \
  "  " GAB_YELLOW "-1.23" GAB_MAGENTA "\t\t\t# A number \n" GAB_RESET          \
  "  " GAB_GREEN "'hello, Joe!'" GAB_MAGENTA "\t\t# A string \n" GAB_RESET     \
  "  " GAB_RED "greet:" GAB_MAGENTA "\t\t# A message\n" GAB_RESET              \
  "  " GAB_BLUE "x => x + 1" GAB_MAGENTA "\t# A block \n" GAB_RESET            \
  "  " GAB_CYAN "{ key: value }" GAB_MAGENTA "\t# A record\n" GAB_RESET "  "   \
  "(" GAB_YELLOW "-1.23" GAB_RESET ", " GAB_GREEN "true:" GAB_RESET            \
  ")" GAB_MAGENTA "\t# A tuple\n" GAB_RESET "  "                               \
  "a_variable" GAB_MAGENTA "\t\t# Or a variable!" GAB_RESET

#define FMT_REFERENCE_BEFORE_INIT "$ is referenced before it is initialized."

#define FMT_ID_NOT_FOUND "Variable $ is not defined in this scope."

#define FMT_MALFORMED_ASSIGNMENT                                               \
  "This assignment is malformed - a valid assignment looks like:\n\n"          \
  "  a = " GAB_YELLOW "1" GAB_MAGENTA                                          \
  "\t\t\t# A single variable and expression\n" GAB_RESET " " GAB_BLACK         \
  "=> a = 1\n" GAB_RESET "  (a, b) = (" GAB_YELLOW "1" GAB_RESET ", " GAB_RED  \
  "bark:" GAB_RESET ")" GAB_MAGENTA                                            \
  "\t# A tuple of variables and expressions\n" GAB_RESET " " GAB_BLACK         \
  "=> a = 1, b = bark:\n" GAB_RESET "  (a*, b) = (" GAB_YELLOW "1" GAB_RESET   \
  ", " GAB_YELLOW "2" GAB_RESET ", " GAB_YELLOW "3" GAB_RESET ")" GAB_MAGENTA  \
  "\t# Specify one variable to collect extra values with '*'\n" GAB_RESET      \
  " " GAB_BLACK "=> a = [1, 2], b = 3\n" GAB_RESET "  (a**) = (" GAB_RED       \
  "num:" GAB_RESET ", " GAB_YELLOW "2" GAB_RESET ")" GAB_MAGENTA               \
  "\t# Specify one variable to zip extra values with '**'\n" GAB_RESET         \
  " " GAB_BLACK "=> a = { num: 2 }\n" GAB_RESET

#define FMT_MALFORMED_ASSIGNMENT_NOTE "\nHint: "

/*
 *******
 * AST *
 *******

  The gab ast is built of two kinds of nodes:
    - Sends  (behavior)
    - Values (data)

  VALUE NODE

  1       => [ 1 ]
  (1,2,3) => [1, 2, 3]

  Simply a list of 0 or more values

  SEND VALUE

  1 + 1   => [{ gab.lhs: [1], gab.msg: +, gab.rhs: [1] }]

  [ 1, 2 ] => [{ gab.lhs: [:gab.list], gab.msg: make, gab.rhs: [1, 2] }]

  Simply a record with a lhs, rhs, and msg.

  BLOCK VALUE

  do            => [[
    b .= 1 + 1       { gab.lhs: [b], gab.msg: =, gab.rhs: [{ 1, b, 1}] },
    b.toString       { gab.lhs: [b], gab.msg: toString, gab.rhs: [] }
  end             ]]

  And thats it! All Gab code can be described here. There are some nuances
 though:

      - Blocks (more specifically, prototypes) are given a shape just like
 records have.
      - gab_compile() accepts a *shape* as an argument. This shape determines
 the environment available as the AST is compiled into a block.
          -> How does this handle nested scopes and chaining?
          -> How do we implement load_local/load_upvalue
 */

struct parser {
  struct gab_src *src;
  size_t offset;
  gab_value err;
};

struct bc {
  v_uint8_t bc;
  v_uint64_t bc_toks;
  v_gab_value *ks;

  struct gab_src *src;

  uint8_t prev_op, pprev_op;
  size_t prev_op_at;

  gab_value err;
};

enum prec_k { kNONE, kEXP, kBINARY_SEND, kSEND, kSPECIAL_SEND, kPRIMARY };

typedef gab_value (*parse_f)(struct gab_triple gab, struct parser *,
                             gab_value lhs);

struct parse_rule {
  parse_f prefix;
  parse_f infix;
  enum prec_k prec;
};

struct parse_rule get_parse_rule(gab_token k);

/*static size_t prev_line(struct parser *parser) {*/
/*  return v_uint64_t_val_at(&parser->src->token_lines, parser->offset - 1);*/
/*}*/

static gab_token curr_tok(struct parser *parser) {
  return v_gab_token_val_at(&parser->src->tokens, parser->offset);
}

static bool curr_prefix(struct parser *parser) {
  return get_parse_rule(curr_tok(parser)).prefix != nullptr;
}

static gab_token prev_tok(struct parser *parser) {
  return v_gab_token_val_at(&parser->src->tokens, parser->offset - 1);
}

static s_char prev_src(struct parser *parser) {
  return v_s_char_val_at(&parser->src->token_srcs, parser->offset - 1);
}

static gab_value prev_id(struct gab_triple gab, struct parser *parser) {
  s_char s = prev_src(parser);

  return gab_nstring(gab, s.len, s.data);
}

bool msg_is_specialform(struct gab_triple gab, gab_value msg) {
  if (msg == gab_message(gab, mGAB_ASSIGN))
    return true;

  if (msg == gab_message(gab, mGAB_BLOCK))
    return true;

  return false;
}

static int encode_codepoint(char *out, int utf) {
  if (utf <= 0x7F) {
    // Plain ASCII
    out[0] = (char)utf;
    return 1;
  } else if (utf <= 0x07FF) {
    // 2-byte unicode
    out[0] = (char)(((utf >> 6) & 0x1F) | 0xC0);
    out[1] = (char)(((utf >> 0) & 0x3F) | 0x80);
    return 2;
  } else if (utf <= 0xFFFF) {
    // 3-byte unicode
    out[0] = (char)(((utf >> 12) & 0x0F) | 0xE0);
    out[1] = (char)(((utf >> 6) & 0x3F) | 0x80);
    out[2] = (char)(((utf >> 0) & 0x3F) | 0x80);
    return 3;
  } else if (utf <= 0x10FFFF) {
    // 4-byte unicode
    out[0] = (char)(((utf >> 18) & 0x07) | 0xF0);
    out[1] = (char)(((utf >> 12) & 0x3F) | 0x80);
    out[2] = (char)(((utf >> 6) & 0x3F) | 0x80);
    out[3] = (char)(((utf >> 0) & 0x3F) | 0x80);
    return 4;
  } else {
    // error - use replacement character
    out[0] = (char)0xEF;
    out[1] = (char)0xBF;
    out[2] = (char)0xBD;
    return 3;
  }
}

static a_char *parse_raw_str(struct parser *parser, s_char raw_str) {
  // The parsed string will be at most as long as the raw string.
  // (\n -> one char)
  char buffer[raw_str.len];
  size_t buf_end = 0;

  // Skip the first and last bytes of the string.
  // These are the opening/closing quotes, doublequotes, or brackets (For
  // interpolation).
  for (size_t i = 1; i < raw_str.len - 1; i++) {
    int8_t c = raw_str.data[i];

    if (c == '\\') {

      switch (raw_str.data[i + 1]) {

      case 'r':
        buffer[buf_end++] = '\r';
        break;
      case 'n':
        buffer[buf_end++] = '\n';
        break;
      case 't':
        buffer[buf_end++] = '\t';
        break;
      case '{':
        buffer[buf_end++] = '{';
        break;
      case '"':
        buffer[buf_end++] = '"';
        break;
      case '0':
        buffer[buf_end++] = '\0';
        break;
      case '\'':
        buffer[buf_end++] = '\'';
        break;
      case '\\':
        buffer[buf_end++] = '\\';
        break;
      case 'e':
        buffer[buf_end++] = '\033';
        break;
      case 'u':
        i += 2;

        if (raw_str.data[i] != '[') {
          return nullptr;
        }

        i++;

        uint8_t cpl = 0;
        char codepoint[8] = {0};

        while (raw_str.data[i] != ']') {

          if (cpl == 7)
            return nullptr;

          codepoint[cpl++] = raw_str.data[i++];
        }

        i++;

        long cp = strtol(codepoint, nullptr, 16);

        int result = encode_codepoint(buffer + buf_end, cp);

        buf_end += result;

        break;
      default:
        return nullptr;
      }

      i++;

    } else {
      buffer[buf_end++] = c;
    }
  }

  return a_char_create(buffer, buf_end);
};

static gab_value trimfront_prev_id(struct gab_triple gab,
                                   struct parser *parser) {
  s_char s = prev_src(parser);

  s.data++;
  s.len--;

  // These can cause collections during compilation.
  return gab_nstring(gab, s.len, s.data);
}

static gab_value trimback_prev_id(struct gab_triple gab,
                                  struct parser *parser) {
  s_char s = prev_src(parser);

  s.len--;

  // These can cause collections during compilation.
  return gab_nstring(gab, s.len, s.data);
}

static gab_value trim_prev_id(struct gab_triple gab, struct parser *parser) {
  s_char s = prev_src(parser);

  s.data++;
  s.len -= 2;

  // These can cause collections during compilation.
  return gab_nstring(gab, s.len, s.data);
}

static inline bool match_token(struct parser *parser, gab_token tok) {
  return v_gab_token_val_at(&parser->src->tokens, parser->offset) == tok;
}

static int vparser_error(struct gab_triple gab, struct parser *parser,
                         enum gab_status e, const char *fmt, va_list args) {
  parser->err = gab_vspanicf(gab, args,
                             (struct gab_err_argt){
                                 .src = parser->src,
                                 .status = e,
                                 .tok = parser->offset - 1,
                                 .note_fmt = fmt,
                             });

  va_end(args);

  return 0;
}

static int parser_error(struct gab_triple gab, struct parser *parser,
                        enum gab_status e, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  return vparser_error(gab, parser, e, fmt, args);
}

static int eat_token(struct gab_triple gab, struct parser *parser) {
  if (match_token(parser, TOKEN_EOF))
    return parser_error(gab, parser, GAB_UNEXPECTED_EOF,
                        "Unexpectedly reached the end of input.");

  parser->offset++;

  if (match_token(parser, TOKEN_ERROR)) {
    eat_token(gab, parser);
    return parser_error(gab, parser, GAB_MALFORMED_TOKEN,
                        "This token is malformed or unrecognized.");
  }

  return 1;
}

static inline int match_and_eat_token_of(struct gab_triple gab,
                                         struct parser *parser, size_t len,
                                         gab_token tok[len]) {
  for (size_t i = 0; i < len; i++)
    if (match_token(parser, tok[i]))
      return (tok[i] == TOKEN_EOF) ? 1 : eat_token(gab, parser);

  return 0;
}

#define match_and_eat_token(gab, parser, ...)                                  \
  ({                                                                           \
    gab_token toks[] = {__VA_ARGS__};                                          \
    match_and_eat_token_of(gab, parser, sizeof(toks) / sizeof(gab_token),      \
                           toks);                                              \
  })

gab_value parse_expression(struct gab_triple gab, struct parser *parser,
                           enum prec_k prec);

static inline void skip_newlines(struct gab_triple gab, struct parser *parser) {
  while (match_and_eat_token(gab, parser, TOKEN_NEWLINE))
    ;
}

size_t node_getinfo_begin(struct gab_src *src, gab_value node) {
  assert(d_uint64_t_exists(&src->node_begin_toks, node));
  return d_uint64_t_read(&src->node_begin_toks, node);
}

size_t node_getinfo_end(struct gab_src *src, gab_value node) {
  assert(d_uint64_t_exists(&src->node_end_toks, node));
  return d_uint64_t_read(&src->node_end_toks, node);
}

gab_value node_storeinfo(struct gab_src *src, gab_value node, size_t begin,
                         size_t end) {
  d_uint64_t_insert(&src->node_begin_toks, node, begin);
  d_uint64_t_insert(&src->node_end_toks, node, end);
  return node;
}

gab_value node_stealinfo(struct gab_src *src, gab_value from, gab_value to) {
  size_t begin = d_uint64_t_read(&src->node_begin_toks, from);
  size_t end = d_uint64_t_read(&src->node_end_toks, from);
  return node_storeinfo(src, to, begin, end);
}

gab_value node_value(struct gab_triple gab, gab_value node) {
  return gab_list(gab, 1, &node);
}

gab_value node_empty(struct gab_triple gab, struct parser *parser) {
  gab_value empty = gab_listof(gab);
  node_storeinfo(parser->src, empty, parser->offset, parser->offset);
  return empty;
  ;
}

bool node_isempty(gab_value node) {
  return gab_valkind(node) == kGAB_RECORD && gab_reclen(node) == 0;
}

bool node_issend(struct gab_triple gab, gab_value node) {
  if (gab_valkind(node) != kGAB_RECORD)
    return false;

  if (gab_recisl(node))
    return false;

  return !msg_is_specialform(gab,
                             gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG));
}

bool node_ismulti(struct gab_triple gab, gab_value node) {
  if (gab_valkind(node) != kGAB_RECORD)
    return false;

  switch (gab_valkind(gab_recshp(node))) {
  case kGAB_SHAPE:
    return !msg_is_specialform(gab,
                               gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG));
  case kGAB_SHAPELIST: {
    size_t len = gab_reclen(node);

    if (len == 0)
      return false;

    for (size_t i = 0; i < len; i++) {
      gab_value child_node = gab_uvrecat(node, i);
      if (node_ismulti(gab, child_node))
        return true;
    }

    return false;
  }
  default:
    assert(false && "UNREACHABLE");
    return false;
  }
}

size_t node_len(struct gab_triple gab, gab_value node);

gab_value node_tuple_lastnode(gab_value node) {
  assert(gab_valkind(node) == kGAB_RECORD);
  assert(gab_valkind(gab_recshp(node)) == kGAB_SHAPELIST);
  size_t len = gab_reclen(node);
  assert(len > 0);
  return gab_uvrecat(node, len - 1);
}

size_t node_valuelen(struct gab_triple gab, gab_value node) {
  // If this value node is a block, get the
  // last tuple in the block and return that
  // tuple's len
  if (gab_valkind(node) == kGAB_RECORD)
    if (gab_valkind(gab_recshp(node)) == kGAB_SHAPELIST)
      if (gab_reclen(node) > 0)
        return node_len(gab, node_tuple_lastnode(node));

  // Otherwise, values are 1 long
  return 1;
}

size_t node_len(struct gab_triple gab, gab_value node) {
  if (gab_valkind(node) != kGAB_RECORD)
    return 0;

  assert(gab_valkind(node) == kGAB_RECORD);
  assert(gab_valkind(gab_recshp(node)) == kGAB_SHAPELIST);

  size_t len = gab_reclen(node);
  size_t total_len = 0;

  // Tuple's length are the sum of their children
  // If the tuple as a whole is multi, subtract one
  // for that send
  for (size_t i = 0; i < len; i++)
    total_len += node_valuelen(gab, gab_uvrecat(node, i));

  return total_len;
}

gab_value node_send(struct gab_triple gab, gab_value lhs, gab_value msg,
                    gab_value rhs) {
  static const char *keys[] = {
      mGAB_AST_NODE_SEND_LHS,
      mGAB_AST_NODE_SEND_MSG,
      mGAB_AST_NODE_SEND_RHS,
  };

  gab_value vals[] = {
      lhs,
      msg,
      rhs,
  };

  return node_value(gab, gab_srecord(gab, 3, keys, vals));
}

static gab_value parse_expressions_body(struct gab_triple gab,
                                        struct parser *parser,
                                        enum gab_token t) {
  size_t begin = parser->offset;

  gab_value result = node_empty(gab, parser);

  skip_newlines(gab, parser);

  while (!match_and_eat_token(gab, parser, t)) {
    skip_newlines(gab, parser);

    gab_value exp = parse_expression(gab, parser, kEXP);

    if (exp == gab_cinvalid)
      return gab_cinvalid;

    gab_value tup = node_value(gab, exp);
    node_stealinfo(parser->src, exp, tup);

    result = gab_lstcat(gab, result, tup);

    if (result == gab_cinvalid)
      return gab_cinvalid;

    skip_newlines(gab, parser);
  }

  size_t end = parser->offset;

  gab_value res = node_value(gab, result);

  node_storeinfo(parser->src, res, begin, end);

  return res;
}

gab_value parse_expressions_until(struct gab_triple gab, struct parser *parser,
                                  enum gab_token t) {
  size_t begin = parser->offset;

  gab_value result = node_empty(gab, parser);

  skip_newlines(gab, parser);

  while (!match_and_eat_token(gab, parser, t)) {
    skip_newlines(gab, parser);

    gab_value exp = parse_expression(gab, parser, kEXP);

    if (exp == gab_cinvalid)
      return gab_cinvalid;

    result = gab_lstcat(gab, result, exp);

    if (result == gab_cinvalid)
      return gab_cinvalid;

    skip_newlines(gab, parser);
  }

  size_t end = parser->offset;

  node_storeinfo(parser->src, result, begin, end);

  return result;
}

gab_value parse_expression(struct gab_triple gab, struct parser *parser,
                           enum prec_k prec) {
  if (!eat_token(gab, parser))
    return gab_cinvalid;

  size_t tok = prev_tok(parser);

  struct parse_rule rule = get_parse_rule(tok);

  if (rule.prefix == nullptr)
    return parser_error(gab, parser, GAB_MALFORMED_EXPRESSION,
                        FMT_EXPECTED_EXPRESSION),
           gab_cinvalid;

  size_t begin = parser->offset;

  gab_value node = rule.prefix(gab, parser, gab_cinvalid);

  size_t end = parser->offset;
  size_t latest_valid_offset = parser->offset;

  node_storeinfo(parser->src, node, begin, end);

  skip_newlines(gab, parser);

  /*
   * The next section will skip newlines to peek and see
   * if we have an infix expression to continue.
   *
   * If we don't find one, we need to *backtrack* the
   * parser to where our initial prefix expression left off.
   *
   * This is because newlines are *expected* in some places as
   * separators. (tuples, lists, and dicts)
   */
  while (prec <= get_parse_rule(curr_tok(parser)).prec) {

    if (node == gab_cinvalid)
      return gab_cinvalid;

    if (!eat_token(gab, parser))
      return gab_cinvalid;

    rule = get_parse_rule(prev_tok(parser));

    if (rule.infix != nullptr)
      node = rule.infix(gab, parser, node);

    latest_valid_offset = parser->offset;

    skip_newlines(gab, parser);
  }

  parser->offset = latest_valid_offset;

  end = parser->offset;

  node_storeinfo(parser->src, node, begin, end);

  return node;
}

static gab_value parse_optional_expression_prec(struct gab_triple gab,
                                                struct parser *parser,
                                                enum prec_k prec) {
  if (!curr_prefix(parser)) {
    gab_value empty = node_empty(gab, parser);
    return empty;
  }

  return parse_expression(gab, parser, prec);
}

gab_value parse_exp_num(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  double num = strtod((char *)prev_src(parser).data, nullptr);
  return node_value(gab, gab_number(num));
}

gab_value parse_exp_msg(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  gab_value id = trimback_prev_id(gab, parser);

  return node_value(gab, gab_strtomsg(id));
}

gab_value parse_exp_sym(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  gab_value id = prev_id(gab, parser);

  return node_value(gab, gab_strtobin(id));
}

gab_value parse_exp_dstr(struct gab_triple gab, struct parser *parser,
                         gab_value lhs) {
  return node_value(gab, trim_prev_id(gab, parser));
}

gab_value parse_exp_sstr(struct gab_triple gab, struct parser *parser,
                         gab_value lhs) {
  a_char *parsed = parse_raw_str(parser, prev_src(parser));

  if (parsed == nullptr)
    return parser_error(gab, parser, GAB_MALFORMED_STRING,
                        "\nSingle quoted strings can contain escape "
                        "sequences.\n"
                        "\n   " GAB_GREEN "'a newline -> " GAB_MAGENTA
                        "\\n" GAB_GREEN ", or a forward slash -> " GAB_MAGENTA
                        "\\\\" GAB_GREEN "'" GAB_RESET "\n   " GAB_GREEN
                        "'a unicode codepoint by number: " GAB_MAGENTA
                        "\\u[" GAB_YELLOW "2502" GAB_MAGENTA "]" GAB_GREEN
                        "'" GAB_RESET),
           gab_cinvalid;

  gab_value str = gab_nstring(gab, parsed->len, parsed->data);

  a_char_destroy(parsed);

  return node_value(gab, str);
}

gab_value parse_exp_rec(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  size_t begin = parser->offset;

  gab_value result = parse_expressions_until(gab, parser, TOKEN_RBRACK);

  if (result == gab_cinvalid)
    return gab_cinvalid;

  gab_value lhs_node = node_value(gab, gab_message(gab, tGAB_RECORD));
  gab_value msg_node = gab_message(gab, mGAB_MAKE);

  gab_value node = node_send(gab, lhs_node, msg_node, result);

  size_t end = parser->offset;

  node_storeinfo(parser->src, result, begin, end);
  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, lhs_node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

gab_value parse_exp_lst(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  size_t begin = parser->offset;

  gab_value result = parse_expressions_until(gab, parser, TOKEN_RBRACE);

  if (result == gab_cinvalid)
    return gab_cinvalid;

  gab_value lhs_node = node_value(gab, gab_message(gab, tGAB_LIST));
  gab_value msg_node = gab_message(gab, mGAB_MAKE);

  gab_value node = node_send(gab, lhs_node, msg_node, result);

  size_t end = parser->offset;

  node_storeinfo(parser->src, result, begin, end);
  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, lhs_node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

gab_value parse_exp_tup(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  return parse_expressions_until(gab, parser, TOKEN_RPAREN);
}

gab_value parse_exp_blk(struct gab_triple gab, struct parser *parser,
                        gab_value lhs) {
  gab_value res = parse_expressions_body(gab, parser, TOKEN_END);

  if (res == gab_cinvalid)
    return gab_cinvalid;

  return res;
}

gab_value parse_exp_send(struct gab_triple gab, struct parser *parser,
                         gab_value lhs) {
  size_t begin = parser->offset;

  gab_value msg = trimfront_prev_id(gab, parser);

  gab_value rhs = parse_optional_expression_prec(gab, parser, kSEND + 1);

  if (rhs == gab_cinvalid)
    return gab_cinvalid;

  gab_value node = node_send(gab, lhs, gab_strtomsg(msg), rhs);

  size_t end = parser->offset;

  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

gab_value parse_exp_send_op(struct gab_triple gab, struct parser *parser,
                            gab_value lhs) {
  size_t begin = parser->offset;

  gab_value msg = prev_id(gab, parser);

  gab_value rhs = parse_optional_expression_prec(gab, parser, kBINARY_SEND + 1);

  if (rhs == gab_cinvalid)
    return gab_cinvalid;

  gab_value node = node_send(gab, lhs, gab_strtomsg(msg), rhs);

  size_t end = parser->offset;

  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

gab_value parse_exp_send_special(struct gab_triple gab, struct parser *parser,
                                 gab_value lhs) {
  size_t begin = parser->offset;

  gab_value msg = prev_id(gab, parser);

  gab_value rhs = parse_expression(gab, parser, kEXP);

  if (rhs == gab_cinvalid)
    return gab_cinvalid;

  gab_value node = node_send(gab, lhs, gab_strtomsg(msg), rhs);

  size_t end = parser->offset;

  node_storeinfo(parser->src, node, begin, end);
  node_storeinfo(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

const struct parse_rule parse_rules[] = {
    {parse_exp_blk, nullptr, kNONE},                  // DO
    {nullptr, nullptr, kNONE},                        // END
    {parse_exp_lst, nullptr, kNONE},                  // LBRACE
    {nullptr, nullptr, kNONE},                        // RBRACE
    {parse_exp_rec, nullptr, kNONE},                  // LBRACK
    {nullptr, nullptr, kNONE},                        // RBRACK
    {parse_exp_tup, nullptr, kNONE},                  // LPAREN
    {nullptr, nullptr, kNONE},                        // RPAREN
    {nullptr, parse_exp_send, kSEND},                 // SEND
    {nullptr, parse_exp_send_op, kBINARY_SEND},       // OPERATOR
    {nullptr, parse_exp_send_special, kSPECIAL_SEND}, // SPECIAL
    {parse_exp_sym, nullptr, kNONE},                  // SYMBOL
    {parse_exp_msg, nullptr, kNONE},                  // MESSAGE
    {parse_exp_sstr, nullptr, kNONE},                 // STRING
    {parse_exp_dstr, nullptr, kNONE},                 // STRING
    {parse_exp_num, nullptr, kNONE},                  // NUMBER
    {nullptr, nullptr, kNONE},                        // NEWLINE
    {nullptr, nullptr, kNONE},                        // EOF
    {nullptr, nullptr, kNONE},                        // ERROR
};

struct parse_rule get_parse_rule(gab_token k) { return parse_rules[k]; }

gab_value parse(struct gab_triple gab, struct parser *parser) {
  size_t begin = parser->offset;

  if (curr_tok(parser) == TOKEN_EOF)
    return eat_token(gab, parser),
           parser_error(gab, parser, GAB_UNEXPECTED_EOF, ""), gab_cinvalid;

  if (curr_tok(parser) == TOKEN_ERROR)
    return eat_token(gab, parser),
           parser_error(gab, parser, GAB_MALFORMED_TOKEN,
                        "This token is malformed or unrecognized."),
           gab_cinvalid;

  gab_value ast = parse_expressions_body(gab, parser, TOKEN_EOF);

  if (ast == gab_cinvalid)
    return gab_cinvalid;

  if (gab.flags & fGAB_AST_DUMP)
    gab_fprintf(stdout, "$\n", gab_pvalintos(gab, ast));

  gab_iref(gab, ast);
  gab_egkeep(gab.eg, ast);

  size_t end = parser->offset;

  node_storeinfo(parser->src, ast, begin, end);

  return ast;
}

union gab_value_pair gab_parse(struct gab_triple gab,
                               struct gab_parse_argt args) {
  gab.flags |= args.flags;

  args.name = args.name ? args.name : "__main__";

  gab_gclock(gab);

  gab_value name = gab_string(gab, args.name);

  struct gab_src *src =
      gab_src(gab, name, (char *)args.source,
              args.source_len ? args.source_len : strlen(args.source) + 1);

  struct parser parser = {.src = src, .err = gab_cundefined};

  gab_value ast = parse(gab, &parser);

  gab_gcunlock(gab);

  assert(ast != gab_cinvalid || parser.err != gab_cundefined);

  if (ast == gab_cinvalid)
    return (union gab_value_pair){.status = gab_cinvalid,
                                  .vresult = parser.err};
  else
    return (union gab_value_pair){.status = gab_cvalid, .vresult = ast};
}

/*

 *************
 * COMPILING *
 *************

 Emiting bytecode for the gab AST.

[0] = LOAD_CONSTANT 0
[0, 1] = LOAD_NCONSTANT 0, 1

*/

void bc_destroy(struct bc *bc) {
  v_uint8_t_destroy(&bc->bc);
  v_uint64_t_destroy(&bc->bc_toks);
}

static int vbc_error(struct gab_triple gab, struct bc *bc, gab_value node,
                     enum gab_status e, const char *fmt, va_list args) {
  size_t tok = d_uint64_t_read(&bc->src->node_begin_toks, node);

  if (tok > 0)
    tok--;

  bc->err = gab_vspanicf(gab, args,
                         (struct gab_err_argt){
                             .src = bc->src,
                             .status = e,
                             .tok = tok,
                             .note_fmt = fmt,
                         });

  va_end(args);

  return 0;
}

static int bc_error(struct gab_triple gab, struct bc *bc, gab_value node,
                    enum gab_status e, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  return vbc_error(gab, bc, node, e, fmt, args);
}

static inline void push_op(struct bc *bc, uint8_t op, gab_value node) {
  bc->pprev_op = bc->prev_op;
  bc->prev_op = op;

  // assert(d_uint64_t_exists(&bc->src->node_begin_toks, node));

  bc->prev_op_at = v_uint8_t_push(&bc->bc, op);
  size_t offset = d_uint64_t_read(&bc->src->node_begin_toks, node);

  v_uint64_t_push(&bc->bc_toks, offset - (offset != 0));
}

static inline void push_byte(struct bc *bc, uint8_t data, gab_value node) {
  // assert(d_uint64_t_exists(&bc->src->node_begin_toks, node));

  v_uint8_t_push(&bc->bc, data);
  size_t offset = d_uint64_t_read(&bc->src->node_begin_toks, node);
  v_uint64_t_push(&bc->bc_toks, offset - (offset != 0));
}

static inline void push_short(struct bc *bc, uint16_t data, gab_value node) {
  push_byte(bc, (data >> 8) & 0xff, node);
  push_byte(bc, data & 0xff, node);
}

static inline uint16_t addk(struct gab_triple gab, struct bc *bc,
                            gab_value value) {
  gab_iref(gab, value);
  gab_egkeep(gab.eg, value);

  assert(bc->ks->len < UINT16_MAX);

  return v_gab_value_push(bc->ks, value);
}

/*
 * SUPER INSTRUCTION OPTIMIZATION
 */

enum super_instruction_transition_k : uint8_t {
  kSI_REPLACE,

  kSI_MAKE_MULTI,
  kSI_MULTI_APPEND,

  kSI_BYTE_ARG_MAKE_MULTI,
  kSI_MULTI_BYTE_ARG_APPEND,

  // Sometimes the second argument-byte contains
  // the multi-byte that needs to be incremented.
  kSI_BYTE_ARG_MAKE_MULTI2,
  kSI_MULTI2_BYTE_ARG_APPEND,

  kSI_SHORT_ARG_MAKE_MULTI,
  kSI_MULTI_SHORT_ARG_APPEND,

  // Sometimes the second argument-byte contains
  // the multi-byte that needs to be incremented.
  kSI_SHORT_ARG_MAKE_MULTI2,
  kSI_MULTI2_SHORT_ARG_APPEND,
};

struct super_instruction {
  uint8_t from, via, to, k;
};

struct super_instruction super_instructions[] = {
    {
        OP_LOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_NLOAD_LOCAL,
        kSI_BYTE_ARG_MAKE_MULTI,
    },
    {
        OP_NLOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_NLOAD_LOCAL,
        kSI_MULTI_BYTE_ARG_APPEND,
    },
    {
        OP_STORE_LOCAL,
        OP_POP,
        OP_POPSTORE_LOCAL,
        kSI_REPLACE,
    },
    {
        OP_POPSTORE_LOCAL,
        OP_STORE_LOCAL,
        OP_NPOPSTORE_STORE_LOCAL,
        kSI_BYTE_ARG_MAKE_MULTI,
    },
    {
        OP_NPOPSTORE_LOCAL,
        OP_STORE_LOCAL,
        OP_NPOPSTORE_STORE_LOCAL,
        kSI_MULTI_BYTE_ARG_APPEND,
    },
    {
        OP_NPOPSTORE_STORE_LOCAL,
        OP_POP,
        OP_NPOPSTORE_LOCAL,
        kSI_REPLACE,
    },
    {
        OP_LOAD_UPVALUE,
        OP_LOAD_UPVALUE,
        OP_NLOAD_UPVALUE,
        kSI_BYTE_ARG_MAKE_MULTI,
    },
    {
        OP_NLOAD_UPVALUE,
        OP_LOAD_UPVALUE,
        OP_NLOAD_UPVALUE,
        kSI_MULTI_BYTE_ARG_APPEND,
    },
    {
        OP_CONSTANT,
        OP_CONSTANT,
        OP_NCONSTANT,
        kSI_SHORT_ARG_MAKE_MULTI,
    },
    {
        OP_NCONSTANT,
        OP_CONSTANT,
        OP_NCONSTANT,
        kSI_MULTI_SHORT_ARG_APPEND,
    },
    {
        OP_TUPLE,
        OP_TUPLE,
        OP_NTUPLE,
        kSI_MAKE_MULTI,
    },
    {
        OP_NTUPLE,
        OP_TUPLE,
        OP_NTUPLE,
        kSI_MULTI_APPEND,
    },
    {
        OP_NTUPLE,
        OP_CONSTANT,
        OP_NTUPLE_CONSTANT,
        kSI_REPLACE,
    },
    {
        OP_NTUPLE_CONSTANT,
        OP_CONSTANT,
        OP_NTUPLE_NCONSTANT,
        kSI_SHORT_ARG_MAKE_MULTI2,
    },
    {
        OP_NTUPLE_NCONSTANT,
        OP_CONSTANT,
        OP_NTUPLE_NCONSTANT,
        kSI_MULTI2_SHORT_ARG_APPEND,
    },
    {
        OP_TUPLE,
        OP_CONSTANT,
        OP_TUPLE_CONSTANT,
        kSI_REPLACE,
    },
    {
        OP_TUPLE_CONSTANT,
        OP_CONSTANT,
        OP_TUPLE_NCONSTANT,
        kSI_SHORT_ARG_MAKE_MULTI,
    },
    {
        OP_TUPLE_NCONSTANT,
        OP_CONSTANT,
        OP_TUPLE_NCONSTANT,
        kSI_MULTI_SHORT_ARG_APPEND,
    },
    {
        OP_TUPLE,
        OP_LOAD_LOCAL,
        OP_TUPLE_LOAD_LOCAL,
        kSI_REPLACE,
    },
    {
        OP_TUPLE_LOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_TUPLE_NLOAD_LOCAL,
        kSI_BYTE_ARG_MAKE_MULTI,
    },
    {
        OP_TUPLE_NLOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_TUPLE_NLOAD_LOCAL,
        kSI_MULTI_BYTE_ARG_APPEND,
    },
    {
        OP_NTUPLE,
        OP_LOAD_LOCAL,
        OP_NTUPLE_LOAD_LOCAL,
        kSI_REPLACE,
    },
    {
        OP_NTUPLE_LOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_NTUPLE_NLOAD_LOCAL,
        kSI_BYTE_ARG_MAKE_MULTI2,
    },
    {
        OP_NTUPLE_NLOAD_LOCAL,
        OP_LOAD_LOCAL,
        OP_NTUPLE_NLOAD_LOCAL,
        kSI_MULTI2_BYTE_ARG_APPEND,
    },
};

const int nsuper_instructions = LEN_CARRAY(super_instructions);

struct inst_arg {
  uint8_t op;

  enum : uint8_t {
    kINST_ARG_NONE,
    kINST_ARG_BYTE,
    kINST_ARG_SHORT,
  } k;

  union {
    uint8_t byte_arg;
    uint16_t short_arg;
  } as;
};

static inline void byte_arg_make_multi(struct bc *bc, struct inst_arg arg,
                                       struct super_instruction si,
                                       gab_value node, int multiarg_offset) {
  // Transition a single-byte-arg instruction to a multi-byte-arg
  // instruction.
  size_t multi_arg = bc->prev_op_at + multiarg_offset;
  uint8_t prev_multi = v_uint8_t_val_at(&bc->bc, multi_arg);

  // Change the previous byte arg to now correspond to the number of bytes
  // to follow (2).
  v_uint8_t_set(&bc->bc, multi_arg, 2);

  // Push the previous byte value, and the new one.
  push_byte(bc, prev_multi, node);
  push_byte(bc, arg.as.byte_arg, node);

  // Update the old instruction to the new, and the previous op.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

static inline void multi_byte_arg_append(struct bc *bc, struct inst_arg arg,
                                         struct super_instruction si,
                                         gab_value node, int multiarg_offset) {
  // Append a new byte arg to a multi-byte instruction.
  size_t multi_arg = bc->prev_op_at + multiarg_offset;
  uint8_t multi = v_uint8_t_val_at(&bc->bc, multi_arg);

  // Increment the multi-arg count.
  v_uint8_t_set(&bc->bc, multi_arg, multi + 1);

  // Push the additional byte argument.
  push_byte(bc, arg.as.byte_arg, node);

  if (si.from == si.to)
    return;

  // Update the old instruction to the new, and the previous op.
  // We can skip this if the super instruction's from and to ops are the
  // same.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

static inline void short_arg_make_multi(struct bc *bc, struct inst_arg arg,
                                        struct super_instruction si,
                                        gab_value node, int multiarg_offset) {
  size_t multi_arg = bc->prev_op_at + multiarg_offset;

  // Reconstruct the short argument from the bytecode.
  uint8_t prev_arg_a = v_uint8_t_val_at(&bc->bc, multi_arg);
  uint8_t prev_arg_b = v_uint8_t_val_at(&bc->bc, multi_arg + 1);

  uint16_t prev_arg = prev_arg_a << 8 | prev_arg_b;

  // Pop off the old short argument, it isn't salvageable.
  v_uint8_t_pop(&bc->bc);
  v_uint8_t_pop(&bc->bc);
  v_uint64_t_pop(&bc->bc_toks);
  v_uint64_t_pop(&bc->bc_toks);

  // Push on a new count argument.
  push_byte(bc, 2, node);

  // Push the original short argument, and the new second one.
  push_short(bc, prev_arg, node);
  push_short(bc, arg.as.short_arg, node);

  // Update the old instruction to the new, and the previous op.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

static inline void multi_short_arg_append(struct bc *bc, struct inst_arg arg,
                                          struct super_instruction si,
                                          gab_value node, int multiarg_offset) {
  // Append a new short arg to a multi-byte instruction.
  size_t multi_arg = bc->prev_op_at + multiarg_offset;
  uint8_t multi = v_uint8_t_val_at(&bc->bc, multi_arg);

  // Increment the multi-arg count.
  v_uint8_t_set(&bc->bc, multi_arg, multi + 1);

  // Push the additional short.
  push_short(bc, arg.as.short_arg, node);

  if (si.from == si.to)
    return;

  // Update the old instruction to the new, and the previous op.
  // We can skip this if the super instruction's from and to ops are the
  // same.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

static inline void push_inst(struct bc *bc, struct inst_arg arg,
                             gab_value node) {
#if cGAB_SUPERINSTRUCTIONS
  for (int i = 0; i < nsuper_instructions; i++) {
    struct super_instruction si = super_instructions[i];

    if (si.from == bc->prev_op && si.via == arg.op) {

      switch (si.k) {
      default:
        assert(false);
        break;
      case kSI_REPLACE: {
        // Push the arg for this new instruction
        switch (arg.k) {
        case kINST_ARG_NONE:
          break;
        case kINST_ARG_BYTE:
          push_byte(bc, arg.as.byte_arg, node);
          break;
        case kINST_ARG_SHORT:
          push_short(bc, arg.as.short_arg, node);
          break;
        }

        // Update the old instruction to the new, and the previous op.
        v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
        bc->prev_op = si.to;
        break;
      }
      case kSI_MAKE_MULTI: {
        // Push a 2, to include previous op and this repetition.
        push_byte(bc, 2, node);

        // Update the instruction to the repeatable target.
        v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
        bc->prev_op = si.to;

        break;
      }
      case kSI_MULTI_APPEND: {
        size_t multi_arg = bc->prev_op_at + 1;
        uint8_t multi = v_uint8_t_val_at(&bc->bc, multi_arg);

        // Increment the multi-arg count.
        v_uint8_t_set(&bc->bc, multi_arg, multi + 1);

        // Leave the instruction, as we are just adding a repetition
        break;
      }
      case kSI_BYTE_ARG_MAKE_MULTI:
        byte_arg_make_multi(bc, arg, si, node, 1);
        break;
      case kSI_BYTE_ARG_MAKE_MULTI2:
        byte_arg_make_multi(bc, arg, si, node, 2);
        break;
      case kSI_MULTI_BYTE_ARG_APPEND:
        multi_byte_arg_append(bc, arg, si, node, 1);
        break;
      case kSI_MULTI2_BYTE_ARG_APPEND:
        multi_byte_arg_append(bc, arg, si, node, 2);
        break;
      case kSI_SHORT_ARG_MAKE_MULTI:
        short_arg_make_multi(bc, arg, si, node, 1);
        break;
      case kSI_SHORT_ARG_MAKE_MULTI2:
        short_arg_make_multi(bc, arg, si, node, 2);
        break;
      case kSI_MULTI_SHORT_ARG_APPEND:
        multi_short_arg_append(bc, arg, si, node, 1);
        break;
      case kSI_MULTI2_SHORT_ARG_APPEND:
        multi_short_arg_append(bc, arg, si, node, 2);
        break;
      };
      return;
    }
  }
#endif

  push_op(bc, arg.op, node);

  switch (arg.k) {
  case kINST_ARG_NONE:
    break;
  case kINST_ARG_BYTE:
    push_byte(bc, arg.as.byte_arg, node);
    break;
  case kINST_ARG_SHORT:
    push_short(bc, arg.as.short_arg, node);
    break;
  }
};

static inline void push_k(struct bc *bc, uint16_t k, gab_value node) {
  push_inst(bc,
            (struct inst_arg){
                .op = OP_CONSTANT,
                .k = kINST_ARG_SHORT,
                .as.short_arg = k,
            },
            node);
}

static inline void push_loadi(struct bc *bc, gab_value i, gab_value node) {
  assert(i == gab_cinvalid || i == gab_true || i == gab_false || i == gab_nil);

  switch (i) {
  case gab_nil:
    push_k(bc, 0, node);
    break;
  case gab_false:
    push_k(bc, 1, node);
    break;
  case gab_true:
    push_k(bc, 2, node);
    break;
  case gab_ok:
    push_k(bc, 3, node);
    break;
  case gab_err:
    push_k(bc, 4, node);
    break;
  case gab_none:
    push_k(bc, 5, node);
    break;
  default:
    assert(false && "Invalid constant");
  }
};

static inline void push_loadni(struct bc *bc, gab_value v, int n,
                               gab_value node) {
  for (int i = 0; i < n; i++)
    push_loadi(bc, v, node);
}

static inline void push_loadk(struct gab_triple gab, struct bc *bc, gab_value k,
                              gab_value node) {
  push_k(bc, addk(gab, bc, k), node);
}

static inline void push_loadl(struct bc *bc, uint8_t local, gab_value node) {
  push_inst(bc,
            (struct inst_arg){
                .k = kINST_ARG_BYTE,
                .op = OP_LOAD_LOCAL,
                .as.byte_arg = local,
            },
            node);
  return;
}

static inline void push_storel(struct bc *bc, uint8_t local, gab_value node) {
  push_inst(bc,
            (struct inst_arg){
                .k = kINST_ARG_BYTE,
                .op = OP_STORE_LOCAL,
                .as.byte_arg = local,
            },
            node);
}

static inline void push_loadu(struct bc *bc, uint8_t upv, gab_value node) {
  push_inst(bc,
            (struct inst_arg){
                .k = kINST_ARG_BYTE,
                .op = OP_LOAD_UPVALUE,
                .as.byte_arg = upv,
            },
            node);
}

static inline void push_send(struct gab_triple gab, struct bc *bc, gab_value m,
                             gab_value node) {
  if (gab_valkind(m) == kGAB_STRING)
    m = gab_strtomsg(m);

  assert(gab_valkind(m) == kGAB_MESSAGE);

  uint16_t ks = addk(gab, bc, m);
  addk(gab, bc, gab_cinvalid);

  for (int i = 0; i < cGAB_SEND_CACHE_LEN * GAB_SEND_CACHE_SIZE; i++)
    addk(gab, bc, gab_cinvalid);

  push_op(bc, OP_SEND, node);
  push_short(bc, ks, node);
}

static inline void push_pop(struct bc *bc, uint8_t n, gab_value node) {
  if (n > 1) {
    push_op(bc, OP_POP_N, node);
    push_byte(bc, n, node);
    return;
  }

  push_inst(bc,
            (struct inst_arg){
                .k = kINST_ARG_NONE,
                .op = OP_POP,
            },
            node);
}

// fix this to work with sends in the middle of tuples, doesn't trim properly
// now
static inline bool push_trim_node(struct gab_triple gab, struct bc *bc,
                                  uint8_t want, gab_value values,
                                  gab_value node) {
  if (bc->prev_op == OP_TRIM) {
    v_uint8_t_set(&bc->bc, bc->prev_op_at + 1, want);
    return true;
  }

  if (values == gab_cinvalid) {
    push_op(bc, OP_TRIM, node);
    push_byte(bc, want, node);
    return true;
  }

  size_t len = node_len(gab, values);

  if (node_ismulti(gab, values)) {
    if (want == 0) {
      push_op(bc, OP_TRIM, node);
      push_byte(bc, 0, node);
      return true;
    }

    push_op(bc, OP_TRIM, node);
    push_byte(bc, want, node);
    return true;
  }

  if (len > want) {
    push_pop(bc, len - want, values);
    return true;
  }

  if (len < want) {
    push_loadni(bc, gab_nil, want - len, values);
    return true;
  }

  // Nothing needs to be done
  return true;
}

static inline void push_listpack(struct gab_triple gab, struct bc *bc,
                                 uint8_t below, uint8_t above, gab_value node) {
  push_op(bc, OP_PACK_LIST, node);
  push_byte(bc, below, node);
  push_byte(bc, above, node);
}

static inline void push_recordpack(struct gab_triple gab, struct bc *bc,
                                   uint8_t below, uint8_t above,
                                   gab_value node) {
  push_op(bc, OP_PACK_RECORD, node);
  push_byte(bc, below, node);
  push_byte(bc, above, node);
}

static inline void push_ret(struct gab_triple gab, struct bc *bc, gab_value tup,
                            gab_value node) {
  assert(node_len(gab, tup) < 16);

  bool is_multi = node_ismulti(gab, tup);
  size_t len = node_len(gab, tup);

  if (len && is_multi)
    len--;

#if cGAB_TAILCALL
  if (len == 0) {
    switch (bc->prev_op) {
    case OP_SEND: {
      uint8_t first_short_byte = v_uint8_t_val_at(&bc->bc, bc->bc.len - 2);
      assert(!(first_short_byte & fHAVE_TAIL));
      v_uint8_t_set(&bc->bc, bc->bc.len - 2, first_short_byte | fHAVE_TAIL);
      push_op(bc, OP_RETURN, node);

      return;
    }
    case OP_TRIM: {
      if (bc->pprev_op != OP_SEND)
        break;

      uint8_t first_short_byte = v_uint8_t_val_at(&bc->bc, bc->bc.len - 4);
      assert(!(first_short_byte & fHAVE_TAIL));
      v_uint8_t_set(&bc->bc, bc->bc.len - 4, first_short_byte | fHAVE_TAIL);
      bc->prev_op = bc->pprev_op;
      bc->bc.len -= 2;
      bc->bc_toks.len -= 2;
      push_op(bc, OP_RETURN, node);

      return;
    }
    }
  }
#endif

  push_op(bc, OP_RETURN, node);
  return;
}

void patch_init(struct bc *bc, uint8_t nlocals) {
  if (v_uint8_t_val_at(&bc->bc, 0) == OP_TRIM)
    v_uint8_t_set(&bc->bc, 1, nlocals);
  else if (v_uint8_t_val_at(&bc->bc, 3) == OP_TRIM)
    v_uint8_t_set(&bc->bc, 4, nlocals);
  else
    assert(false && "UNREACHABLE");
}

size_t locals_in_env(gab_value env) {
  size_t n = 0, len = gab_reclen(env);

  for (size_t i = 0; i < len; i++) {
    gab_value num_or_nil = gab_uvrecat(env, i);

    if (num_or_nil == gab_nil)
      n++;
  }

  return n;
}

size_t upvalues_in_env(gab_value env) {
  size_t n = 0, len = gab_reclen(env);

  for (size_t i = 0; i < len; i++) {
    gab_value num_or_nil = gab_uvrecat(env, i);

    if (gab_valkind(num_or_nil) == kGAB_NUMBER)
      n++;
  }

  return n;
}

gab_value peek_env(gab_value env, int depth) {
  size_t nenv = gab_reclen(env);

  if (depth + 1 > nenv)
    return gab_cundefined;

  return gab_uvrecat(env, nenv - depth - 1);
}

gab_value put_env(struct gab_triple gab, gab_value env, int depth,
                  gab_value new_ctx) {
  size_t nenv = gab_reclen(env);

  assert(depth + 1 <= nenv);
  return gab_urecput(gab, env, nenv - depth - 1, new_ctx);
}

struct lookup_res {
  gab_value env;

  enum {
    kLOOKUP_NONE,
    kLOOKUP_UPV,
    kLOOKUP_LOC,
  } k;

  int64_t idx;
};

/*
 * Modify the given environment to capture the message 'id'.
 * If already captured, the environment is unchanged.
 */
static struct lookup_res add_upvalue(struct gab_triple gab, gab_value env,
                                     gab_value id, int depth) {
  gab_value ctx = peek_env(env, depth);

  if (ctx == gab_cundefined)
    return (struct lookup_res){env, kLOOKUP_NONE};

  // Don't pull redundant upvalues
  gab_value current_upv_idx = gab_recat(ctx, id);

  if (current_upv_idx != gab_cundefined)
    return (struct lookup_res){env, kLOOKUP_UPV, gab_valtoi(current_upv_idx)};

  uint16_t count = upvalues_in_env(ctx);

  // Throw some sort of error here
  if (count == GAB_UPVALUE_MAX)
    return (struct lookup_res){env, kLOOKUP_NONE};

  /*compiler_error(*/
  /*    bc, GAB_TOO_MANY_UPVALUES,*/
  /*    "For arbitrary reasons, blocks cannot capture more than 255 "*/
  /*    "variables.");*/
  assert(!gab_rechas(ctx, id));
  ctx = gab_recput(gab, ctx, id, gab_number(count));
  env = put_env(gab, env, depth, ctx);

  return (struct lookup_res){env, kLOOKUP_UPV, count};
}

static int64_t lookup_upv(gab_value ctx, gab_value id) {
  assert(gab_valkind(gab_recat(ctx, id)) == kGAB_NUMBER);
  return gab_valtoi(gab_recat(ctx, id));
}

static int lookup_local(gab_value ctx, gab_value id) {
  size_t idx = 0, len = gab_reclen(ctx);

  for (size_t i = 0; i < len; i++) {
    gab_value k = gab_ukrecat(ctx, i);
    gab_value v = gab_uvrecat(ctx, i);

    // If v isn't nil, then this is an upvalue. Skip it.
    if (v != gab_nil)
      continue;

    if (k == id)
      return idx;

    idx++;
  }

  return -1;
}

/*
 * Find for an id in the env at depth.
 */
static int resolve_local(struct gab_triple gab, gab_value env, gab_value id,
                         uint8_t depth) {
  gab_value ctx = peek_env(env, depth);

  if (ctx == gab_cundefined)
    return -1;

  return lookup_local(ctx, id);
}

static struct lookup_res resolve_upvalue(struct gab_triple gab, gab_value env,
                                         gab_value name, uint8_t depth) {
  size_t nenvs = gab_reclen(env);

  if (depth >= nenvs)
    return (struct lookup_res){env, kLOOKUP_NONE};

  int local = resolve_local(gab, env, name, depth + 1);

  if (local >= 0)
    return add_upvalue(gab, env, name, depth);

  struct lookup_res res = resolve_upvalue(gab, env, name, depth + 1);

  if (res.k) // This means we found either a local, or an upvalue
    return add_upvalue(gab, res.env, name, depth);

  return (struct lookup_res){env, kLOOKUP_NONE};
}

/* Returns COMP_ERR if an error is encountered,
 * COMP_ID_NOT_FOUND if no matching local or upvalue is found,
 * COMP_RESOLVED_TO_LOCAL if the id was a local, and
 * COMP_RESOLVED_TO_UPVALUE if the id was an upvalue.
 */
static struct lookup_res resolve_id(struct gab_triple gab, struct bc *bc,
                                    gab_value env, gab_value id) {
  int idx = resolve_local(gab, env, id, 0);

  if (idx == -1)
    return resolve_upvalue(gab, env, id, 0);
  else
    return (struct lookup_res){env, kLOOKUP_LOC, idx};
}

gab_value compile_symbol(struct gab_triple gab, struct bc *bc, gab_value tuple,
                         gab_value id, gab_value env) {
  struct lookup_res res = resolve_id(gab, bc, env, id);

  switch (res.k) {
  case kLOOKUP_LOC:
    push_loadl(bc, res.idx, tuple);
    return res.env;
  case kLOOKUP_UPV:
    push_loadu(bc, res.idx, tuple);
    return res.env;
  default:
    bc_error(gab, bc, tuple, GAB_UNBOUND_SYMBOL, "$ is unbound",
             gab_bintostr(id));
    return gab_cinvalid;
  }
};

gab_value compile_tuple(struct gab_triple gab, struct bc *bc, gab_value node,
                        gab_value env);

gab_value compile_record(struct gab_triple gab, struct bc *bc, gab_value tuple,
                         gab_value node, gab_value env);

gab_value compile_value(struct gab_triple gab, struct bc *bc, gab_value tuple,
                        size_t n, gab_value env) {
  gab_value node = gab_uvrecat(tuple, n);

  switch (gab_valkind(node)) {
  case kGAB_NUMBER:
  case kGAB_STRING:
  case kGAB_MESSAGE:
    push_loadk(gab, bc, node, tuple);
    return env;

  case kGAB_BINARY:
    return compile_symbol(gab, bc, tuple, node, env);

  case kGAB_RECORD:
    return compile_record(gab, bc, tuple, node, env);

  default:
    assert(false && "UN-UNQUOATABLE VALUE");
    return gab_cinvalid;
  }
}

gab_value unpack_binding(struct gab_triple gab, struct bc *bc,
                         gab_value bindings, size_t i, gab_value ctx,
                         v_gab_value *targets, int *listpack_at_n,
                         int *recpack_at_n) {
  gab_value binding = gab_uvrecat(bindings, i);

  switch (gab_valkind(binding)) {

  case kGAB_BINARY:
    if (gab_valkind(gab_recat(ctx, binding)) == kGAB_NUMBER) {
      return bc_error(gab, bc, bindings, GAB_MALFORMED_ASSIGNMENT,
                      FMT_MALFORMED_ASSIGNMENT FMT_MALFORMED_ASSIGNMENT_NOTE
                      "Cannot assign to a captured variable: $.",
                      gab_bintostr(binding)),
             gab_cinvalid;
    }

    ctx = gab_recput(gab, ctx, binding, gab_nil);
    v_gab_value_push(targets, binding);
    return ctx;

  case kGAB_RECORD: {
    if (gab_valkind(gab_recshp(binding)) == kGAB_SHAPE) {
      // Assume this is a send
      gab_value lhs = gab_mrecat(gab, binding, mGAB_AST_NODE_SEND_LHS);
      gab_value rhs = gab_mrecat(gab, binding, mGAB_AST_NODE_SEND_RHS);
      gab_value m = gab_mrecat(gab, binding, mGAB_AST_NODE_SEND_MSG);

      gab_value rec = gab_uvrecat(lhs, 0);

      /*
       * Compiling a PACK member.
       */
      if (m == gab_message(gab, mGAB_SPLATLIST)) {
        if (gab_valkind(rec) != kGAB_BINARY)
          goto err;

        if (!node_isempty(rhs))
          goto err;

        if (*listpack_at_n >= 0 || *recpack_at_n >= 0)
          return bc_error(gab, bc, binding, GAB_MALFORMED_ASSIGNMENT,
                          FMT_MALFORMED_ASSIGNMENT FMT_MALFORMED_ASSIGNMENT_NOTE
                          "There may only be one assignment target with '*' or "
                          "'**'"),
                 gab_cinvalid;

        assert(gab_valkind(gab_recat(ctx, rec)) != kGAB_NUMBER);
        ctx = gab_recput(gab, ctx, rec, gab_nil);
        v_gab_value_push(targets, rec);

        *listpack_at_n = i;

        return ctx;
      }

      /*
       * Compiling a DICT member
       */
      if (m == gab_message(gab, mGAB_SPLATDICT)) {
        if (gab_valkind(rec) != kGAB_BINARY)
          goto err;

        if (!node_isempty(rhs))
          goto err;

        if (*listpack_at_n >= 0 || *recpack_at_n >= 0)
          return bc_error(gab, bc, binding, GAB_MALFORMED_ASSIGNMENT,
                          FMT_MALFORMED_ASSIGNMENT FMT_MALFORMED_ASSIGNMENT_NOTE
                          "There may only be one assignment target with '*' or "
                          "'**'"),
                 gab_cinvalid;

        assert(gab_valkind(gab_recat(ctx, rec)) != kGAB_NUMBER);
        ctx = gab_recput(gab, ctx, rec, gab_nil);
        v_gab_value_push(targets, rec);

        *recpack_at_n = i;

        return ctx;
      }

    err:
      return bc_error(gab, bc, binding, GAB_MALFORMED_ASSIGNMENT,
                      FMT_MALFORMED_ASSIGNMENT),
             gab_cinvalid;
    }
  }

  default:
    return bc_error(gab, bc, binding, GAB_MALFORMED_ASSIGNMENT,
                    FMT_MALFORMED_ASSIGNMENT),
           gab_cinvalid;
  }
}

gab_value unpack_bindings_into_env(struct gab_triple gab, struct bc *bc,
                                   gab_value bindings, gab_value env,
                                   gab_value values) {
  size_t local_ctx = gab_reclen(env) - 1;
  gab_value ctx = gab_uvrecat(env, local_ctx);

  int listpack_at_n = -1, recpack_at_n = -1;

  size_t len = gab_reclen(bindings);

  if (!len)
    return env;

  v_gab_value targets = {};

  for (size_t i = 0; i < len; i++) {
    ctx = unpack_binding(gab, bc, bindings, i, ctx, &targets, &listpack_at_n,
                         &recpack_at_n);

    if (ctx == gab_cinvalid)
      return v_gab_value_destroy(&targets), ctx;
  }

  size_t actual_targets = targets.len;

  if (listpack_at_n >= 0) {
    push_listpack(gab, bc, listpack_at_n, actual_targets - listpack_at_n - 1,
                  bindings);
  } else if (recpack_at_n >= 0) {
    push_recordpack(gab, bc, recpack_at_n, actual_targets - recpack_at_n - 1,
                    bindings);
  } else if (!push_trim_node(gab, bc, actual_targets, values, bindings)) {
    return v_gab_value_destroy(&targets), gab_cinvalid;
  }

  env = gab_urecput(gab, env, local_ctx, ctx);

  /*
   * Sometimes, these bindings don't have a corresponding value AST
   * to take care of right now (Such as arguments to a function).
   * In thase case, binding stops here.
   */
  if (values == gab_cinvalid)
    return v_gab_value_destroy(&targets), env;

  for (size_t i = 0; i < actual_targets; i++) {
    gab_value target = targets.data[actual_targets - i - 1];

    switch (gab_valkind(target)) {
    case kGAB_BINARY: {
      struct lookup_res res = resolve_id(gab, bc, env, target);

      switch (res.k) {
      case kLOOKUP_LOC:
        push_storel(bc, res.idx, bindings);
        if (i + 1 != actual_targets)
          push_pop(bc, 1, bindings);
        break;
      case kLOOKUP_UPV:
        assert(false && "INVALID UPV TARGET");
      case kLOOKUP_NONE:
        assert(false && "INVALID NONE TARGET");
        break;
      }

      break;
    }
    default:
      return v_gab_value_destroy(&targets),
             bc_error(gab, bc, bindings, GAB_MALFORMED_ASSIGNMENT,
                      FMT_MALFORMED_ASSIGNMENT),
             gab_cinvalid;
    }
  }

  return v_gab_value_destroy(&targets), env;
}

gab_value compile_block(struct gab_triple gab, struct bc *bc, gab_value node,
                        gab_value env) {
  gab_value LHS = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_LHS);
  gab_value RHS = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_RHS);

  gab_value lst = gab_listof(gab, gab_binary(gab, "self"));

  env = gab_lstpush(gab, env, gab_erecord(gab));

  gab_value bindings = gab_lstcat(gab, lst, LHS);
  node_stealinfo(bc->src, LHS, bindings);

  union gab_value_pair pair = gab_compile(gab, (struct gab_compile_argt){
                                                   .ast = RHS,
                                                   .env = env,
                                                   .bindings = bindings,
                                                   .mod = bc->src->name,
                                               });

  if (pair.status == gab_cinvalid)
    return bc->err = pair.vresult, gab_cinvalid;

  gab_value prt = pair.vresult;
  assert(gab_valkind(prt) == kGAB_PROTOTYPE);

  env = gab_recpop(gab, gab_prtenv(prt), nullptr, nullptr);

  push_op(bc, OP_BLOCK, RHS);
  push_short(bc, addk(gab, bc, prt), RHS);

  return env;
}

gab_value compile_assign(struct gab_triple gab, struct bc *bc, gab_value node,
                         gab_value env) {
  gab_value lhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_LHS);
  gab_value rhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_RHS);

  env = compile_tuple(gab, bc, rhs_node, env);

  if (env == gab_cinvalid)
    return gab_cinvalid;

  // TODO: This emits a *trim node* in certain situations.
  // The trim node sets the tuple to 0 currently, which creates inconsistent
  // behavior with assignment expressions. EG: a = 1 => 1 EG: a = 1 + 1 => ()
  // The trim node *should* instead leave the tuple on the stack with want
  // length. This causes another bug somewhere else
  env = unpack_bindings_into_env(gab, bc, lhs_node, env, rhs_node);

  if (env == gab_cinvalid)
    return gab_cinvalid;

  return env;
}

gab_value compile_specialform(struct gab_triple gab, struct bc *bc,
                              gab_value tuple, gab_value node, gab_value env) {
  gab_value msg = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG);

  if (msg == gab_message(gab, mGAB_ASSIGN))
    return compile_assign(gab, bc, node, env);

  if (msg == gab_message(gab, mGAB_BLOCK))
    return compile_block(gab, bc, node, env);

  assert(false && "UNHANDLED SPECIAL FORM");
  return gab_cinvalid;
};

gab_value compile_record(struct gab_triple gab, struct bc *bc, gab_value tuple,
                         gab_value node, gab_value env) {
  // Unquoting a record can mean one of two things:
  //  - This is a block, and each of the membres need to be compiled and
  //  trimmed, except for the last.
  //  - This is a send, and the send needs to be emitted.
  switch (gab_valkind(gab_recshp(node))) {
  case kGAB_SHAPE: {
    // We have a send node!
    gab_value lhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_LHS);
    gab_value rhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_RHS);
    gab_value msg = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG);

    if (msg_is_specialform(gab, msg))
      return compile_specialform(gab, bc, tuple, node, env);

    push_inst(bc, (struct inst_arg){OP_TUPLE}, node);

    env = compile_tuple(gab, bc, lhs_node, env);

    if (env == gab_cinvalid)
      return gab_cinvalid;

    // If the lhs was multi,
    env = compile_tuple(gab, bc, rhs_node, env);

    if (env == gab_cinvalid)
      return gab_cinvalid;

    push_send(gab, bc, msg, node);

    break;
  }
  case kGAB_SHAPELIST: {
    size_t len = gab_reclen(node);
    size_t last_node = len - 1;

    for (size_t i = 0; i < len; i++) {
      gab_value child_node = gab_uvrecat(node, i);

      env = compile_tuple(gab, bc, child_node, env);

      if (env == gab_cinvalid)
        return gab_cinvalid;

      if (i != last_node)
        if (!push_trim_node(gab, bc, 0, child_node, child_node))
          return gab_cinvalid;
    }
    break;
  }
  default:
    assert(false && "INVALID SHAPE KIND");
  }

  return env;
}

// Explicit tuple passed in here? Because of sends, where the lhs and rhs are
// each compiled as tuples, but really they are part of one tuple (which may or
// may not need to be cons'd)
gab_value compile_tuple(struct gab_triple gab, struct bc *bc, gab_value node,
                        gab_value env) {
  size_t len = gab_reclen(node);

  for (size_t i = 0; i < len; i++) {
    env = compile_value(gab, bc, node, i, env);

    if (env == gab_cinvalid)
      return gab_cinvalid;
  }

  return env;
}

void build_upvdata(gab_value env, uint8_t len, char *data) {
  if (len == 0)
    return;

  /*
   * Iterate through the env, and build out the data argument expected
   * by prototypes.
   *
   * I need to iterate through each environment in the stack
   *
   * If we're local, flag the captures as local.
   * Otherwise, do nothing.
   *
   * Upvalues are all the non-nil values in the context.
   *
   * As insertion-order is preserved, we can iterate the
   * ctx 0..len and be fine.
   *
   */
  size_t nenvs = gab_reclen(env);

  assert(nenvs >= 2);

  gab_value ctx = gab_uvrecat(env, nenvs - 1);
  gab_value parent = gab_uvrecat(env, nenvs - 2);

  bool has_grandparent = nenvs >= 3;

  size_t nbindings = gab_reclen(ctx);

  size_t nlocals = locals_in_env(parent);
  size_t nupvalues = upvalues_in_env(parent);

  for (size_t i = 0; i < nbindings; i++) {
    gab_value k = gab_ukrecat(ctx, i);
    gab_value v = gab_uvrecat(ctx, i);

    if (v == gab_nil)
      continue;

    bool is_local = has_grandparent ? (gab_recat(parent, k) == gab_nil) : true;

    size_t idx = is_local ? lookup_local(parent, k) : lookup_upv(parent, k);

    assert((is_local && idx < nlocals) || (!is_local && idx < nupvalues));

    uint64_t nth_upvalue = gab_valtou(v);
    assert(nth_upvalue < len);

    data[nth_upvalue] = (idx << 1) | is_local;
  }
}

/*
 *    *******
 *    * ENV *
 *    *******
 *
 *    The ENV argument is a stack of records.
 *
 *    [ { self {}, a {} }, { self {}, b {} } ]
 *
 *    Each variable has a record of attributes:
 *      * captured? -> number - is the variable captured by a child scope? If
 * so, whats its upv index?
 *
 *    Each scope needs to keep track of:
 *     - local variables introduced in this scope
 *     - local variables captured by child scopes
 *     - update parent scopes whose variables it captures
 */
union gab_value_pair gab_compile(struct gab_triple gab,
                                 struct gab_compile_argt args) {
  assert(gab_valkind(args.ast) == kGAB_RECORD);
  assert(gab_valkind(args.env) == kGAB_RECORD);
  gab.flags |= args.flags;

  struct gab_src *src = d_gab_src_read(&gab.eg->sources, args.mod);

  if (src == nullptr)
    return (union gab_value_pair){{gab_cinvalid, gab_cinvalid}};

  struct bc bc = {.ks = &src->constants, .src = src, .err = gab_cinvalid};

  args.env =
      unpack_bindings_into_env(gab, &bc, args.bindings, args.env, gab_cinvalid);

  if (args.env == gab_cinvalid)
    return assert(bc.err != gab_cinvalid),
           (union gab_value_pair){{gab_cinvalid, bc.err}};

  size_t nenvs = gab_reclen(args.env);
  assert(nenvs > 0);

  size_t nargs = gab_reclen(gab_uvrecat(args.env, nenvs - 1));
  assert(nargs < GAB_ARG_MAX);

  if (!push_trim_node(gab, &bc, nargs, gab_cinvalid, args.bindings))
    return assert(bc.err != gab_cinvalid),
           (union gab_value_pair){{gab_cinvalid, bc.err}};

  assert(bc.bc.len == bc.bc_toks.len);

  /*
   * The first tuple in a block is its *arguments*. We don't want to return
   * this tuple from the block when the block returns.
   * We push another, empty tuple here. This will be returned by the block.
   **/
  push_inst(&bc, (struct inst_arg){OP_TUPLE}, args.ast);

  args.env = compile_tuple(gab, &bc, args.ast, args.env);

  assert(bc.bc.len == bc.bc_toks.len);

  if (args.env == gab_cinvalid)
    return assert(bc.err != gab_cinvalid), bc_destroy(&bc),
           (union gab_value_pair){{gab_cinvalid, bc.err}};

  assert(gab_reclen(args.env) == nenvs);

  gab_value local_env = gab_uvrecat(args.env, nenvs - 1);
  assert(bc.bc.len == bc.bc_toks.len);

  push_ret(gab, &bc, args.ast, args.ast);

  size_t nlocals = locals_in_env(local_env);
  assert(nlocals < GAB_LOCAL_MAX);

  size_t nupvalues = upvalues_in_env(local_env);
  assert(nupvalues < GAB_UPVALUE_MAX);

  assert(bc.bc.len == bc.bc_toks.len);

  patch_init(&bc, nlocals);

  size_t len = bc.bc.len;
  size_t end = gab_srcappend(src, len, bc.bc.data, bc.bc_toks.data);

  bc_destroy(&bc);

  size_t begin = end - len;

  /*
   * Some blocks may have 0 upvalues.
   * To prevent undefined behavior, just allocate an extra
   * byte that is always unused.
   */
  char data[nupvalues + 1];
  build_upvdata(args.env, nupvalues, data);

  size_t bco = d_uint64_t_read(&bc.src->node_begin_toks, args.ast);
  v_uint64_t_set(&bc.src->bytecode_toks, begin, bco);

  gab_value proto = gab_prototype(gab, src, begin, len,
                                  (struct gab_prototype_argt){
                                      .nupvalues = nupvalues,
                                      .nlocals = nlocals,
                                      .narguments = nargs,
                                      .nslots = (nlocals + 3),
                                      .env = args.env,
                                      .data = data,
                                  });

  if (gab.flags & fGAB_BUILD_DUMP)
    gab_fmodinspect(stdout, proto);

  assert(bc.err == gab_cinvalid);

  return (union gab_value_pair){
      .status = gab_cvalid,
      .vresult = proto,
  };
}

union gab_value_pair gab_build(struct gab_triple gab,
                               struct gab_parse_argt args) {
  gab.flags |= args.flags;

  args.name = args.name ? args.name : "__main__";

  gab_gclock(gab);

  gab_value mod = gab_string(gab, args.name);

  union gab_value_pair ast = gab_parse(gab, args);

  assert(ast.vresult != gab_cundefined);
  if (ast.status != gab_cvalid)
    return gab_gcunlock(gab), ast;

  struct gab_src *src = d_gab_src_read(&gab.eg->sources, mod);

  if (src == nullptr)
    return gab_gcunlock(gab), (union gab_value_pair){.status = gab_cinvalid,
                                                     .vresult = gab_cundefined};

  // Default to empty list here
  gab_value bindings = gab_listof(gab);

  if (args.len) {
    gab_value vargs[args.len];

    for (int i = 0; i < args.len; i++)
      vargs[i] = gab_binary(gab, args.argv[i]);

    bindings = gab_list(gab, args.len, vargs);
  }

  node_storeinfo(src, bindings, 0, 0);

  gab_value env =
      gab_listof(gab, gab_recordof(gab, gab_binary(gab, "self"), gab_nil));

  union gab_value_pair res = gab_compile(gab, (struct gab_compile_argt){
                                                  .ast = ast.vresult,
                                                  .env = env,
                                                  .mod = mod,
                                                  .bindings = bindings,
                                              });
  assert(res.vresult != gab_cundefined);

  if (res.status == gab_cinvalid)
    return gab_gcunlock(gab), res;

  gab_srccomplete(gab, src);

  gab_value main = gab_block(gab, res.vresult);
  assert(main != gab_cundefined);

  gab_iref(gab, main);
  gab_iref(gab, res.vresult);
  gab_egkeep(gab.eg, main);
  gab_egkeep(gab.eg, res.vresult);

  return gab_gcunlock(gab),
         (union gab_value_pair){.status = gab_cvalid, .vresult = main};
}
