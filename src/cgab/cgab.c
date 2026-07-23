/**
 *  MIT License
 *
 *  Copyright (c) 2023-2026 Teddy Randby
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

#include "cgab.h"
#include "engine.h"
#include "hash.h"

#include <__stddef_unreachable.h>
#include <ctype.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define popcountl(n) __builtin_popcountl(n)
#define ctzl(n) __builtin_ctzl(n)

/* Helpers used by all the sprintf methods */
GAB_INTERNAL int64_t __gab_vsnprintf_through(char **dst, size_t *n,
                                             const char *fmt, va_list va) {
  int res = vsnprintf(*dst, *n, fmt, va);

  if (res > *n) {
    *dst += *n;
    *n = 0;
    return -1;
  }

  *dst += res;
  *n -= res;

  return res;
}

GAB_INTERNAL int64_t __gab_snprintf_through(char **dst, size_t *n,
                                            const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  int res = __gab_vsnprintf_through(dst, n, fmt, va);
  va_end(va);

  return res;
}

GAB_INTERNAL int64_t __gab_gvsnprintf_through(char **dst, size_t *n,
                                              const char *fmt, va_list va) {
  int res = gab_vsprintf(*dst, *n, fmt, va);

  if (res < 0) {
    *dst += *n;
    *n = 0;
    return -1;
  }

  *dst += res;
  *n -= res;

  return res;
}

GAB_INTERNAL int64_t __gab_gsnprintf_through(char **dst, size_t *n,
                                             const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  int res = __gab_gvsnprintf_through(dst, n, fmt, va);
  va_end(va);

  return res;
}

GAB_INTERNAL union gab_value_pair __gab_vmexec(struct gab_triple gab,
                                               gab_value fiber);

/* ----------------------------------------
 *
 *    GAB LEXER
 *
 *  This section contains the code for producing a stream of tokens from a
 *   string.
 * ----------------------------------------
 */

GAB_INTERNAL bool __gab_lexcanstartop(uint8_t c) {
  switch (c) {
  case '!':
  case '$':
  case '%':
  case '^':
  case '*':
  case '/':
  case '+':
  case '-':
  case '&':
  case '|':
  case '=':
  case '<':
  case '>':
  case '?':
  case '~':
  case '@':
    return true;
  default:
    return false;
  }
}

GAB_INTERNAL bool __gab_lexcancontinueop(uint8_t c) {
  switch (c) {
  default:
    return __gab_lexcanstartop(c);
  }
}

GAB_INTERNAL bool __gab_lexcanstartsym(uint8_t c) {
  return isalpha(c) || c == '_';
}

GAB_INTERNAL bool __gab_lexcancontinuesym(uint8_t c) {
  return __gab_lexcanstartsym(c) || isdigit(c) || c == '\\';
}

GAB_INTERNAL bool __gab_lexcancontinuehex(uint8_t c) {
  if (isdigit(c))
    return true;

  if (c >= 'a' && c <= 'f')
    return true;

  if (c >= 'A' && c <= 'F')
    return true;

  return false;
}

GAB_INTERNAL bool __gab_lexiscomment(uint8_t c) { return c == '#'; }

typedef struct gab_lx {
  char *cursor;
  char *row_start;
  uint64_t row;
  uint64_t col;

  uint8_t status;

  struct gab_src *source;

  s_char current_row_comment;
  s_char current_row_src;
  s_char current_token_src;
} gab_lx;

/* Advance the lexer's cursor one byte */
GAB_INTERNAL void __gab_lexadvance(gab_lx *self) {
  self->cursor++;
  self->col++;
  self->current_token_src.len++;
  self->current_row_src.len++;
}

/* Begin lexing a row of source */
GAB_INTERNAL void __gab_lexrowbeg(gab_lx *self) {
  self->current_row_comment = (s_char){0};
  self->current_row_src.data = self->cursor;
  self->current_row_src.len = 0;
  self->col = 0;
  self->row++;
}

/* Begin lexing a token */
GAB_INTERNAL void __gab_lextokbeg(gab_lx *self) {
  self->current_token_src.data = self->cursor;
  self->current_token_src.len = 0;
}

/* Complete lexing a row of source */
GAB_INTERNAL void __gab_lexrowend(gab_lx *self) {
  if (self->current_row_src.len &&
      self->current_row_src.data[self->current_row_src.len - 1] == '\n')
    self->current_row_src.len--;

  v_s_char_push(&self->source->lines, self->current_row_src);

  __gab_lexrowbeg(self);
}

GAB_INTERNAL void __gab_lexcreate(gab_lx *self, struct gab_src *src) {
  memset(self, 0, sizeof(gab_lx));

  self->source = src;
  self->cursor = src->source->data;
  self->row_start = src->source->data;

  v_gab_value_push(&src->constants, gab_nil);
  v_gab_value_push(&src->constants, gab_false);
  v_gab_value_push(&src->constants, gab_true);
  v_gab_value_push(&src->constants, gab_ok);
  v_gab_value_push(&src->constants, gab_err);
  v_gab_value_push(&src->constants, gab_none);

  d_uint64_t_create(&src->node_begin_toks, 64);
  d_uint64_t_create(&src->node_end_toks, 64);

  __gab_lexrowbeg(self);
}

GAB_INTERNAL int __gab_lexpeek(gab_lx *self) { return *self->cursor; }

GAB_INTERNAL int __gab_lexpeeknext(gab_lx *self) { return *(self->cursor + 1); }

GAB_INTERNAL gab_token __gab_lexerror(gab_lx *self, enum gab_status s) {
  self->status = s;
  return TOKEN_ERROR;
}

typedef struct keyword {
  const char *literal;
  gab_token token;
} keyword;

const keyword keywords[] = {
    {

        "do",
        TOKEN_DO,
    },
    {
        "end",
        TOKEN_END,
    },
};

GAB_INTERNAL gab_token __gab_lexstr(gab_lx *self) {
  uint8_t start = __gab_lexpeek(self);
  uint8_t stop = start == '"' ? '"' : '\'';

  do {
    __gab_lexadvance(self);

    if (__gab_lexpeek(self) == '\0')
      return __gab_lexerror(self, GAB_MALFORMED_STRING);

    if (start != '"')
      if (__gab_lexpeek(self) == '\n')
        return __gab_lexerror(self, GAB_MALFORMED_STRING);

  } while (__gab_lexpeek(self) != stop);

  __gab_lexadvance(self);
  return start == '"' ? TOKEN_DOUBLESTRING : TOKEN_SINGLESTRING;
}

GAB_INTERNAL gab_token __gab_lexop(gab_lx *self) {
  while (__gab_lexcancontinueop(__gab_lexpeek(self)))
    __gab_lexadvance(self);

  if (__gab_lexpeek(self) == ':')
    return __gab_lexadvance(self), TOKEN_MESSAGE;

  return TOKEN_OPERATOR;
}

GAB_INTERNAL gab_token __gab_lexsym(gab_lx *self) {
  while (__gab_lexcancontinuesym(__gab_lexpeek(self)))
    __gab_lexadvance(self);

  if (__gab_lexpeek(self) == ':')
    return __gab_lexadvance(self), TOKEN_MESSAGE;

  for (int i = 0; i < sizeof(keywords) / sizeof(keyword); i++) {
    keyword k = keywords[i];
    s_char lit = s_char_create(k.literal, strlen(k.literal));
    if (s_char_match(self->current_token_src, lit)) {
      return k.token;
    }
  }

  return TOKEN_SYMBOL;
}

GAB_INTERNAL gab_token __gab_lexint(gab_lx *self) {
  while (isdigit(__gab_lexpeek(self)))
    __gab_lexadvance(self);

  return TOKEN_NUMBER;
}

GAB_INTERNAL bool __gab_lexisexp(char c) {
  return isdigit(c) || c == '+' || c == '-';
}

GAB_INTERNAL gab_token __gab_lexdec(gab_lx *self) {
  if (__gab_lexint(self) == TOKEN_ERROR)
    return TOKEN_ERROR;

  // Check for a decimal exponent
  if (__gab_lexpeek(self) == 'e' && __gab_lexisexp(__gab_lexpeeknext(self)))
    return __gab_lexadvance(self), __gab_lexadvance(self), __gab_lexint(self);

  return TOKEN_NUMBER;
}

GAB_INTERNAL gab_token __gab_lexhex(gab_lx *self) {
  while (__gab_lexcancontinuehex(__gab_lexpeek(self)))
    __gab_lexadvance(self);

  // Check for a binary exponent
  if (__gab_lexpeek(self) == 'p' && __gab_lexisexp(__gab_lexpeeknext(self)))
    return __gab_lexadvance(self), __gab_lexadvance(self), __gab_lexint(self);

  return TOKEN_NUMBER;
}

GAB_INTERNAL gab_token __gab_lexnum(gab_lx *self) {
  if (__gab_lexpeek(self) == '0' && __gab_lexpeeknext(self) == 'x')
    return __gab_lexadvance(self), __gab_lexadvance(self), __gab_lexhex(self);

  if (__gab_lexint(self) == TOKEN_ERROR)
    return TOKEN_ERROR;

  // Check for a decimal portion of the number
  if (__gab_lexpeek(self) == '.' && isdigit(__gab_lexpeeknext(self)))
    return __gab_lexadvance(self), __gab_lexadvance(self), __gab_lexdec(self);

  // Check for a decimal exponent to the number
  if (__gab_lexpeek(self) == 'e' && __gab_lexisexp(__gab_lexpeeknext(self)))
    return __gab_lexadvance(self), __gab_lexadvance(self), __gab_lexint(self);

  return TOKEN_NUMBER;
}

GAB_INTERNAL gab_token __gab_lexother(gab_lx *self) {
  switch (__gab_lexpeek(self)) {
  case ';':
    __gab_lexadvance(self);
    return TOKEN_NEWLINE;
  case ',':
    __gab_lexadvance(self);
    return TOKEN_NEWLINE;
  case '(':
    __gab_lexadvance(self);
    return TOKEN_LPAREN;
  case ')':
    __gab_lexadvance(self);
    return TOKEN_RPAREN;
  case '[':
    __gab_lexadvance(self);
    return TOKEN_LBRACE;
  case ']':
    __gab_lexadvance(self);
    return TOKEN_RBRACE;
  case '{':
    __gab_lexadvance(self);
    return TOKEN_LBRACK;
  case '}':
    __gab_lexadvance(self);
    return TOKEN_RBRACK;
  case ':':
    __gab_lexadvance(self);

    if (__gab_lexpeek(self) == ':')
      return __gab_lexadvance(self), TOKEN_COLONCOLON;

    if (__gab_lexpeek(self) == '=')
      return __gab_lexadvance(self), TOKEN_COLONEQUAL;

    return TOKEN_MESSAGE;
  case '\\':
    __gab_lexadvance(self);

    if (__gab_lexpeek(self) == '{') {
      __gab_lexadvance(self);
      return TOKEN_SLBRACK;
    }

    __gab_lexadvance(self);
    return __gab_lexerror(self, GAB_MALFORMED_TOKEN);
  case '.':
    __gab_lexadvance(self);

    if (__gab_lexcanstartop(__gab_lexpeek(self))) {
      __gab_lexadvance(self);

      enum gab_token t = __gab_lexop(self);

      if (t == TOKEN_OPERATOR)
        return TOKEN_SEND;

      return __gab_lexerror(self, GAB_MALFORMED_TOKEN);
    }

    if (__gab_lexcanstartsym(__gab_lexpeek(self))) {
      __gab_lexadvance(self);

      enum gab_token t = __gab_lexsym(self);

      if (t == TOKEN_SYMBOL)
        return TOKEN_SEND;

      return __gab_lexerror(self, GAB_MALFORMED_TOKEN);
    }

    if (isdigit(__gab_lexpeek(self)))
      return __gab_lexint(self);

    return TOKEN_SEND;

  default:
    if (__gab_lexcanstartop(__gab_lexpeek(self)))
      return __gab_lexop(self);

    __gab_lexadvance(self);
    return __gab_lexerror(self, GAB_MALFORMED_TOKEN);
  }
}

GAB_INTERNAL void __gab_lexskipcmt(gab_lx *self) {
  while (__gab_lexpeek(self) != '\n') {
    __gab_lexadvance(self);

    if (__gab_lexpeeknext(self) == '\0' || __gab_lexpeeknext(self) == EOF)
      break;
  }
}

GAB_INTERNAL gab_token __gab_lexnext(gab_lx *self) {
  if (self->cursor - self->source->source->data >= self->source->source->len)
    goto eof;

  while (isblank(__gab_lexpeek(self)) ||
         __gab_lexiscomment(__gab_lexpeek(self))) {
    if (__gab_lexiscomment(__gab_lexpeek(self)))
      __gab_lexskipcmt(self);

    if (isblank(__gab_lexpeek(self)))
      __gab_lexadvance(self);
  }

  gab_assert(self->cursor - self->source->source->data <
                 self->source->source->len,
             "Shall not have run out of data");

  gab_token tok;
  __gab_lextokbeg(self);

  if (__gab_lexpeek(self) == '\0' || __gab_lexpeek(self) == EOF) {
  eof:
    tok = TOKEN_EOF;
    v_gab_token_push(&self->source->tokens, tok);
    v_s_char_push(&self->source->token_srcs, self->current_token_src);
    v_uint64_t_push(&self->source->token_lines, self->row);

    __gab_lexrowend(self);

    return tok;
  }

  if (__gab_lexpeek(self) == '\r' && __gab_lexpeeknext(self) == '\n') {
    __gab_lexadvance(self);
    __gab_lexadvance(self);
    tok = TOKEN_NEWLINE;

    v_gab_token_push(&self->source->tokens, tok);
    v_s_char_push(&self->source->token_srcs, self->current_token_src);
    v_uint64_t_push(&self->source->token_lines, self->row);

    __gab_lexrowend(self);

    return tok;
  }

  if (__gab_lexpeek(self) == '\n') {
    __gab_lexadvance(self);
    tok = TOKEN_NEWLINE;

    v_gab_token_push(&self->source->tokens, tok);
    v_s_char_push(&self->source->token_srcs, self->current_token_src);
    v_uint64_t_push(&self->source->token_lines, self->row);

    __gab_lexrowend(self);

    return tok;
  }

  if (__gab_lexcanstartsym(__gab_lexpeek(self))) {
    tok = __gab_lexsym(self);
    goto fin;
  }

  if (__gab_lexpeek(self) == '-' && isdigit(__gab_lexpeeknext(self))) {
    __gab_lexadvance(self);
    tok = __gab_lexnum(self);
    goto fin;
  }

  if (isdigit(__gab_lexpeek(self))) {
    tok = __gab_lexnum(self);
    goto fin;
  }

  if (__gab_lexpeek(self) == '"') {
    tok = __gab_lexstr(self);
    goto fin;
  }

  if (__gab_lexpeek(self) == '\'') {
    tok = __gab_lexstr(self);
    goto fin;
  }

  tok = __gab_lexother(self);

fin:
  v_gab_token_push(&self->source->tokens, tok);
  v_s_char_push(&self->source->token_srcs, self->current_token_src);
  v_uint64_t_push(&self->source->token_lines, self->row);

  return tok;
}

GAB_INTERNAL void __gab_srcdestroy(struct gab_src *self) {
  a_char_destroy(self->source);

  v_s_char_destroy(&self->lines);

  v_gab_token_destroy(&self->tokens);
  v_s_char_destroy(&self->token_srcs);
  v_uint64_t_destroy(&self->token_lines);

  v_gab_value_destroy(&self->constants);

  v_uint8_t_destroy(&self->bytecode);
  v_uint64_t_destroy(&self->bytecode_toks);
  d_uint64_t_destroy(&self->node_begin_toks);
  d_uint64_t_destroy(&self->node_end_toks);

  for (uint64_t i = 0; i < self->len; i++) {
    if (self->thread_bytecode[i].constants)
      free(self->thread_bytecode[i].constants);
    if (self->thread_bytecode[i].bytecode)
      free(self->thread_bytecode[i].bytecode);
  }

  free(self);
}

GAB_INTERNAL struct gab_src *__gab_source(struct gab_triple gab, gab_value name,
                                          const char *source, uint64_t len) {
  mtx_lock(&gab.eg->sources_mtx);

  if (d_gab_src_exists(&gab.eg->sources, name)) {
    if (gab.flags & fGAB_USE_RELOAD) {
      // We should really free some resources here.
      // Eh, there are a lot of pointers dangling into this.
      // Probably best to just save it somewhere else.
    } else {
      struct gab_src *src = d_gab_src_read(&gab.eg->sources, name);

      mtx_unlock(&gab.eg->sources_mtx);

      return src;
    }
  }

  uint64_t sz =
      sizeof(struct gab_src) + (gab.eg->len) * sizeof(struct src_bytecode);

  struct gab_src *src = malloc(sz);
  memset(src, 0, sz);

  src->len = gab.eg->len;
  src->source = a_char_create(source, len);
  src->name = name;

  gab_egkeep(gab.eg, gab_iref(gab, name));

  if (!len)
    goto fin;

  gab_lx lex;
  __gab_lexcreate(&lex, src);

  for (;;) {
    gab_token t = __gab_lexnext(&lex);

    if (t == TOKEN_EOF)
      break;
  }

fin:
  d_gab_src_insert(&gab.eg->sources, name, src);

  mtx_unlock(&gab.eg->sources_mtx);

  return src;
}

GAB_INTERNAL uint64_t __gab_srcappend(struct gab_src *self, uint64_t len,
                                      uint8_t bc[static len],
                                      uint64_t toks[static len]) {
  v_uint8_t_cap(&self->bytecode, self->bytecode.len + len);
  v_uint64_t_cap(&self->bytecode_toks, self->bytecode_toks.len + len);

  for (uint64_t i = 0; i < len; i++) {
    v_uint8_t_push(&self->bytecode, bc[i]);
    v_uint64_t_push(&self->bytecode_toks, toks[i]);
  }

  gab_assert(self->bytecode.len == self->bytecode_toks.len,
             "Each bytecode shall have a token");

  return self->bytecode.len;
}

GAB_INTERNAL void __gab_srccomplete(struct gab_triple gab,
                                    struct gab_src *self) {
  for (int i = 0; i < self->len; i++) {
    uint8_t *bc = malloc(self->bytecode.len);
    memcpy(bc, self->bytecode.data, self->bytecode.len);

    gab_value *ks = malloc(self->constants.len * sizeof(gab_value));
    memcpy(ks, self->constants.data, self->constants.len * sizeof(gab_value));

    self->thread_bytecode[i] = (struct src_bytecode){bc, ks};
  }
}

GAB_API gab_value gab_srcname(struct gab_src *src) { return src->name; }

GAB_API uint64_t gab_srcline(struct gab_src *src, uint64_t bytecode_offset) {
  if (!src->source->len)
    return 0;

  uint64_t tok = v_uint64_t_val_at(&src->bytecode_toks, bytecode_offset);
  return v_uint64_t_val_at(&src->token_lines, tok);
}

GAB_API uint64_t gab_tsrcline(struct gab_src *src, uint64_t tok_offset) {
  if (!src->source->len)
    return 0;

  return v_uint64_t_val_at(&src->token_lines, tok_offset);
}

/* ----------------------------------------
 *
 *    GAB ENGINE
 *
 *  This section contains the code for managing a gab *engine*.
 *
 *  This includes:
 *    - thread management
 *    - data-structure setup/teardown
 *    - module/package resolution
 *    - signalling
 *    - generaly-useful wrapper apis
 * ----------------------------------------
 */

struct errdetails {
  const char *src_name, *tok_name, *msg_name;
  uint64_t token, row, col_begin, col_end, byte_begin, byte_end;
  enum gab_status status;
  int wkid;
};

GAB_API uint64_t gab_eglen(struct gab_eg *eg) { return eg->len; }

GAB_API gab_value *gab_egerrs(struct gab_eg *eg) {
  v_gab_value_thrd errs;
  v_gab_value_thrd_drain(&eg->err, &errs);

  if (!errs.len)
    return nullptr;

  v_gab_value_thrd_push(&errs, gab_nil);

  /* Just free the mutex, leave the pointer to be cleaned up by caller */
  mtx_destroy(&errs.mtx);
  gab_assert(errs.len > 0,
             "The array of errors shall have len > 0 in this codepath");
  gab_assert(
      errs.data != nullptr,
      "The array of errors returned shall not be null when errs.len > 0");
  return errs.data;
};

struct primitive {
  const char *name;
  union {
    gab_value val;
    enum gab_kind kind;
    const char *message;
  };
  gab_value primitive;
};

struct primitive all_primitives[] = {
    {
        .name = mGAB_TYPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_TYPE),
    },
};

struct primitive val_primitives[] = {
    {
        .name = mGAB_EQ,
        .val = gab_cundefined,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_EQ),
    },
    {
        .name = mGAB_CONS,
        .val = gab_cundefined,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CONS),
    },
};

struct primitive msg_primitives[] = {
    {
        .name = mGAB_MAKE,
        .message = tGAB_LIST,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LIST),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_FIBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_FIBER),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_RECORD),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_SHAPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SHAPE),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_STRING),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_BINARY,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BINARY),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_CHANNEL,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CHANNEL),
    },
    {
        .name = mGAB_BND,
        .message = "false",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LND),
    },
    {
        .name = mGAB_BOR,
        .message = "false",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LOR),
    },
    {
        .name = mGAB_LIN,
        .message = "false",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LIN),
    },
    {
        .name = mGAB_BND,
        .message = "true",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LND),
    },
    {
        .name = mGAB_BOR,
        .message = "true",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LOR),
    },
    {
        .name = mGAB_LIN,
        .message = "true",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LIN),
    },
};

struct primitive kind_primitives[] = {
    {
        .name = mGAB_BIN,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BIN),
    },
    {
        .name = mGAB_BIN,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BIN),
    },
    {
        .name = mGAB_BOR,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BOR),
    },
    {
        .name = mGAB_BND,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BND),
    },
    {
        .name = mGAB_LSH,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LSH),
    },
    {
        .name = mGAB_RSH,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_RSH),
    },
    {
        .name = mGAB_ADD,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_ADD),
    },
    {
        .name = mGAB_SUB,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SUB),
    },
    {
        .name = mGAB_MUL,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_MUL),
    },
    {
        .name = mGAB_DIV,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_DIV),
    },
    {
        .name = mGAB_MOD,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_MOD),
    },
    {
        .name = mGAB_LT,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LT),
    },
    {
        .name = mGAB_LTE,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LTE),
    },
    {
        .name = mGAB_GT,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_GT),
    },
    {
        .name = mGAB_GTE,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_GTE),
    },
    {
        .name = mGAB_LT,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_STR_LT),
    },
    {
        .name = mGAB_LTE,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_STR_LTE),
    },
    {
        .name = mGAB_GT,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_STR_GT),
    },
    {
        .name = mGAB_GTE,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_STR_GTE),
    },
    {
        .name = mGAB_ADD,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CONCAT),
    },
    {
        .name = mGAB_MAKE,
        .kind = kGAB_SHAPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_MAKE_SHAPE),
    },
    {
        .name = mGAB_SPLATLIST,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATLIST),
    },
    {
        .name = mGAB_SPLATLIST,
        .kind = kGAB_SHAPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATSHAPE),
    },
    {
        .name = mGAB_SPLATDICT,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATDICT),
    },
    {
        .name = mGAB_CONS,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CONS_RECORD),
    },
    {
        .name = mGAB_USE,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_USE),
    },
    {
        .name = mGAB_CALL,
        .kind = kGAB_NATIVE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CALL_NATIVE),
    },
    {
        .name = mGAB_CALL,
        .kind = kGAB_BLOCK,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CALL_BLOCK),
    },
    {
        .name = mGAB_PUT,
        .kind = kGAB_CHANNEL,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_PUT),
    },
    {
        .name = mGAB_TAKE,
        .kind = kGAB_CHANNEL,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_TAKE),
    },
};

struct native {
  const char *name;
  union {
    enum gab_kind kind;
    const char *message;
    const char *box_type;
  };
  gab_native_f native;
};

GAB_API enum gab_signal gab_yield(struct gab_triple gab) {
  if (gab_sigwaiting(gab)) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
#if cGAB_LOG_EG
    fprintf(stderr, "(%i) RECV SIG: %i\n", gab.wkid, sig.signal);
#endif
    return sig.signal;
  }

  return sGAB_IGN;
}

GAB_API void gab_busywait(struct gab_triple gab) {
  if (gab.eg->wait > 0) {
    thrd_sleep(&(const struct timespec){.tv_nsec = gab.eg->wait}, nullptr);
  }

  thrd_yield();
}

GAB_API int32_t gab_njobs(struct gab_triple gab) {
  struct gab_sig sig = atomic_load(&gab.eg->sig);
  return popcountl(sig.mask);
}

GAB_INTERNAL void __gab_jbalive(struct gab_triple gab, int32_t wkid) {
  for (;;) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    struct gab_sig next = {
        .schedule = sig.schedule,
        .signal = sig.signal,
        .mask = sig.mask | (1 << wkid),
    };

    gab_assert(
        !(next.signal == sGAB_IGN && next.schedule == 0),
        "Signal shall not be sGAB_IGN, and scheduled job shall not be 0.");

    if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
      break;

    gab_busywait(gab);
  }
}

GAB_INTERNAL bool __gab_jbisalive(struct gab_triple gab, int32_t wkid) {
  struct gab_sig sig = atomic_load(&gab.eg->sig);
  return sig.mask & (1 << wkid);
}

GAB_INTERNAL void __gab_jbunalive(struct gab_triple gab, int32_t wkid) {
  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      // This should remain the *only* place in the system
      // where we propagate the TERM signal.
      gab_sigpropagate(gab);
      break;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    struct gab_sig next = {
        .schedule = sig.schedule,
        .signal = sig.signal,
        .mask = sig.mask & ~(1 << wkid),
    };

    gab_assert(
        !(next.signal == sGAB_IGN && next.schedule == 0),
        "Signal shall not be sGAB_IGN, and scheduled job shall not be 0.");

    if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
      break;
  }
}

const char *kind_strs[] = {
    [kGAB_STRING] = "gab\\string",
    [kGAB_BINARY] = "gab\\binary",
    [kGAB_MESSAGE] = "gab\\message",
    [kGAB_PRIMITIVE] = "gab\\primitive",
    [kGAB_NUMBER] = "gab\\number",
    [kGAB_NATIVE] = "gab\\native",
    [kGAB_PROTOTYPE] = "gab\\prototype",
    [kGAB_BLOCK] = "gab\\block",
    [kGAB_BOX] = "gab\\box",
    [kGAB_RECORD] = "gab\\record",
    [kGAB_RECORDNODE] = "gab\\recordnode",
    [kGAB_SHAPE] = "gab\\shape",
    [kGAB_SHAPENODE] = "gab\\shapenode",
    [kGAB_SHAPELIST] = "gab\\shapelist",
    [kGAB_FIBER] = "gab\\fiber",
    [kGAB_FIBERDONE] = "gab\\fiberdone",
    [kGAB_FIBERRUNNING] = "gab\\fiberrunning",
    [kGAB_CHANNEL] = "gab\\channel",
    [kGAB_CHANNELCLOSED] = "gab\\channelclosed",
    [kGAB_NKINDS] = "none",
};

int32_t __gab_jbgc(void *data) {
  struct gab_triple *g = data;
  struct gab_triple gab = *g;

  gab_precondition(gab.wkid == 0, "The gc-job shall have wkid '0'");

  struct gab_job *job = gab.eg->jobs + gab.wkid;

#if cGAB_LOG_EG
  fprintf(stderr, "[GCWORKER] STARTING\n");
#endif

  cnd_init(&gab.eg->gc_cnd);
  mtx_lock(&gab.eg->gc_mtx);

#if cGAB_LOG_EG
  fprintf(stderr, "[GCWORKER] WAITING\n");
#endif

  while (gab_njobs(gab) > 0) {
    int res = cnd_wait(&gab.eg->gc_cnd, &gab.eg->gc_mtx);

    if (res == thrd_timedout)
      continue;

    for (;;) {
      struct gab_sig sig = atomic_load(&gab.eg->sig);
      struct gab_sig expected = {sig.mask, -2, sig.signal};
      struct gab_sig desired = {sig.mask, -1, sig.signal};
      if (atomic_compare_exchange_weak(&gab.eg->sig, &expected, desired))
        break;
    }

    if (res == thrd_error)
      continue;

#if cGAB_LOG_EG
    fprintf(stderr, "[GCWORKER] RECEIVE SIGNAL\n");
#endif

  read_signal:
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      gab_sigclear(gab);
      continue;
    case sGAB_COLL: {
#if cGAB_LOG_EG
      fprintf(stderr, "[GCWORKER] COLLECTING\n");
      for (int i = 0; i < kGAB_NKINDS; i++) {
        uint64_t count = gab.eg->counts[i];
        uint64_t total = gab.eg->sizes[i];
        fprintf(stderr, "\t[%s] => %li objects, %li avg. %li total bytes.\n",
                kind_strs[i], count, count ? total / count : 0, total);
      }
#endif
      gab_gcdocollect(gab);
      gab_sigclear(gab);

      struct gab_sig sig = atomic_load(&gab.eg->sig);

      gab_assert(!(sig.schedule == 0 && sig.signal == sGAB_IGN),
                 "Signal shall have been cleared");

      continue;
    }
    case sGAB_IGN:
      gab_busywait(gab);
      // If we woke up due to a signal, we need
      // to continue looping until we receive the signal.
      if (res == thrd_success && gab_njobs(gab) > 0)
        goto read_signal;

      break;
    }

    /*
     * TODO @cgab @runtime: Coordinate work stealing here, where we are
     * guaranteed to *not* be collecting.
     *
     * if we have spare jobs:
     *  look for the first worker
     */
  }

#if cGAB_LOG_EG
  fprintf(stderr, "[GCWORKER] BAILING\n");
#endif
  free(g);
  v_gab_value_destroy(&job->lock_keep);
  cnd_destroy(&gab.eg->gc_cnd);
  mtx_unlock(&gab.eg->gc_mtx);
  return 0;
}

/*
 * TODO @cgab @runtime: Implement some form of work stealing (Or preemption).
 */
GAB_INTERNAL bool __gab_jbisrunning(struct gab_triple gab,
                                    struct gab_job *job) {
  if (q_gab_value_is_empty(&job->working_queue))
    return false;

  gab_value fiber = q_gab_value_peek(&job->working_queue);
  return gab_valkind(fiber) == kGAB_FIBERRUNNING;
}

static const char *gab_opcode_names[] = {
#define OP_CODE(name) #name,
#include "bytecode.h"
#undef OP_CODE
#undef GAB_OPCODE_NAMES_IMPL
};

GAB_INTERNAL bool __gab_jbstep(struct gab_triple gab, struct gab_job *job) {
  switch (gab_yield(gab)) {
  case sGAB_COLL:
    gab_gcepochnext(gab);
    gab_sigpropagate(gab);
    break;
  case sGAB_TERM:
    return false;
  default:
    break;
  }

  bool workqempty = q_gab_value_is_empty(&job->working_queue);

#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) TAKING WITH $ tries\n", gab_number(gab.wkid),
              gab_number(cGAB_JOB_IDLE_TRIES * workqempty));
#endif

  /*
   * Pull a value from our specific work channel.
   *
   * If our working queue is empty, we wait IDLE_TRIES.
   * If it isn't, don't waste cycles waiting.
   */
  gab_value fiber =
      gab_tchntake(gab, job->work_channel, cGAB_JOB_IDLE_TRIES * workqempty);

#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) local chntake result: $\n", gab_number(gab.wkid),
              fiber);
#endif

  // Terminate if requested.
  // If the channel closed, terminate
  if (fiber == gab_cinvalid || fiber == gab_cundefined)
    return false;

  // If we timed out, pull from the global work_channel
  if (fiber == gab_ctimeout)
    fiber = gab_tchntake(gab, gab.eg->work_channel,
                         cGAB_JOB_IDLE_TRIES * workqempty);

  // Terminate if requested.
  // If the channel closed, terminate
  if (fiber == gab_cinvalid || fiber == gab_cundefined)
    return false;

  if (fiber != gab_ctimeout) {
    gab_assert(gab_valkind(fiber) == kGAB_FIBER,
               "(%i) Fibers in queue shall only have kind "
               "kGAB_FIBER, not %d.",
               gab.wkid, gab_valkind(fiber));

#if cGAB_LOG_EG
    gab_fprintf(stderr, "($) global chntake result: $\n", gab_number(gab.wkid),
                fiber);
#endif

    // Our global take succeeded - append to our local queue.
    if (!q_gab_value_dyn_push(&job->waiting_queue, fiber))
      gab_unreachable("Shall not fail to append fiber to waiting queue.");
  }

  if (!q_gab_value_is_full(&job->working_queue)) {
    fiber = q_gab_value_dyn_pop(&job->waiting_queue);
    // TODO @cgab @bug: Properly handle these lifetimes.
    // gab_dref(gab, fiber);

    if (fiber != gab_cinvalid) {
#if cGAB_LOG_EG
      gab_fprintf(stderr, "($) TRANSFER $ WAITING => WORKING\n",
                  gab_number(gab.wkid), fiber);
#endif

      if (!q_gab_value_push(&job->working_queue, fiber))
        gab_unreachable("May not fail to push to working queue.");
    }
  }

  if (q_gab_value_is_empty(&job->working_queue))
    return gab_busywait(gab), true;

  // Peek at job to do on the queue.
  fiber = q_gab_value_peek(&job->working_queue);

  gab_assert(
      gab_valkind(fiber) == kGAB_FIBER,
      "(%i) Fibers in the queue shall only have type kGAB_FIBER, not %d (%p).",
      gab.wkid, gab_valkind(fiber), gab_valtoo(fiber));

  gab_assert(
      q_gab_value_peek(&job->working_queue) == fiber,
      "(%i) The fiber about to be run shall be at the front of the queue.",
      gab.wkid);

#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) EXECUTING $\n", gab_number(gab.wkid), fiber);
#endif

  // Run our fiber.
  union gab_value_pair res = __gab_vmexec(gab, fiber);

#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) EXECUTED: $ -> $\n", gab_number(gab.wkid), fiber,
              res.status);

  if (res.status == gab_ctimeout) {
    fprintf(stderr, "(%i) TIMED OUT FROM %s\n", gab.wkid,
            gab_opcode_names[*GAB_VAL_TO_FIBER(fiber)->vm.ip]);
  }
#endif

  // We did work - pop it off the queue now.
  gab_value popped = q_gab_value_pop(&job->working_queue);
  gab_assert(fiber == popped, "The popped fiber shall match the fiber we ran "
                              "off the front of the queue.");

  switch (res.status) {
  case gab_ctimeout:
    gab_assert(!gab_fibisrunning(popped),
               "A popped fiber shall not be running");
    gab_assert(!gab_fibisdone(popped), "A timedout fiber shall not be done");

    gab_assert(gab_valkind(fiber) == kGAB_FIBER,
               "Fibers in the queue shall only have type kGAB_FIBER, not %d.",
               gab_valkind(fiber));

    // We did not complete the work. Push back onto our queue.
    if (!q_gab_value_push(&job->working_queue, fiber))
      gab_unreachable(
          "There is guaranteed to be space for the fiber in this codepath.");
    break;
  // We completed the work. Nothing else to do.
  case gab_cvalid:
    gab_assert(gab_fibisdone(popped), "A valid fiber shall be done");

    // We panicked. Crash the system.
    if (res.aresult->data[0] != gab_ok) {
      gab_value err = res.aresult->data[1];
      if (err != gab_cinvalid) {
        gab_iref(gab, err);
        gab_egkeep(gab.eg, err);

        v_gab_value_thrd_push(&gab.eg->err, err);

        if (gab.flags & fGAB_SIGTERM_ON_ERR)
          gab_sigterm(gab);
      }
    }
    break;

  // We were interruppted by sGAB_TERM. Signal will be handled below.
  case gab_cinvalid:
    gab_assert(gab_fibisdone(popped), "A terminated fiber shall be done");

    return false;
  default:
    gab_unreachable("Unhandled result.status value");
  }

  return true;
}

GAB_INTERNAL void __gab_jbbail(struct gab_triple gab, struct gab_job *job) {
#if cGAB_LOG_EG
  fprintf(stderr, "(%i) BAILING\n", gab.wkid);
#endif
  // Before waiting, check that we aren't already dead.
  if (!__gab_jbisalive(gab, gab.wkid))
    return;

  // Wait for the terminate signal to arrive for this thread
  while (!gab_sigwaiting(gab))
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      continue;
    case sGAB_TERM:
      goto bail;
    case sGAB_IGN:
      break;
    }

bail:
  while (!q_gab_value_is_empty(&job->working_queue)) {
    gab_value fiber = q_gab_value_peek(&job->working_queue);

    gab_assert(gab_sigwaiting(gab), "While bailing, there shall be a sGAB_TERM "
                                    "signal waiting for this worker");

    gab_assert(gab_valkind(fiber) == kGAB_FIBER,
               "Fibers in the queue should only have type kGAB_FIBER, not %d",
               gab_valkind(fiber));

    // Run each queued fiber. Since there is a TERM signal waiting on this
    // worker, each fiber will terminate itself here, in one instruction.
    union gab_value_pair res = __gab_vmexec(gab, fiber);
#if cGAB_LOG_EG
    if (res.status == gab_ctimeout)
      gab_fprintf(stderr, "($) Failed to term $\n", gab_number(gab.wkid),
                  fiber);
#endif
    // Ensure that the termination occurred.
    gab_assert(res.status != gab_ctimeout,
               "One step of execution shall 'bail' the fiber. %s did not bail.",
               gab_opcode_names[*gab_fibvm(fiber)->ip]);

    gab_assert(gab_fibisdone(fiber), "A terminated fiber shall be done");

    // gab_value err = gab_fibstacktrace(gab, fiber);
    //
    // gab_iref(gab, err);
    // gab_egkeep(gab.eg, err);
    //
    // v_gab_value_thrd_push(&gab.eg->err, err);

    // Truly pop off the fiber now.
    gab_value popped = q_gab_value_pop(&job->working_queue);

    gab_assert(job->locked == 0,
               "The worker shall have a balanced 'lock' value of 0 when "
               "bailed. Saw %d. Last ran: %s.",
               job->locked, gab_opcode_names[*gab_fibvm(popped)->ip]);
  }

  gab_assert(q_gab_value_is_empty(&job->working_queue),
             "The queue shall be empty once all fibers have bailed");

  gab_assert(
      job->locked == 0,
      "The worker (%i) shall have a balanced 'lock' value of 0 when bailed. "
      "Saw %d.",
      gab.wkid, job->locked);

  __gab_jbunalive(gab, gab.wkid);

  v_gab_value_destroy(&job->lock_keep);
}

GAB_API uint64_t gab_egalive(struct gab_eg *eg) {
  struct gab_sig sig = atomic_load(&eg->sig);
  return popcountl(sig.mask);
}

int32_t __gab_jbworker(void *data) {
  struct gab_triple *g = data;
  struct gab_triple gab = *g;

  gab_precondition(
      gab.wkid > 1,
      "A workers id shall be greater than 1 (0 and 1 are reserved)");

  __gab_jbalive(gab, gab.wkid);

  struct gab_job *job = gab.eg->jobs + gab.wkid;

#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) SPAWNED\n", gab_number(gab.wkid));
#endif

  while (__gab_jbstep(gab, job))
    ;

  __gab_jbbail(gab, job);

#if cGAB_LOG_EG
  fprintf(stderr, "(%i) CLOSING\n", gab.wkid);
#endif

  free(g);

  return 0;
}

GAB_INTERNAL struct gab_job *__gab_jbnext(struct gab_triple gab) {
  for (;;) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    uint32_t shifted = sig.mask >> gab.wkid;
    uint32_t next_available = __builtin_ctzl(~shifted);

    uint64_t idx = gab.wkid + next_available;

    if (idx >= gab.eg->len)
      return nullptr;

    struct gab_sig next = {
        .schedule = sig.schedule,
        .signal = sig.signal,
        .mask = sig.mask | (1 << idx),
    };

    gab_assert(!(next.signal == sGAB_IGN && next.schedule == 0), "");
    if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
      return gab.eg->jobs + idx;

    gab_busywait(gab);
  }

  // No room for new jobs
  return nullptr;
}

GAB_INTERNAL bool __gab_job(struct gab_triple gab, struct gab_job *job,
                            int(fn)(void *), gab_value fiber) {
  if (!job)
    return false;

#if cGAB_LOG_EG
  fprintf(stderr, "(%i) spawning %lu\n", gab.wkid, job - gab.eg->jobs);
#endif

  job->locked = 0;
  v_gab_value_create(&job->lock_keep, 8);
  q_gab_value_create(&job->working_queue, 32);
  q_gab_value_dyn_create(&job->waiting_queue, 32);

  job->work_channel = gab_channel(gab);
  gab_iref(gab, job->work_channel);
  gab_egkeep(gab.eg, job->work_channel);

  if (fiber != gab_cundefined) {
#if cGAB_LOG_EG
    gab_fprintf(stderr, "($) SPAWN $\n", gab_number(gab.wkid), fiber);
#endif
    if (!q_gab_value_push(&job->working_queue, fiber))
      gab_unreachable("The queue shall always have space in this codepath");
  }

  if (!fn)
    return true;

  struct gab_triple *gabcpy = malloc(sizeof(struct gab_triple));
  memcpy(gabcpy, &gab, sizeof(struct gab_triple));
  gabcpy->wkid = job - gab.eg->jobs;

  gab_assert(gabcpy->wkid != 1,
             "The copy's worker id shall not be the 'main thread' id");

  return thrd_create(&job->td, fn, gabcpy) == thrd_success;
}

GAB_INTERNAL bool __gab_jbspawn(struct gab_triple gab, gab_value fiber) {
  return __gab_job(gab, __gab_jbnext(gab), __gab_jbworker, fiber);
}

GAB_API union gab_value_pair gab_create(struct gab_create_argt args,
                                        struct gab_triple gab_out[static 1]) {
  uint64_t njobs = args.jobs ? args.jobs : cGAB_DEFAULT_NJOBS;

  if (njobs > 29)
    return (union gab_value_pair){{gab_cinvalid}};

  uint64_t actual_njobs = njobs + 2;

  uint64_t egsize =
      sizeof(struct gab_eg) + sizeof(struct gab_job) * actual_njobs;

  struct gab_eg *eg = malloc(egsize);
  memset(eg, 0, egsize);

  eg->wait = args.wait ? args.wait : cGAB_DEFAULT_WAIT_NS;
  eg->len = actual_njobs;
  eg->hash_seed = time(nullptr);
  atomic_init(&eg->sig, (struct gab_sig){0, -1, 0});

  // The only non-zero initialization that jobs need is epoch = 1
  for (uint64_t i = 0; i < eg->len; i++)
    eg->jobs[i].epoch = 1;

  v_gab_value_thrd_create(&eg->err, 8);

  mtx_init(&eg->sources_mtx, mtx_plain);
  mtx_init(&eg->gc_mtx, mtx_plain);
  mtx_init(&eg->modules_mtx, mtx_plain);

  d_gab_src_create(&eg->sources, 8);
  d_strings_create(&eg->strings, 8);
  d_shapes_create(&eg->shapes, 8);

  gab_out->eg = eg;
  gab_out->flags = args.flags;
  gab_out->wkid = 1;

  struct gab_triple gab = *gab_out;

  // Maybe we can create/reserve a slot in jobs for
  // the user's main thread here?
  // gab wkid 1 can always be main thread.
  // we can have a flag that says 'detatch' or something
  // and will allow the main thread to begin contributing to the system.
  bool res = __gab_job(gab, gab.eg->jobs + 1, nullptr, gab_cundefined);
  gab_assert(res, "Job creation shall not fail for the main thread");

  __gab_jbalive(gab, 1);

  gab_gccreate(gab);

  res = __gab_job(gab, gab.eg->jobs, __gab_jbgc, gab_cundefined);
  gab_assert(res, "Job creation shall not fail for the gc thread");

  __gab_jbalive(gab, 0);

  gab_gclock(gab);

  eg->types[kGAB_NUMBER] = gab_string(gab, tGAB_NUMBER);
  eg->types[kGAB_BINARY] = gab_string(gab, tGAB_BINARY);
  eg->types[kGAB_STRING] = gab_string(gab, tGAB_STRING);
  eg->types[kGAB_MESSAGE] = gab_string(gab, tGAB_MESSAGE);
  eg->types[kGAB_PROTOTYPE] = gab_string(gab, tGAB_PROTOTYPE);
  eg->types[kGAB_NATIVE] = gab_string(gab, tGAB_NATIVE);
  eg->types[kGAB_BLOCK] = gab_string(gab, tGAB_BLOCK);
  eg->types[kGAB_SHAPE] = gab_string(gab, tGAB_SHAPE);
  eg->types[kGAB_SHAPELIST] = gab_string(gab, tGAB_SHAPE);
  eg->types[kGAB_RECORD] = gab_string(gab, tGAB_RECORD);
  eg->types[kGAB_RECORDNODE] = gab_string(gab, tGAB_RECORD);
  eg->types[kGAB_BOX] = gab_string(gab, tGAB_BOX);
  eg->types[kGAB_FIBER] = gab_string(gab, tGAB_FIBER);
  eg->types[kGAB_FIBERDONE] = gab_string(gab, tGAB_FIBER);
  eg->types[kGAB_FIBERRUNNING] = gab_string(gab, tGAB_FIBER);
  eg->types[kGAB_CHANNEL] = gab_string(gab, tGAB_CHANNEL);
  eg->types[kGAB_CHANNELCLOSED] = gab_string(gab, tGAB_CHANNEL);
  eg->types[kGAB_PRIMITIVE] = gab_string(gab, tGAB_PRIMITIVE);

  gab_niref(gab, 1, kGAB_NKINDS, eg->types);
  gab_negkeep(gab.eg, kGAB_NKINDS, eg->types);

  atomic_init(&eg->messages, gab_erecord(gab));

  eg->work_channel = gab_iref(gab, gab_channel(gab));

  int nroots = 0;

  if (args.roots)
    for (int i = 0; args.roots[i] != nullptr; i++) {
      gab_precondition(nroots < cGAB_RESOURCE_MAX,
                       "Number of roots shall not exceed cGAB_RESOURCE_MAX");
      gab.eg->resroots[nroots++] = args.roots[i];
    }

  int nres = 0;

  if (args.resources)
    for (int i = 0; args.resources[i].prefix != nullptr; i++) {
      gab_precondition(
          nres < cGAB_RESOURCE_MAX,
          "Number of resources shall not exceed cGAB_RESOURCE MAX");
      gab.eg->res[nres++] = args.resources[i];
    }

  for (int i = 0; i < LEN_CARRAY(kind_primitives); i++) {
    gab_egkeep(
        gab.eg,
        gab_iref(gab,
                 gab_def(gab, (struct gab_def_argt){
                                  gab_message(gab, kind_primitives[i].name),
                                  gab_type(gab, kind_primitives[i].kind),
                                  kind_primitives[i].primitive,
                              })));
  }

  for (int i = 0; i < LEN_CARRAY(val_primitives); i++) {
    gab_egkeep(
        gab.eg,
        gab_iref(gab, gab_def(gab, (struct gab_def_argt){
                                       gab_message(gab, val_primitives[i].name),
                                       val_primitives[i].val,
                                       val_primitives[i].primitive,
                                   })));
  }

  for (int i = 0; i < LEN_CARRAY(msg_primitives); i++) {
    gab_egkeep(
        gab.eg,
        gab_iref(gab,
                 gab_def(gab, (struct gab_def_argt){
                                  gab_message(gab, msg_primitives[i].name),
                                  gab_message(gab, msg_primitives[i].message),
                                  msg_primitives[i].primitive,
                              })));
  }

  for (int i = 0; i < LEN_CARRAY(all_primitives); i++) {
    for (int t = 0; t < kGAB_NKINDS; t++) {
      gab_egkeep(
          gab.eg,
          gab_iref(gab,
                   gab_def(gab, (struct gab_def_argt){
                                    gab_message(gab, all_primitives[i].name),
                                    gab_type(gab, t),
                                    all_primitives[i].primitive,
                                })));
    }
  }

  gab_gcunlock(gab);

  uint64_t len = 0;
  struct gab_package *cursor = args.packages;
  while (cursor && cursor->package)
    len++, cursor++;

  uint64_t nargs = 0;
  gab_value vargs[len + 1];
  const char *sargs[len + 1];

  sargs[nargs] = "";
  vargs[nargs] = gab_ok;
  nargs++;

  // Use each module that's asked for, in order.
  // Build up an array of names and values.
  for (int i = 0; i < len; i++) {
    struct gab_package *pkg = args.packages + i;

    union gab_value_pair res = gab_use(gab, (struct gab_use_argt){
                                                .spackage_name = pkg->package,
                                                .smodule_name = pkg->module,
                                                .len = nargs,
                                                .sargv = sargs,
                                                .argv = vargs,
                                            });
    if (res.status == gab_ctimeout)
      res = gab_fibawait(gab, res.vresult);

    // If any of these uses fail, return the failure.
    if (res.status != gab_cvalid)
      return res;

    if (res.aresult->data[0] != gab_ok)
      return res;

    vargs[nargs] = res.aresult->data[1];
    sargs[nargs] = pkg->alias    ? pkg->alias
                   : pkg->module ? pkg->module
                                 : pkg->package;
    nargs++;
  }

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(vargs, nargs),
  };
}

GAB_INTERNAL bool __gab_gcisdone(struct gab_triple gab);

GAB_API void gab_destroy(struct gab_triple gab) {
  gab_precondition(gab.wkid == 1, "Shall only be called from the main thread");

  bool res = gab_sigterm(gab);
  gab_assert(res, "Sigterm shall not fail when destryoing");

  if (__gab_jbisalive(gab, gab.wkid))
    __gab_jbbail(gab, gab.eg->jobs + 1);

  while (gab_njobs(gab) > 1)
    gab_busywait(gab);

  gab_dref(gab, gab.eg->work_channel);
  gab_ndref(gab, 1, gab.eg->scratch.len, gab.eg->scratch.data);

  atomic_store(&gab.eg->messages, gab_cinvalid);

  gab_assert(gab_njobs(gab) == 1,
             "There shall only be one thread remaining - the gc thread");

  /**
   * Four consececutive collections are needed here because
   * of the delayed nature of the RC algorithm.
   *
   * Decrements are process an epoch *after* they are queued.
   *
   * There are three epochs tracked, so we need three collections
   * to ensure that all rc events are processed.
   */

  // First, clear any pending signals.
  // I'm not sure if this makes sense to do here,
  // Or if this is the responsibility of user to clear
  // signal *before* calling this function.
  // gab_sigclear(gab);

  // gab_gcloglen(gab);
  res = gab_sigcoll(gab);
  while (gab_signaling(gab))
    gab_busywait(gab);

  gab_assert(res, "sigcoll shall not fail");
  struct gab_sig sig = atomic_load(&gab.eg->sig);
  gab_assert(sig.signal == sGAB_IGN,
             "After collection, signal shall be sGAB_IGN");

  // gab_gcloglen(gab);
  res = gab_sigcoll(gab);
  while (gab_signaling(gab))
    gab_busywait(gab);

  gab_assert(res, "sigcoll shall not fail");
  sig = atomic_load(&gab.eg->sig);
  gab_assert(sig.signal == sGAB_IGN,
             "After collection, signal shall be sGAB_IGN");

  // gab_gcloglen(gab);
  res = gab_sigcoll(gab);

  while (gab_signaling(gab))
    gab_busywait(gab);

  gab_assert(res, "sigcoll shall not fail");
  sig = atomic_load(&gab.eg->sig);
  gab_assert(sig.signal == sGAB_IGN,
             "After collection, signal shall be sGAB_IGN");

  // gab_gcloglen(gab);
  res = gab_sigcoll(gab);

  while (gab_signaling(gab))
    gab_busywait(gab);

  gab_assert(res, "sigcoll shall not fail");
  sig = atomic_load(&gab.eg->sig);
  gab_assert(sig.signal == sGAB_IGN,
             "After collection, signal shall be sGAB_IGN");

  gab_verify(__gab_gcisdone(gab), "GC Buffers shall be empty");

  gab_assert(gab_njobs(gab) == 1,
             "There shall only be one worker alive - the gc thread");

  gab_gcdestroy(gab);

  gab_sigterm(gab);

  thrd_join(gab.eg->jobs[0].td, nullptr);

  for (uint64_t i = 0; i < gab.eg->sources.cap; i++) {
    if (d_gab_src_iexists(&gab.eg->sources, i)) {
      struct gab_src *s = d_gab_src_ival(&gab.eg->sources, i);
      __gab_srcdestroy(s);
    }
  }

  d_strings_destroy(&gab.eg->strings);
  d_shapes_destroy(&gab.eg->shapes);
  d_gab_modules_destroy(&gab.eg->modules);
  d_gab_src_destroy(&gab.eg->sources);

  v_gab_value_destroy(&gab.eg->scratch);
  v_gab_value_thrd_destroy(&gab.eg->err);

  mtx_destroy(&gab.eg->gc_mtx);
  mtx_destroy(&gab.eg->sources_mtx);
  mtx_destroy(&gab.eg->modules_mtx);

  free(gab.eg);
}

GAB_INTERNAL bool __gab_replchkres(struct gab_triple gab,
                                   union gab_value_pair res) {
  gab_value *err = gab_egerrs(gab.eg);

  while (gab_signaling(gab))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      gab_sigpropagate(gab);
      break;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      continue;
    }

  if (err) {
    for (gab_value *thiserr = err; *thiserr != gab_nil; thiserr++) {
      gab_assert(gab_valkind(*thiserr) == kGAB_RECORD,
                 "An error value shall be a record");

      if (*thiserr == res.vresult)
        continue;

      const char *errstr = gab_errtocs(gab, *thiserr);

      if (errstr)
        puts(errstr);

      // TODO @cgab @bug: Do something else if errtocs fails
      gab_assert(errstr, "gab_errtocs should not produce nil. This happens if "
                         "you try to create strings while signalling.");
    };

    free(err);
  }

  if (res.status != gab_cvalid) {
    const char *errstr = gab_errtocs(gab, res.vresult);

    if (errstr)
      puts(errstr);

    return true;
  }

  return err != nullptr;
}

GAB_INTERNAL bool __gab_replchkmore(struct gab_triple gab,
                                    union gab_value_pair res) {
  if (res.status != gab_cinvalid)
    return false;

  gab_value err = res.vresult;
  gab_value status = gab_mrecat(gab, err, "status");
  gab_assert(status != gab_cundefined,
             "The error record shall have a status field");

  gab_assert(gab_valkind(status) == kGAB_STRING,
             "The status field shall be a string, not %d.",
             gab_valkind(status));

  const char *status_name = gab_strdata(&status);
  if (!strcmp(status_name, "UNEXPECTED_EOF"))
    return true;

  return false;
}

/*
 * Should be able to take work here on 1st worker maybe.
 */
GAB_INTERNAL void __gab_replwait(struct gab_triple gab,
                                 struct gab_repl_argt *args, gab_value fib) {
  while (!gab_fibisdone(fib)) {
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      // Fallthrough
    case sGAB_TERM:
      gab_sigpropagate(gab);
      // Fallthrough
    default:
      gab_busywait(gab);
      continue;
    }
  }
}

GAB_API void gab_repl(struct gab_triple gab, struct gab_repl_argt args) {
  uint64_t iterations = 0;
  gab_value env = gab_cinvalid;

  args.welcome_message = args.welcome_message ? args.welcome_message : "";
  args.result_prefix = args.result_prefix ? args.result_prefix : "";
  args.prompt_prefix = args.prompt_prefix ? args.prompt_prefix : "";
  args.promptmore_prefix =
      args.promptmore_prefix ? args.promptmore_prefix : args.prompt_prefix;

  printf("%s\n", args.welcome_message);

  v_char source = {};

  for (;;) {

  readmore:
    char *line;
    if (source.len)
      line = args.readline(args.promptmore_prefix);
    else
      line = args.readline(args.prompt_prefix);

    if (!line)
      return;

    if (line[0] == '\0')
      continue;

    if (args.add_hist)
      args.add_hist(line);

    v_char_spush(&source, s_char_cstr(line));
    v_char_push(&source, '\n');

    iterations++;

  retry:
    // Skip self
    uint64_t reclen = env == gab_cinvalid ? 0 : (gab_reclen(env) - 1);

    uint64_t len = reclen + args.len;

    // Allocate the maximum number of space we may need
    const char *keys[len + 1];
    gab_value keyvals[len + 1];
    gab_value vals[len + 1];

    // TODO @engine @bug: The calls to string below need to have signals handled
    // This *kind* of works, but a fiber can signal us *after* we pass this
    // point.
    while (gab_signaling(gab))
      switch (gab_yield(gab)) {
      case sGAB_TERM:
        gab_sigpropagate(gab);
        break;
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        continue;
      }

    // Insert local names from env
    for (uint64_t i = 0; i < reclen; i++) {
      uint64_t index = i + 1;
      keyvals[i] = gab_ukrecat(env, index);
      vals[i] = gab_uvrecat(env, index);
      keys[i] = gab_strdata(keyvals + i);
    }

    // If there is a local name in the env, skip it from the args.
    // otherwise, add an element for this.
    uint64_t n = reclen;
    for (uint64_t i = 0; i < args.len; i++) {
      gab_value vkey = gab_string(gab, args.sargv[i]);
      if (vkey == gab_cinvalid)
        goto retry;

      if (env != gab_cinvalid && gab_rechas(env, vkey))
        continue;

      keys[n] = args.sargv[i];
      vals[n] = args.argv[i];
      n++;
    }

    // Append the iterations number to the end of the given name
    char unique_name[strlen(args.name) + 16];
    snprintf(unique_name, sizeof(unique_name), "%s:%" PRIu64 "", args.name,
             iterations);

    union gab_value_pair block = gab_build(gab, (struct gab_parse_argt){
                                                    .name = unique_name,
                                                    .source = source.data,
                                                    .source_len = source.len,
                                                    .flags = args.flags,
                                                    .len = n,
                                                    .argv = keys,
                                                });
    if (__gab_replchkmore(gab, block))
      goto readmore;

    if (__gab_replchkres(gab, block))
      goto fin;

    // gab_value before_env = gab_blkshp(block.vresult);

    union gab_value_pair fiber = gab_arun(gab, (struct gab_run_argt){
                                                   .flags = args.flags,
                                                   .len = n,
                                                   .argv = vals,
                                                   .main = block.vresult,
                                               });

    if (__gab_replchkres(gab, fiber))
      goto fin;

    __gab_replwait(gab, &args, fiber.vresult);

    union gab_value_pair res = gab_fibawait(gab, fiber.vresult);

    /* Setup env regardless of run failing/succeeding */
    // TODO @bug: replace awaite - thats gross.
    // how else can I get variables to work in the repl?
    // gab_value new_env = gab_fibawaite(gab, fiber.vresult);

    /* Sometimes the env that is returned from here is
     *  an env from a ~different~ block. This is because
     *  we always tailcall, so the bottom frame can change the block
     *  it belongs to throughout execution.
     **/

    // if (env == gab_cinvalid || new_env == gab_cinvalid)
    // env = new_env;
    // If the block's environment is equal to the fiber's final environment
    // then we know we *didn't* tailcall out of the block.
    // TODO @cgab @bug: Don't leak this reccat below
    // else if (before_env == gab_recshp(new_env))
    // env = gab_iref(gab, gab_reccat(gab, env, new_env));

    // gab_assert(env != gab_cinvalid, "Should have a valid env");

    if (__gab_replchkres(gab, res))
      goto fin;

    for (int32_t i = 1; i < res.aresult->len; i++) {
      gab_value arg = res.aresult->data[i];

      if (i == res.aresult->len - 1) {
        gab_fvalinspect(stdout, gab_pvalintos(gab, arg, ""), -1);
      } else {
        gab_fvalinspect(stdout, gab_pvalintos(gab, arg, ""), -1);
        printf(" ");
      }
    }

    putc('\n', stdout);

  fin:

    source.len = 0;
  }
}

GAB_API union gab_value_pair gab_aexec(struct gab_triple gab,
                                       struct gab_exec_argt args) {
  gab.flags |= args.flags;

  union gab_value_pair main = gab_build(gab, (struct gab_parse_argt){
                                                 .name = args.name,
                                                 .source_len = args.source_len,
                                                 .source = args.source,
                                                 .len = args.len,
                                                 .argv = args.sargv,
                                             });

  if (main.status != gab_cvalid || gab.flags & fGAB_BUILD_CHECK)
    return main;

  return gab_arun(gab, (struct gab_run_argt){
                           .main = main.vresult,
                           .len = args.len,
                           .argv = args.argv,
                       });
}

GAB_API union gab_value_pair gab_exec(struct gab_triple gab,
                                      struct gab_exec_argt args) {
  union gab_value_pair fib = gab_aexec(gab, args);

  if (fib.status != gab_cvalid)
    return fib;

  return gab_fibawait(gab, fib.vresult);
}

GAB_INTERNAL gab_value __gab_egdodef(struct gab_triple gab, gab_value messages,
                                     uint64_t len,
                                     struct gab_def_argt args[static len]) {

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    struct gab_def_argt arg = args[i];

    gab_value specs = gab_recat(messages, arg.message);

    if (specs == gab_cundefined)
      specs = gab_record(gab, 0, 0, nullptr, nullptr);

    gab_value newspecs =
        gab_recput(gab, specs, arg.receiver, arg.specialization);

    messages = gab_recput(gab, messages, arg.message, newspecs);
  }

  return gab_gcunlock(gab), messages;
}

GAB_API bool gab_ndef(struct gab_triple gab, uint64_t len,
                      struct gab_def_argt args[static len]) {
  gab_value messages = atomic_load(&gab.eg->messages);

  for (;;) {
    if (atomic_compare_exchange_weak(&gab.eg->messages, &messages,
                                     __gab_egdodef(gab, messages, len, args)))
      return atomic_fetch_add(&gab.eg->messages_epoch, 1), true;

    gab_busywait(gab);
  }

  return false;
}

GAB_INTERNAL void __gab_egqfib(struct gab_triple gab, gab_value fib) {
  gab_iref(gab, fib);

  gab_value qres = gab_tchnput(gab, gab.eg->work_channel, fib, 1);

#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) GLOBAL QFIB $ => $\n", gab_number(gab.wkid), fib,
              qres);
#endif

  if (qres != gab_cvalid) {
    q_gab_value_dyn_push(&gab.eg->jobs[gab.wkid].waiting_queue, fib);
#if cGAB_LOG_EG
    gab_fprintf(stderr, "($) WAITING QFIB $\n", gab_number(gab.wkid), fib);
#endif
  }
}

/* Find a string in the table without creating a string object. */
GAB_INTERNAL struct gab_ostring *__gab_egstrfind(struct gab_eg *self,
                                                 uint64_t hash, uint64_t len,
                                                 const char *data) {
  if (self->strings.len == 0)
    return nullptr;

  uint64_t index = hash & (self->strings.cap - 1);

  for (;;) {
    d_status status = d_strings_istatus(&self->strings, index);
    struct gab_ostring *key = d_strings_ikey(&self->strings, index);

    switch (status) {
    case D_TOMBSTONE:
      break;
    case D_EMPTY:
      return nullptr;
    case D_FULL:
      if (key->len == len && key->hash == hash &&
          !memcmp(key->data, data, len * sizeof(*data)))
        return key;
    }

    index = (index + 1) & (self->strings.cap - 1);
  }
}

GAB_INTERNAL uint64_t __gab_shpnth(gab_value shape, uint64_t midx);
GAB_INTERNAL bool __gab_shpisn(gab_value shape, uint64_t midx);
GAB_INTERNAL bool __gab_shpisl(gab_value shape, uint64_t midx);
GAB_INTERNAL gab_value __gab_shpkey(gab_value shape, uint64_t sidx);
GAB_INTERNAL gab_value __gab_shpval(gab_value shape, uint64_t sidx);

/* Compare a shape to a linear buffer of keys. This is O(n). */
GAB_INTERNAL bool __gab_dshpcmp(gab_value shape, uint64_t stride, uint64_t len,
                                gab_value *data) {
  // TODO @cgab @opt: Iterate indices properly, no brute-forcing
  for (uint64_t midx = 0; midx < 32; midx++) {
    uint32_t sidx = __gab_shpnth(shape, midx);

    if (__gab_shpisn(shape, midx)) {
      // We have a node
      if (__gab_shpisl(shape, midx)) {
        gab_value key = __gab_shpkey(shape, sidx);
        gab_value idx = __gab_shpval(shape, sidx);

        gab_assert(idx * stride < len * stride,
                   "Somehow shape index %lu is out of range %lu by %lu",
                   idx * stride, len, stride);

        // We have key 'key' at index 'idx' in the shape.
        // Compare this to the key in the data buffer.
        if (key != data[idx * stride])
          return false;

        continue;
      }

      // Recurse into branch
      gab_value b = __gab_shpkey(shape, sidx);
      if (!__gab_dshpcmp(b, stride, len, data))
        return false;
    }
  }

  return true;
}

/* Compare a shape to another shape. This is O(nlog32(n)). */
GAB_INTERNAL bool __gab_sshpcmp(gab_value shape1, gab_value shape2) {
  // TODO @cgab @opt: Iterate indices properly, no brute-forcing
  for (uint64_t midx = 0; midx < 32; midx++) {
    uint32_t sidx = __gab_shpnth(shape1, midx);

    if (__gab_shpisn(shape1, midx)) {
      // We have a node
      if (__gab_shpisl(shape1, midx)) {
        // We have key 'key' at index 'idx1' in the shape.
        gab_value key = __gab_shpkey(shape1, sidx);
        uint64_t idx1 = __gab_shpval(shape1, sidx);

        // Use the O(log32(n)) shpfind to get the index of 'key' in shape2.
        uint64_t idx2 = gab_shpfind(shape2, key);

        // Compare the indices.
        if (idx1 != idx2)
          return false;

        continue;
      }

      // Recurse into branch
      gab_value b = __gab_shpkey(shape1, sidx);
      if (!__gab_sshpcmp(b, shape2))
        return false;
    }
  }

  return true;
}

GAB_INTERNAL struct gab_oshape *__gab_egshpfind(struct gab_eg *self,
                                                uint64_t hash, uint64_t stride,
                                                uint64_t len, gab_value *data) {
  if (self->shapes.len == 0)
    return nullptr;

  uint64_t index = hash & (self->shapes.cap - 1);

  for (;;) {
    d_status status = d_shapes_istatus(&self->shapes, index);
    struct gab_oshape *key = d_shapes_ikey(&self->shapes, index);

    switch (status) {
    case D_TOMBSTONE:
      break;
    case D_EMPTY:
      return nullptr;
    case D_FULL:
      // We have a key in this slot.
      // We have to check each key of data against
      // The keys we are looking for
      if (key->len == len && key->hash == hash) {
        if (!len)
          return key;

        // TODO @cgab @opt: This is n^2 searching the tree.
        // Better to traverse the tree once, and compare against data.
        if (__gab_dshpcmp(__gab_obj(key), stride, len, data))
          return key;

        break;
      }
    }

    index = (index + 1) & (self->shapes.cap - 1);
  }
}

GAB_INTERNAL struct gab_oshape *__gab_legshpfind(struct gab_eg *self,
                                                 uint64_t hash, uint64_t len,
                                                 gab_value shp,
                                                 gab_value last) {
  if (self->shapes.len == 0)
    return nullptr;

  uint64_t index = hash & (self->shapes.cap - 1);

  for (;;) {
    d_status status = d_shapes_istatus(&self->shapes, index);
    struct gab_oshape *key = d_shapes_ikey(&self->shapes, index);

    switch (status) {
    case D_TOMBSTONE:
      break;
    case D_EMPTY:
      return nullptr;
    case D_FULL:
      // Include last key in candidate shape length
      if (key->len == (len + 1) && key->hash == hash) {
        if (!len)
          return key;

        // Check last key
        if (gab_ushpat(__gab_obj(key), len) != last)
          goto next;

        // Compare the shape and key
        if (__gab_sshpcmp(shp, __gab_obj(key)))
          return key;

        break;
      }
    }

  next:
    index = (index + 1) & (self->shapes.cap - 1);
  }
}

GAB_API a_gab_value *gab_segmodat(struct gab_eg *eg, const char *name) {
  uint64_t hash = s_char_hash(s_char_cstr(name));

  mtx_lock(&eg->modules_mtx);

  a_gab_value *module = d_gab_modules_read(&eg->modules, hash);

  mtx_unlock(&eg->modules_mtx);

  return module;
}

GAB_API a_gab_value *gab_segmodput(struct gab_eg *eg, const char *name,
                                   a_gab_value *module) {
  uint64_t hash = s_char_hash(s_char_cstr(name));

  mtx_lock(&eg->modules_mtx);

  if (d_gab_modules_read(&eg->modules, hash) != nullptr)
    return mtx_unlock(&eg->modules_mtx), nullptr;

  d_gab_modules_insert(&eg->modules, hash, module);
  return mtx_unlock(&eg->modules_mtx), module;
}

GAB_API uint64_t gab_egkeep(struct gab_eg *gab, gab_value v) {
  return gab_negkeep(gab, 1, &v);
}

GAB_API uint64_t gab_negkeep(struct gab_eg *gab, uint64_t len,
                             gab_value values[static len]) {
  mtx_lock(&gab->modules_mtx);

  for (uint64_t i = 0; i < len; i++)
    if (gab_valiso(values[i]))
      v_gab_value_push(&gab->scratch, values[i]);

  mtx_unlock(&gab->modules_mtx);

  return len;
}

GAB_API int64_t gab_sprintf(char *dest, uint64_t n, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  int res = gab_vsprintf(dest, n, fmt, va);

  va_end(va);

  return res;
}

GAB_API int64_t gab_psprintf(char *dest, uint64_t n, const char *prefix,
                             const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  int res = gab_vpsprintf(dest, n, prefix, fmt, va);

  va_end(va);

  return res;
}

GAB_API int64_t gab_fprintf(FILE *stream, const char *fmt, ...) {
  va_list va;

  for (uint64_t i = 128;; i <<= 1) {
    va_start(va, fmt);

    char buf[i];
    if (gab_vsprintf(buf, i, fmt, va) >= 0)
      return va_end(va), fputs(buf, stream);

    va_end(va);
  }

  return -1;
}

GAB_API int64_t gab_npsprintf(char *dest, uint64_t n, const char *prefix,
                              const char *fmt, uint64_t argc, gab_value *argv) {
  const char *c = fmt;
  char *cursor = dest;
  uint64_t remaining = n;
  uint64_t i = 0;

  while (*c != '\0') {
    switch (*c) {
    case '$': {
      if (i >= argc)
        return -1;

      gab_value arg = argv[i++];

      int res = gab_psvalinspect(&cursor, &remaining, arg, prefix, 1);

      if (res < 0)
        return res;

      break;
    }
    default:
      if (remaining == 0)
        return -1;

      *cursor++ = *c;
      remaining -= 1;
    }

    c++;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  if (i != argc)
    return -1;

  return n - remaining;
}

/*
 *
 * GAB UTF8-DECODING, adapted from:
 *
 * Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
 * See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
 */

#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

// clang-format off
static const uint8_t utf8d[] = {
  // The first part of the table maps bytes to character classes that
  // to reduce the size of the transition table and create bitmasks.
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
   8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

  // The second part is a transition table that maps a combination
  // of a state of the automaton and a character class to a state.
   0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
  12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
  12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
  12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
  12,36,12,12,12,12,12,12,12,12,12,12, 
};
// clang-format on

GAB_INTERNAL uint32_t __gab_utf8_decode(uint32_t *state, uint32_t *codep,
                                        uint32_t byte) {
  uint32_t type = utf8d[byte];

  *codep = (*state != UTF8_ACCEPT) ? (byte & 0x3fu) | (*codep << 6)
                                   : (0xff >> type) & (byte);

  *state = utf8d[256 + *state + type];
  return *state;
}

GAB_INTERNAL int __gab_utf8_codepoints(uint8_t *s, uint64_t *count) {
  uint32_t codepoint;
  uint32_t state = UTF8_ACCEPT;

  for (*count = 0; *s; ++s)
    if (!__gab_utf8_decode(&state, &codepoint, *s))
      *count += 1;

  return state != UTF8_ACCEPT;
}

GAB_INTERNAL int __gab_utf8_next(uint8_t *s) {
  uint32_t codepoint;
  uint32_t state = UTF8_ACCEPT;

  for (uint32_t width = 1; *s; ++s, ++width)
    if (!__gab_utf8_decode(&state, &codepoint, *s))
      return width;

  return 0;
}

GAB_API int64_t gab_nsprintf(char *dest, uint64_t n, const char *fmt,
                             uint64_t argc, gab_value *argv) {
  const char *c = fmt;
  char *cursor = dest;
  uint64_t remaining = n;
  uint64_t i = 0;

  while (true) {
    int64_t width = __gab_utf8_next((uint8_t *)c);

    // Null char encountered, we are done.
    if (width == 0)
      break;

    // Success, encountered a full glyph
    if (width == 1 && *c == '$') {
      if (i >= argc)
        return -1;

      gab_value arg = argv[i++];

      int res = gab_svalinspect(&cursor, &remaining, arg, 1);

      if (res < 0)
        return -1;

    } else {
      if (remaining < width)
        return -1;

      memcpy(cursor, c, width);

      // Advance cursor and remaining by the number of bytes written.
      cursor += width;
      remaining -= width;
    }

    // Advance the source string by number of bytes read.
    c += width;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  if (i != argc)
    return -1;

  return n - remaining;
}

GAB_API int64_t gab_vpsprintf(char *dest, uint64_t n, const char *prefix,
                              const char *fmt, va_list varargs) {
  const char *c = fmt;
  char *cursor = dest;
  uint64_t remaining = n;

  while (*c != '\0') {
    switch (*c) {
    case '@': {
      gab_value arg = va_arg(varargs, gab_value);

      int res = gab_psvalinspect(&cursor, &remaining, arg, "", 1);

      if (res < 0)
        return -1;

      break;
    }
    case '$': {
      gab_value arg = va_arg(varargs, gab_value);

      int res = gab_psvalinspect(&cursor, &remaining, arg, prefix, 1);

      if (res < 0)
        return -1;

      break;
    }
    default:
      if (remaining == 0)
        return -1;

      *cursor++ = *c;
      remaining -= 1;
    }
    c++;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  return n - remaining;
}

GAB_API int64_t gab_vsprintf(char *dest, uint64_t n, const char *fmt,
                             va_list varargs) {
  const char *c = fmt;
  char *cursor = dest;
  uint64_t remaining = n;

  // This logic has a bug with codepoints. Jenkies
  while (*c != '\0') {

    uint64_t width = __gab_utf8_next((uint8_t *)c);

    if (width == 0)
      break;

    if (width == 1 && *c == '$') {
      gab_value arg = va_arg(varargs, gab_value);

      int res = gab_svalinspect(&cursor, &remaining, arg, 1);

      if (res < 0)
        return -1;

    } else {
      if (remaining < width)
        return -1;

      memcpy(cursor, c, width);

      // Advance cursor and remaining by the number of bytes written.
      cursor += width;
      remaining -= width;
    }

    // Advance the source string by number of bytes read.
    c += width;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  return n - remaining;
}

static const char *gab_status_names[] = {
#define STATUS(name, message) #name,
#include "status_code.h"
#undef STATUS
};

static const char *gab_token_names[] = {
#define TOKEN(message) #message,
#include "token.h"
#undef TOKEN
};

static const char *gab_status_messages[] = {
#define STATUS(name, message) message,
#include "status_code.h"
#undef STATUS
};

/* pretty-print an error into a string buffer */
GAB_INTERNAL int64_t __gab_spprinterr(struct gab_triple gab, char **buf,
                                      uint64_t *len, struct errdetails *args,
                                      const char *hint) {
  struct gab_src *src =
      d_gab_src_read(&gab.eg->sources, gab_string(gab, args->src_name));

  const char *tok_name =
      src ? gab_token_names[v_gab_token_val_at(&src->tokens, args->token)]
          : "C";

  const char *src_name = src ? gab_strdata(&src->name) : "C";

  // Include gab@<wkid> here isn't useful really anymore. Can be removed.
  // Maybe it is better to show the fiber?
  if (__gab_snprintf_through(buf, len,
                             "[" GAB_GREEN "gab@%i" GAB_RESET
                             "] panicked in " GAB_GREEN "%s" GAB_RESET
                             " near " GAB_YELLOW "%s.\n\n" GAB_RESET,
                             args->wkid, src_name, tok_name) < 0)
    return -1;

  if (args->status)
    if (__gab_snprintf_through(
            buf, len,
            GAB_RED "E%03i" GAB_RESET "|" GAB_RED " %s" GAB_RESET "\n",
            args->status, gab_status_messages[args->status]) < 0)
      return -1;

  if (src) {
    s_char tok_src = v_s_char_val_at(&src->token_srcs, args->token);

    uint64_t line_num = v_uint64_t_val_at(&src->token_lines, args->token);

    s_char line_src = v_s_char_val_at(&src->lines, line_num - 1);

    // Skip preceding whitespace for this line.
    uint64_t whitespace_skipped = 0;
    while (line_src.data[whitespace_skipped] == ' ' ||
           line_src.data[whitespace_skipped] == '\t') {
      whitespace_skipped++;
      gab_assert(
          line_src.len > whitespace_skipped,
          "We should still have line to render after skipping whitespace");
    }

    line_src.data += whitespace_skipped;
    line_src.len -= whitespace_skipped;

    if (line_num > 1) {
      uint64_t prev_line_num = line_num - 2;
      s_char prev_line_src = v_s_char_val_at(&src->lines, prev_line_num);
      if (prev_line_src.len > whitespace_skipped)
        if (__gab_snprintf_through(
                buf, len, "\n      %.*s",
                (int)(prev_line_src.len - whitespace_skipped),
                prev_line_src.data + whitespace_skipped) < 0) {
          return -1;
        }
    }

    int leftpad = (int)(tok_src.data - line_src.data);
    // int tokpad = (int)tok_src.len - 2;
    int rhs_width = (int)tok_src.len - 1;

    const char *lhs = "^";
    const char *rhs = "^^^^^^^^^^^^^^^^^^^^^^";

    if (__gab_snprintf_through(buf, len,
                               "\n" GAB_RED "%.4" PRIu64 "" GAB_RESET "| %.*s"
                               "\n      " GAB_YELLOW "%*s%s%.*s" GAB_RESET "",
                               line_num, (int)line_src.len, line_src.data,
                               leftpad, "", lhs, rhs_width, rhs) < 0) {
      return -1;
    }

    if (line_num < src->lines.len) {
      uint64_t next_line_num = line_num;
      s_char next_line_src = v_s_char_val_at(&src->lines, next_line_num);

      if (next_line_src.len > whitespace_skipped)
        if (__gab_snprintf_through(
                buf, len, "\n      %.*s",
                (int)(next_line_src.len - whitespace_skipped),
                next_line_src.data + whitespace_skipped) < 0) {
          return -1;
        }
    }
  }

  if (hint > 0)
    if (__gab_snprintf_through(buf, len, "\n\n%s", hint) < 0)
      return -1;

  return __gab_snprintf_through(buf, len, "\n");
};

/* structurally print an error into a string buffer */
GAB_INTERNAL int64_t __gab_ssprinterr(struct gab_triple gab, char **buf,
                                      uint64_t *len, struct errdetails *d,
                                      const char *hint) {
  __gab_snprintf_through(buf, len, "%s:%s:%s:%s", gab_status_names[d->status],
                         d->src_name, d->tok_name, d->msg_name);

  __gab_snprintf_through(
      buf, len, ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 "",
      d->row, d->col_begin, d->col_end, d->byte_begin, d->byte_end);

  return __gab_snprintf_through(buf, len, "\n");
}

GAB_API gab_value gab_vspanicf(struct gab_triple gab, va_list va,
                               struct gab_err_argt args) {
  struct errdetails err = {
      .tok_name =
          args.src && args.src->source->len
              ? gab_token_names[v_gab_token_val_at(&args.src->tokens, args.tok)]
              : "C",
  };

  if (args.src && args.src->source->len) {
    err.row = v_uint64_t_val_at(&args.src->token_lines, args.tok);

    s_char line_src = v_s_char_val_at(&args.src->lines, err.row - 1);
    s_char tok_src = v_s_char_val_at(&args.src->token_srcs, args.tok);

    gab_assert(tok_src.data >= line_src.data,
               "The token should be after the beginning of the line");

    err.col_begin = tok_src.data - line_src.data;
    err.col_end = tok_src.data + tok_src.len - line_src.data;

    err.byte_begin = tok_src.data - args.src->source->data;
    err.byte_end = tok_src.data + tok_src.len - args.src->source->data;
  }

  err.src_name = args.src ? gab_strdata(&args.src->name) : "C";

  err.status = args.status;

  gab_gclock(gab);

  char hint[cGAB_ERR_SPRINTF_BUF_MAX] = {0};
  if (args.note_fmt) {
    if (gab_vpsprintf(hint, sizeof(hint), "   | ", args.note_fmt, va) < 0)
      ;
  }

  // Signaling here causes the record to have no keys.
  gab_value vstatus = gab_message(gab, "status");
  if (vstatus == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vsrc = gab_message(gab, "src");
  if (vsrc == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vtok_offset = gab_message(gab, "tok\\offset");
  if (vtok_offset == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vtok_t = gab_message(gab, "tok\\t");
  if (vtok_t == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vhint = gab_message(gab, "hint");
  if (vhint == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vrow = gab_message(gab, "row");
  if (vrow == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vcol_begin = gab_message(gab, "col\\begin");
  if (vcol_begin == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vcol_end = gab_message(gab, "col\\end");
  if (vcol_end == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vbyte_begin = gab_message(gab, "byte\\begin");
  if (vbyte_begin == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vbyte_end = gab_message(gab, "byte\\end");
  if (vbyte_end == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value vthrd = gab_message(gab, "thread");
  if (vthrd == gab_cinvalid)
    return gab_gcunlock(gab), gab_cinvalid;

  gab_value rec = gab_recordof(
      gab, vstatus, gab_string(gab, gab_status_names[err.status]), vsrc,
      gab_string(gab, err.src_name), vtok_offset, gab_number(args.tok), vtok_t,
      gab_string(gab, err.tok_name), vhint, gab_string(gab, hint), vrow,
      gab_number(err.row), vcol_begin, gab_number(err.col_begin), vcol_end,
      gab_number(err.col_end), vbyte_begin, gab_number(err.byte_begin),
      vbyte_end, gab_number(err.byte_end), vthrd, gab_number(args.wkid), );

  gab_assert(gab_reclen(rec) == 11,
             "Error record shall be constructed correctly");

  gab_gcunlock(gab);

  return rec;
}

GAB_INTERNAL gab_value __gab_errtos(struct gab_triple gab, gab_value err) {
  gab_value token_type = gab_mrecat(gab, err, "tok\\t");
  if (token_type == gab_cinvalid)
    return gab_cinvalid;

  gab_value srcname = gab_mrecat(gab, err, "src");
  if (srcname == gab_cinvalid)
    return gab_cinvalid;

  gab_value status = gab_mrecat(gab, err, "status");
  if (status == gab_cinvalid)
    return gab_cinvalid;

  gab_value hint = gab_mrecat(gab, err, "hint");
  if (hint == gab_cinvalid)
    return gab_cinvalid;

  gab_value vtoken = gab_mrecat(gab, err, "tok\\offset");
  if (vtoken == gab_cinvalid)
    return gab_cinvalid;

  gab_value vrow = gab_mrecat(gab, err, "row");
  if (vrow == gab_cinvalid)
    return gab_cinvalid;

  gab_value vcol_begin = gab_mrecat(gab, err, "col\\begin");
  if (vcol_begin == gab_cinvalid)
    return gab_cinvalid;

  gab_value vcol_end = gab_mrecat(gab, err, "col\\end");
  if (vcol_end == gab_cinvalid)
    return gab_cinvalid;

  gab_value vbyte_begin = gab_mrecat(gab, err, "byte\\begin");
  if (vbyte_begin == gab_cinvalid)
    return gab_cinvalid;

  gab_value vbyte_end = gab_mrecat(gab, err, "byte\\end");
  if (vbyte_end == gab_cinvalid)
    return gab_cinvalid;

  gab_value vwkid = gab_mrecat(gab, err, "thread");
  if (vwkid == gab_cinvalid)
    return gab_cinvalid;

  gab_assert(gab_valkind(vtoken) == kGAB_NUMBER,
             "tok\\offset shall be a number");
  uint64_t token = gab_valtou(vtoken);

  gab_assert(gab_valkind(vrow) == kGAB_NUMBER, "row shall be a number");
  uint64_t row = gab_valtou(vrow);

  gab_assert(gab_valkind(vcol_begin) == kGAB_NUMBER,
             "col\\begin shall be a number");
  uint64_t col_begin = gab_valtou(vcol_begin);

  gab_assert(gab_valkind(vcol_end) == kGAB_NUMBER,
             "col\\end shall be a number");
  uint64_t col_end = gab_valtou(vcol_end);

  gab_assert(gab_valkind(vbyte_begin) == kGAB_NUMBER,
             "byte\\begin shall be a number");
  uint64_t byte_begin = gab_valtou(vbyte_begin);

  gab_assert(gab_valkind(vbyte_end) == kGAB_NUMBER,
             "byte\\end shall be a number");
  uint64_t byte_end = gab_valtou(vbyte_end);

  gab_assert(gab_valkind(vwkid) == kGAB_NUMBER, "wkid shall be a number");
  uint64_t wkid = gab_valtou(vwkid);

  enum gab_status status_enum = GAB_OK;
  const char *statusname = gab_strdata(&status);
  for (int i = 0; i < LEN_CARRAY(gab_status_names); i++) {
    if (!strcmp(statusname, gab_status_names[i])) {
      status_enum = i;
      break;
    }
  }

  gab_assert(status_enum != GAB_OK,
             "We should not have an ok result in the error path");

  struct errdetails e = {
      .token = token,
      .src_name = gab_strdata(&srcname),
      .status = status_enum,
      .tok_name = gab_strdata(&token_type),
      .byte_begin = byte_begin,
      .byte_end = byte_end,
      .col_begin = col_begin,
      .col_end = col_end,
      .row = row,
      .wkid = wkid,
  };

  const char *cstrhint = gab_strdata(&hint);

  int64_t (*print_fn)(struct gab_triple, char **, uint64_t *,
                      struct errdetails *, const char *) =
      gab.flags & fGAB_ERR_STRUCTURED ? __gab_ssprinterr : __gab_spprinterr;

  for (uint64_t i = 128;; i <<= 1) {
    char buf[i];
    uint64_t n = i;
    char *cursor = buf;
    if (print_fn(gab, &cursor, &n, &e, cstrhint) >= 0) {
      gab_value newestr = gab_string(gab, buf);
      return newestr;
    }
  }

  return gab_nil;
}

GAB_API const char *gab_errtocs(struct gab_triple gab, gab_value err) {
  gab_precondition(gab_valkind(err) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(err));

  if (gab_valkind(err) != kGAB_RECORD)
    return nullptr;

  if (!gab_recisl(err)) {
    gab_value str;

    do {
      str = __gab_errtos(gab, err);

      switch (gab_yield(gab)) {
      case sGAB_TERM:
        gab_sigpropagate(gab);
        break;
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        continue;
      }
    } while (str == gab_cinvalid);

    gab_assert(str != gab_nil, "Shall not fail to create str");
    gab_assert(gab_strlen(str) > 5, "Shall not return pointer to shortstr");
    // Only works because this will never be a shortstr.
    // This is *not* an appropriate way to use
    // gab_strdata however, because if str *is* a shortstr,
    // the pointer would be pointing to our local variable str.
    // And then we would be returning a pointer to a local.
    return gab_strdata(&str);
  }

  int len = gab_reclen(err);

  if (!len)
    return nullptr;

  gab_value total_str = gab_cinvalid;
  do {
    total_str = gab_string(gab, "");
  } while (total_str == gab_cinvalid);

  gab_assert(total_str != gab_cinvalid, "Shall not fail to create str");

  if (total_str == gab_cinvalid)
    return nullptr;

  for (int i = len - 1; i >= 0; --i) {
    gab_value next_err = gab_uvrecat(err, i);
    gab_assert(next_err != gab_nil, "Next error shall not be nil");
    gab_assert(gab_reclen(next_err) > 1, "Next error shall have values");

    gab_value next_str;

    do {
      next_str = __gab_errtos(gab, next_err);

      switch (gab_yield(gab)) {
      case sGAB_TERM:
        gab_sigpropagate(gab);
        break;
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        continue;
      }
    } while (next_str == gab_cinvalid);

    gab_assert(next_str != gab_nil, "Shall have valid next str");

    do {
      total_str = gab_strcat(gab, total_str, next_str);

      switch (gab_yield(gab)) {
      case sGAB_TERM:
        gab_sigpropagate(gab);
        break;
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        continue;
      }
    } while (total_str == gab_cinvalid);
    // if (total_str == gab_cinvalid)
    //   return nullptr;
  }

  gab_assert(gab_strlen(total_str) > 5,
             "Returned Str Length shall be greater than 5, so as not to "
             "invalidate pointer.");
  return gab_strdata(&total_str);
}

#define MODULE_SYMBOL "gab_lib"

GAB_INTERNAL a_char *__gab_resmatch(const char **roots,
                                    const struct gab_resource *res,
                                    const char *package, const char *module,
                                    const char **root) {
  for (int i = 0; roots[i] != nullptr; i++) {
    uint64_t r_len = strlen(roots[i]);
    uint64_t p_len = strlen(res->prefix);
    uint64_t s_len = strlen(res->suffix);
    uint64_t pkg_len = strlen(package);

    uint64_t mod_len = module ? strlen(module) : 0;

    /*
     * What is the best/most correct way to combine the package and module here?
     */
    uint64_t total_len = r_len + p_len + pkg_len + 1 + mod_len + s_len + 1;

    char buffer[total_len];

    memcpy(buffer, roots[i], r_len);

    memcpy(buffer + r_len, package, pkg_len);
    buffer[r_len + pkg_len++] = '/';

    memcpy(buffer + r_len + pkg_len, res->prefix, p_len);

    if (module) {
      memcpy(buffer + r_len + pkg_len + p_len, module, mod_len);
    }

    memcpy(buffer + r_len + pkg_len + p_len + mod_len, res->suffix, s_len + 1);

    gab_precondition(res->exister != nullptr,
                     "Resource shall have valid exister fn pointer");

    if (res->exister(buffer)) {

      if (root)
        *root = roots[i];

      return a_char_create(buffer, total_len);
    }
  }

  return nullptr;
}

/*
 *
 * Resolve MODULE within the given PACKAGE, starting at ROOTS and using
 * RESOURCES.
 *
 * For each of the roots, check if PACKAGE exists.
 *  - A package is folder or file, which exists *in* the root.
 *  - If MODULE is requested, try to resolve MODULE within PACKAGE via
 * resources.
 *  - If MODULE isn't requested, just try to resolve PACKAGE.
 *
 */

GAB_API struct gab_module_res gab_mresolve(const char **roots,
                                           const struct gab_resource *resources,
                                           const char *package,
                                           const char *module) {

  char *colon = strchr(package, ':');
  if (colon) {
    // In this case, the package name implies a module.
    gab_precondition(
        !module,
        "A package implied a module, but module was already specified.");

    module = colon + 1;

    *colon = '\0';
  }

  for (int i = 0; resources[i].prefix != nullptr; i++) {
    const struct gab_resource *res = resources + i;
    const char *root = nullptr;

    a_char *module_path = __gab_resmatch(roots, res, package, module, &root);

    if (module_path) {
      return (struct gab_module_res){
          .path = module_path,
          .resource = res,
          .root_path = root,
          // Skip the root. This should resolve to the full package + module
          // path.
          .package_path = module_path->data + strlen(root),
          // Skip the root and package. This should resolve to the module path,
          // relative to the package.
          .module_path = module_path->data + strlen(root) + strlen(package) + 1,
      };
    }
  }

  return (struct gab_module_res){0};
}

GAB_API struct gab_module_res
gab_resolve(struct gab_triple gab, const char *package, const char *module) {
  return gab_mresolve(gab.eg->resroots, gab.eg->res, package, module);
}

GAB_API union gab_value_pair gab_use(struct gab_triple gab,
                                     struct gab_use_argt args) {
  gab.flags |= args.flags;

  const char *package = args.spackage_name;
  if (args.vpackage_name) {
    package = gab_strdata(&args.vpackage_name);
  }

  const char *module = args.smodule_name;
  if (args.vmodule_name) {
    module = gab_strdata(&args.vmodule_name);
  }

  if (gab_valkind(args.env) == kGAB_RECORD) {
    args.len = gab_reclen(args.env);
  }

  gab_precondition(args.len != 0, "Args must not be zero.");

  const char *env_sargv[args.len];
  gab_value env_vsargv[args.len];
  gab_value env_vargv[args.len];

  if (gab_valkind(args.env) == kGAB_RECORD) {
    for (uint64_t i = 0; i < args.len; i++) {
      env_vsargv[i] = gab_ukrecat(args.env, i);

      gab_precondition(gab_valkind(env_vsargv[i]) == kGAB_BINARY,
                       "Invalid kind %d. Expected a gab\\binary",
                       gab_valkind(env_vsargv[i]));

      env_sargv[i] = gab_strdata(env_vsargv + i);
      env_vargv[i] = gab_uvrecat(args.env, i);
    }

    args.sargv = env_sargv;
    args.argv = env_vargv;
  }

  struct gab_module_res mod = gab_resolve(gab, package, module);

  if (mod.resource) {
    if (!(gab.flags & fGAB_USE_RELOAD)) {
      a_gab_value *cached = gab_segmodat(gab.eg, mod.path->data);

      if (cached != nullptr) {
        /* Skip the first argument, which is the module's data */

        return (union gab_value_pair){
            .status = gab_cvalid,
            .aresult = cached,
        };
      }
    }

    gab_precondition(mod.resource->loader != nullptr,
                     "Expected valid resource loader fn pointer");

    union gab_value_pair result = mod.resource->loader(
        gab, mod.path->data, args.len, args.sargv, args.argv);

    if (result.status != gab_cvalid)
      return result;

    if (result.aresult->data[0] != gab_ok)
      return result;

    gab_segmodput(gab.eg, mod.path->data, result.aresult);

    return a_char_destroy(mod.path), result;
  }

  if (module)
    return gab_panicf(gab, "Module @:@ could not be found",
                      gab_string(gab, package), gab_string(gab, module));
  else
    return gab_panicf(gab, "Package @ could not be found",
                      gab_string(gab, package));
}

GAB_API union gab_value_pair gab_run(struct gab_triple gab,
                                     struct gab_run_argt args) {
  union gab_value_pair fb = gab_arun(gab, args);

  if (fb.status != gab_cvalid)
    return fb;

  return gab_fibawait(gab, fb.vresult);
}

GAB_API union gab_value_pair gab_arun(struct gab_triple gab,
                                      struct gab_run_argt args) {
  return gab_tarun(gab, -1, args);
}

GAB_API union gab_value_pair gab_tarun(struct gab_triple gab, uint64_t tries,
                                       struct gab_run_argt args) {
  gab.flags |= args.flags;

  if (gab.flags & fGAB_BUILD_CHECK)
    return (union gab_value_pair){.status = gab_cinvalid};

  gab_value fb = gab_fiber(gab, (struct gab_fiber_argt){
                                    .message = gab_message(gab, mGAB_CALL),
                                    .receiver = args.main,
                                    .flags = gab.flags,
                                    .argv = args.argv,
                                    .argc = args.len,
                                });

  if (fb == gab_cinvalid)
    return (union gab_value_pair){{gab_cinvalid}};

  gab_iref(gab, fb);
  gab_egkeep(gab.eg, fb);

  // TODO @runtime @perf: Push to local queue instead of always deferring
  // globally.

  // If we're *in* a valid worker we can push to the local queue.
  //   if (gab.wkid) {
  //     q_gab_value *q = &gab.eg->jobs[gab.wkid].queue;
  // #if cGAB_LOG_EG
  //     fprintf(stdout, "(%i) localqueue ", gab.wkid);
  //     gab_fprintf(stdout, "$\n", fb);
  // #endif
  //     if (q_gab_value_push(q, fb))
  //       return (union gab_value_pair){{gab_cvalid, fb}};
  //   }

#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) chnput $\n", gab_number(gab.wkid), fb);
#endif

  // Somehow check if the put will block, and create a job in that case.
  // Should check to see if the channel has takers waiting already.

  // TODO @cgab @runtime: When spawning a worker thread, try to donate all
  // queued fibers which have *never* been run. This is safe with our GC
  // strategy Fibers should not change type *back* to kGAB_FIBER after yielding.
  // They should remain kGAB_FIBERRUNNING, so that we know if a fiber has *ever*
  // been run on a thread. In order for our GC to be sound, VM Stacks *cannot*
  // migrate from thread to thread (After they may have been seen by the gc (ie,
  // run). We should also *skip* incrementing/decrementing stacks for Fibers
  // which have never been run in GC.

  if (!__gab_jbspawn(gab, fb))
    if (gab_tchnput(gab, gab.eg->work_channel, fb, tries) == gab_ctimeout)
      return (union gab_value_pair){{gab_ctimeout, fb}};

  return (union gab_value_pair){{gab_cvalid, fb}};
}

GAB_API union gab_value_pair gab_send(struct gab_triple gab,
                                      struct gab_send_argt args) {
  union gab_value_pair fb = gab_asend(gab, args);

  if (fb.status != gab_cvalid)
    return fb;

  union gab_value_pair res = gab_fibawait(gab, fb.vresult);

  if (res.status != gab_cvalid)
    return res;

  gab_dref(gab, fb.vresult);

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = res.aresult,
  };
};

GAB_API union gab_value_pair gab_asend(struct gab_triple gab,
                                       struct gab_send_argt args) {
  gab.flags |= args.flags;

  gab_value fb = gab_fiber(gab, (struct gab_fiber_argt){
                                    .message = args.message,
                                    .receiver = args.receiver,
                                    .argv = args.argv,
                                    .argc = args.len,
                                    .flags = gab.flags,
                                });

  if (fb == gab_cinvalid)
    return (union gab_value_pair){{gab_cinvalid}};

  gab_iref(gab, fb);
  gab_egkeep(gab.eg, fb);

  // TODO @cgab @bug: These chnputs block, which is problematic.
  // I should really maybe have a queue for this.
  // These potentially block callers annoyingly long
  if (args.pinmask) {
    int32_t wkid = ctzl(args.pinmask) + 1;
    if (wkid > gab.eg->len)
      return (union gab_value_pair){{gab_cinvalid}};

    // TODO @cgab @bug: Properly test & try all allowed workers in the pinmask.
    if (!__gab_jbisalive(gab, wkid))
      return (union gab_value_pair){{gab_cinvalid}};

    if (gab.wkid == wkid)
      q_gab_value_dyn_push(&gab.eg->jobs[wkid].waiting_queue, fb);
    else
      gab_chnput(gab, gab.eg->jobs[wkid].work_channel, fb);

  } else if (!__gab_jbspawn(gab, fb)) {
    __gab_egqfib(gab, fb);
  }

  return (union gab_value_pair){{gab_cvalid, fb}};
};

GAB_API bool gab_sigterm(struct gab_triple gab) {
  while (!gab_signal(gab, sGAB_TERM, 1))
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return true;
    default:
      break;
    };

  return true;
}

GAB_API bool gab_asigcoll(struct gab_triple gab) {
  return gab_signal(gab, sGAB_COLL, 1);
}

GAB_API bool gab_sigcoll(struct gab_triple gab) {
  while (!gab_asigcoll(gab))
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      return true;
    case sGAB_TERM:
      return false;
    default:
      break;
    };

  return true;
}

GAB_API struct gab_impl_rest gab_impl(struct gab_triple gab, gab_value message,
                                      gab_value receiver) {
  gab_value messages = gab_thisfibmsg(gab);
  gab_value specs = gab_recat(messages, message);

  // There are no specs for this message.
  // *maybe* this is a rec and property situation.
  // So jump straight there.
  if (specs == gab_cundefined)
    goto property;

  gab_value spec = gab_cundefined;
  gab_value type = receiver;

  /* Check if the receiver has a supertype, and if that supertype implments
   * the message. ie <gab.shape 0 1>*/
  if (gab_valhast(receiver)) {
    type = gab_valtype(gab, receiver);
    spec = gab_recat(specs, type);
    if (spec != gab_cundefined)
      return (struct gab_impl_rest){
          .messages = messages,
          .type = type,
          .as.spec = spec,
          kGAB_IMPL_TYPE,
      };
  }

  /* Check for the kind of the receiver. ie 'gab\record' */
  type = gab_type(gab, gab_valkind(receiver));
  spec = gab_recat(specs, type);
  if (spec != gab_cundefined)
    return (struct gab_impl_rest){
        .messages = messages,
        .type = type,
        .as.spec = spec,
        kGAB_IMPL_KIND,
    };

  /* Check for a default, generic implementation */
  /* Previously, this had a higher priority than
   * record properties - I don't remember why I made that change.
   *
   * Ahh, I remember the issue. The `Messages.specializations` record
   * is impossible to do anything with, because it is a record with a key
   * for every message in the system.
   */
  type = gab_cundefined;
  spec = gab_recat(specs, type);
  if (spec != gab_cundefined)
    return (struct gab_impl_rest){
        .messages = messages,
        .type = type,
        .as.spec = spec,
        kGAB_IMPL_GENERAL,
    };

  /* Check if the receiver is a record and has a matching property */
property:
  if (gab_valkind(receiver) == kGAB_RECORD) {
    type = gab_recshp(receiver);
    if (gab_rechas(receiver, message))
      return (struct gab_impl_rest){
          .messages = messages,
          .type = type,
          .as.offset = gab_recfind(receiver, message),
          kGAB_IMPL_PROPERTY,
      };
  }

  return (struct gab_impl_rest){.messages = messages, .status = kGAB_IMPL_NONE};
}

GAB_API gab_value gab_type(struct gab_triple gab, enum gab_kind k) {
  gab_precondition(k < kGAB_NKINDS, "Invalid kind %d", k);
  return gab.eg->types[k];
}

GAB_API struct gab_gc *gab_gc(struct gab_triple gab) { return &gab.eg->gc; }

GAB_API gab_value gab_thisfiber(struct gab_triple gab) {
  return q_gab_value_peek(&gab.eg->jobs[gab.wkid].working_queue);
}

GAB_API gab_value gab_thisfibmsg(struct gab_triple gab) {
  return atomic_load(&gab.eg->messages);
  /*gab_value fiber = gab_thisfiber(gab);*/
  /**/
  /*if (fiber == gab_cinvalid)*/
  /*  return gab_atmat    (gab, gab.eg->messages);*/
  /**/
  /*struct gab_ofiber *f =
        GAB_VAL_TO_FIBER(fiber);*/
  /*return gab_atmat(gab, f->messages);*/
}

GAB_API inline bool gab_sigwaiting(struct gab_triple gab) {
  struct gab_sig sig = atomic_load_explicit(&gab.eg->sig, memory_order_acquire);
  return sig.schedule == gab.wkid;
}

GAB_API inline bool gab_signaling(struct gab_triple gab) {
  /*printf("SCHEDULE: %i, SIGNALING: %d\n", gab.eg->sig.schedule,
   * gab.eg->sig.schedule >= 0);*/
  struct gab_sig sig = atomic_load_explicit(&gab.eg->sig, memory_order_acquire);
  return sig.signal;
}

GAB_API inline bool gab_signext(struct gab_triple gab, int wkid) {
  for (;;) {
    gab_busywait(gab);

    struct gab_sig sig = atomic_load(&gab.eg->sig);

    if (!sig.mask)
      return true;

#if cGAB_LOG_EG
    fprintf(stderr, "(%i) TRY NEXT %i: against %b\n", gab.wkid, wkid, sig.mask);
#endif

    gab_precondition(sig.signal > 0, "Should have a signal to propagate");

    // Wrap around the number of jobs. Since
    // The 0th job is the GC job, we will wrap around
    // and begin the gc last.
    if (wkid >= gab.eg->len) {
      struct gab_sig next = {
          .mask = sig.mask,
          .schedule = 0,
          .signal = sig.signal,
      };

      gab_assert(next.signal != sGAB_IGN, "Should have a signal to propagate");

      // cnd_signal(&gab.eg->gc_cnd);

      if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
        return true;
      else
        continue;
    }

    // If the worker we're signalling for isn't alive,
    // try to skip it.
    if (!(sig.mask & (1 << wkid))) {
      uint32_t shifted = sig.mask >> wkid;
      uint32_t nxt_open = shifted ? __builtin_ctzl(shifted) : 0;
      uint32_t n = nxt_open ? wkid + nxt_open : 0;

      gab_assert(sig.mask & (1 << n),
                 "The working we're signalling for should be alive.");

      struct gab_sig next = {
          .mask = sig.mask,
          .schedule = n,
          .signal = sig.signal,
      };

      uint32_t last_job = n ? n : gab.eg->len;

#if cGAB_LOG_GC
      fprintf(stderr, "(%i) (%b) SKIPPING %u to %u\n", gab.wkid, sig.mask, wkid,
              last_job);
#endif

      // Ugly way of incrementing epoch for not-alive jobs.
      if (sig.signal == sGAB_COLL)
        for (uint32_t i = wkid; i < last_job; i++) {
          gab_assert(!(sig.mask & (1 << i)),
                     "Shall not skip a worker which is alive.");
#if cGAB_LOG_GC
          fprintf(stderr, "(%i) EPOCHINC via SKIP\n", i);
#endif
          gab.eg->jobs[i].epoch++;
        }

      gab_assert(next.signal != sGAB_IGN, "Next signal should not be ignore");

      if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
        return true;
      else
        continue;
    }

    if (sig.schedule < (int8_t)wkid) {
      struct gab_sig next = {
          .mask = sig.mask,
          .schedule = wkid,
          .signal = sig.signal,
      };

      gab_assert(next.signal != sGAB_IGN, "Next signal should not be ignore");
      if (atomic_compare_exchange_weak(&gab.eg->sig, &sig, next))
        return true;
      else
        continue;
    }

    gab_assert(sig.signal != sGAB_IGN, "Signal should not be ignore");
    if (sig.schedule == wkid)
      return true;
    else
      continue;
  }
}

GAB_API inline bool gab_sigclear(struct gab_triple gab) {
  for (;;) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    struct gab_sig exp = (struct gab_sig){sig.mask, 0, sig.signal};
    struct gab_sig next = (struct gab_sig){sig.mask, -1, sGAB_IGN};
    if (atomic_compare_exchange_weak(&gab.eg->sig, &exp, next)) {
#if cGAB_LOG_EG
      fprintf(stderr, "(%i) CLEAR %i\n", gab.wkid, sig.signal);
#endif
      return true;
    }
  }
}

GAB_API inline bool gab_signal(struct gab_triple gab, enum gab_signal s,
                               int wkid) {
  gab_precondition(wkid < gab.eg->len, "Wkid should be less than %i, got %i",
                   gab.eg->len, wkid);

  gab_precondition(wkid > 0, "Wkid should be greater than 0");

  for (;;) {
    struct gab_sig sig = atomic_load(&gab.eg->sig);
    struct gab_sig none = {sig.mask, -1, sGAB_IGN};

    if (sig.signal == s)
      return true;

    if (sig.schedule == gab.wkid) {
      switch (sig.signal) {
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      case sGAB_TERM:
        return false;
      default:
        break;
      }
    }

    if (atomic_compare_exchange_weak(&gab.eg->sig, &none,
                                     ((struct gab_sig){sig.mask, -2, s}))) {
#if cGAB_LOG_EG
      fprintf(stderr, "(%i) SIGNAL %i TO %b\n", gab.wkid, s, sig.mask);
#endif

      mtx_lock(&gab.eg->gc_mtx);
      cnd_signal(&gab.eg->gc_cnd);
      mtx_unlock(&gab.eg->gc_mtx);

      for (;;) {
        struct gab_sig sig = atomic_load(&gab.eg->sig);

        /* acknowledgment received */
        if (sig.schedule == -1)
          break;
      }
      return gab_signext(gab, wkid);
    }
  }
};

/* ----------------------------------------
 *
 *    GAB OBJECTS
 *
 *  This section contains the code for manipulating gab values - specifically
 * heap-allocated objects.
 * ----------------------------------------
 */

#define GAB_CREATE_OBJ(obj_type, kind)                                         \
  ((struct obj_type *)__gab_objcreate(gab, sizeof(struct obj_type), kind))

#define GAB_CREATE_FLEX_OBJ(obj_type, flex_type, flex_count, kind)             \
  ((struct obj_type *)__gab_objcreate(                                         \
      gab, sizeof(struct obj_type) + sizeof(flex_type) * (flex_count),         \
      (kind)))

GAB_INTERNAL struct gab_obj *__gab_objcreate(struct gab_triple gab, uint64_t sz,
                                             enum gab_kind k) {
  struct gab_obj *self = gab_egalloc(gab, nullptr, sz);
  gab.eg->sizes[k] += sz;
  gab.eg->counts[k]++;

  self->kind = k;
  self->references = 1;
  self->flags = fGAB_OBJ_NEW;

#if cGAB_LOG_GC
  fprintf(stderr, "(%i) CREATE\t%p\t%lu\t%d\n", gab.wkid, (void *)self, sz, k);
#endif

  struct gab_job *wk = gab.eg->jobs + gab.wkid;
  if (wk->locked) {
    v_gab_value_push(&wk->lock_keep, __gab_obj(self));
    // TODO @cgab @bug: Is buffering when locked correct?
    // I think this would just leak memory
    // if it actually did anything.
    GAB_OBJ_BUFFERED(self);
#if cGAB_LOG_GC
    fprintf(stderr, "(%i) QLOCK\t%p\n", gab.wkid, (void *)self);
#endif
  } else {
    gab_dref(gab, __gab_obj(self));
  }

  return self;
}

GAB_INTERNAL uint64_t __gab_objsize(struct gab_obj *obj) {
  switch (obj->kind) {
  case kGAB_CHANNEL:
  case kGAB_CHANNELCLOSED:
    return sizeof(struct gab_ochannel);
  case kGAB_BOX: {
    struct gab_obox *o = (struct gab_obox *)obj;
    return sizeof(struct gab_obox) + o->len * sizeof(char);
  }
  case kGAB_RECORDNODE: {
    struct gab_orecnode *o = (struct gab_orecnode *)obj;
    return sizeof(struct gab_orecnode) + o->len * sizeof(gab_value);
  }
  case kGAB_RECORD: {
    struct gab_orec *o = (struct gab_orec *)obj;
    return sizeof(struct gab_orec) + o->len * sizeof(gab_value);
  }
  case kGAB_BLOCK: {
    struct gab_oblock *o = (struct gab_oblock *)obj;
    return sizeof(struct gab_oblock) + o->nupvalues * sizeof(gab_value);
  }
  case kGAB_PROTOTYPE: {
    struct gab_oprototype *o = (struct gab_oprototype *)obj;
    return sizeof(struct gab_oprototype) + o->nupvalues * sizeof(char);
  }
  case kGAB_SHAPE:
  case kGAB_SHAPENODE:
  case kGAB_SHAPELIST: {
    struct gab_oshape *o = (struct gab_oshape *)obj;
    return sizeof(struct gab_oshape) + o->datalen * sizeof(gab_value);
  }
  case kGAB_STRING: {
    struct gab_ostring *o = (struct gab_ostring *)obj;
    return sizeof(struct gab_ostring) + (o->len + 1) * sizeof(char);
  }
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE:
    return sizeof(struct gab_ofiber);
  case kGAB_NATIVE:
    return sizeof(struct gab_onative);
  default:
    break;
  }

  gab_unreachable("gab_objsize() of unknown kind %d", obj->kind);
  return 0;
}

GAB_INTERNAL int64_t __gab_sshpnodedumpkeys(char **dest, uint64_t *n,
                                            gab_value shape, int depth);

GAB_INTERNAL int64_t __gab_sshpdumpkeys(char **dest, uint64_t *n,
                                        gab_value shape, int depth) {
  uint64_t len = gab_shplen(shape);

  if (len == 0)
    return 0;

  if (len > cGAB_SHAPE_LEN_CUTOFF && depth >= 0)
    return __gab_snprintf_through(dest, n, "... ");

  if (__gab_snprintf_through(dest, n, " ") < 0)
    return -1;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_svalinspect(dest, n, gab_ushpat(shape, i), depth - 1) < 0)
      return -1;

    if (i + 1 < len)
      if (__gab_snprintf_through(dest, n, " ") < 0)
        return -1;
  }

  return __gab_snprintf_through(dest, n, " ");
}

GAB_INTERNAL int64_t __gab_srecdumpvals(char **dest, uint64_t *n, gab_value rec,
                                        int depth) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));
  uint64_t len = gab_reclen(rec);

  if (len == 0)
    return 0;

  if (len > cGAB_LIST_LEN_CUTOFF && depth >= 0)
    return __gab_snprintf_through(dest, n, " ... ");

  if (__gab_snprintf_through(dest, n, " ") < 0)
    return -1;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_svalinspect(dest, n, gab_uvrecat(rec, i), depth - 1) < 0)
      return -1;

    if (i + 1 < len)
      if (__gab_snprintf_through(dest, n, ", ") < 0)
        return -1;
  }

  return __gab_snprintf_through(dest, n, " ");
}

GAB_INTERNAL int64_t __gab_srecdumpprops(char **dest, uint64_t *n,
                                         gab_value rec, int depth) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));
  uint64_t len = gab_reclen(rec);

  if (len == 0)
    return 0;

  if (len > cGAB_DICT_LEN_CUTOFF && depth >= 0)
    return __gab_snprintf_through(dest, n, " ... ");

  if (__gab_snprintf_through(dest, n, " ") < 0)
    return -1;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_svalinspect(dest, n, gab_ukrecat(rec, i), depth - 1) < 0)
      return -1;

    if (__gab_snprintf_through(dest, n, " ") < 0)
      return -1;

    if (gab_svalinspect(dest, n, gab_uvrecat(rec, i), depth - 1) < 0)
      return -1;

    if (i + 1 < len)
      if (__gab_snprintf_through(dest, n, ", ") < 0)
        return -1;
  }

  return __gab_snprintf_through(dest, n, " ");
}

GAB_INTERNAL int64_t __gab_svalinspect(char **dest, uint64_t *n, gab_value self,
                                       int depth) {
  switch (gab_valkind(self)) {
  case kGAB_PRIMITIVE: {
    switch (self) {
    case gab_cundefined:
      return __gab_snprintf_through(dest, n, "cundefined");
    case gab_cinvalid:
      return __gab_snprintf_through(dest, n, "cinvalid");
    case gab_ctimeout:
      return __gab_snprintf_through(dest, n, "ctimeout");
    case gab_cvalid:
      return __gab_snprintf_through(dest, n, "cvalid");
    default:
      return __gab_snprintf_through(dest, n, "<" tGAB_PRIMITIVE " %s>",
                                    gab_opcode_names[gab_valtop(self)]);
    }
  }
  case kGAB_NUMBER:
    return __gab_snprintf_through(dest, n, "%lg", gab_valtof(self));
  case kGAB_STRING:
    return __gab_snprintf_through(dest, n, "%s", gab_strdata(&self));
  case kGAB_BINARY: {
    const char *s = gab_strdata(&self);

    if (__gab_snprintf_through(dest, n, "<" tGAB_BINARY " 0x") < 0)
      return -1;

    uint64_t len = gab_strlen(self);

    if (len < cGAB_BINARY_LEN_CUTOFF) {
      while (len--)
        if (__gab_snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;
    } else {
      uint64_t preview = cGAB_BINARY_LEN_CUTOFF;
      while (preview--)
        if (__gab_snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;

      if (__gab_snprintf_through(dest, n, "...") < 0)
        return -1;
    }

    if (__gab_snprintf_through(dest, n, ">") < 0)
      return -1;

    return 0;
  }
  case kGAB_MESSAGE:
    return __gab_snprintf_through(dest, n, "%s:", gab_strdata(&self));
  case kGAB_SHAPENODE:
    return __gab_sshpnodedumpkeys(dest, n, self, depth);
  case kGAB_SHAPE:
  case kGAB_SHAPELIST:
    return __gab_snprintf_through(dest, n, "<" tGAB_SHAPE " ") +
           __gab_sshpdumpkeys(dest, n, self, depth) +
           __gab_snprintf_through(dest, n, ">");
  case kGAB_CHANNEL:
    return __gab_snprintf_through(dest, n, "<" tGAB_CHANNEL " %p>",
                                  GAB_VAL_TO_CHANNEL(self));
  case kGAB_CHANNELCLOSED:
    return __gab_snprintf_through(dest, n, "<" tGAB_CHANNEL " %p>",
                                  GAB_VAL_TO_CHANNEL(self));
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE: {
    struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(self);

    return __gab_snprintf_through(dest, n, "<" tGAB_FIBER " %p ", fiber) +
           __gab_svalinspect(dest, n, fiber->data[0], 0) +
           __gab_snprintf_through(dest, n, " ") +
           __gab_svalinspect(dest, n, fiber->data[1], 0) +
           __gab_snprintf_through(dest, n, ">");
  }
  case kGAB_RECORD: {
    if (gab_valkind(gab_recshp(self)) == kGAB_SHAPELIST)
      return __gab_snprintf_through(dest, n, "[") +
             __gab_srecdumpvals(dest, n, self, depth) +
             __gab_snprintf_through(dest, n, "]");
    else
      return __gab_snprintf_through(dest, n, "{") +
             __gab_srecdumpprops(dest, n, self, depth) +
             __gab_snprintf_through(dest, n, "}");
  }
  case kGAB_RECORDNODE:
    return __gab_snprintf_through(dest, n, "RECORDNODE");
  case kGAB_BOX: {
    struct gab_obox *con = GAB_VAL_TO_BOX(self);
    return __gab_snprintf_through(dest, n, "<" tGAB_BOX " ") +
           gab_svalinspect(dest, n, con->type, depth) +
           __gab_snprintf_through(dest, n, ">");
  }
  case kGAB_BLOCK: {
    struct gab_oblock *blk = GAB_VAL_TO_BLOCK(self);
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);
    uint64_t line = gab_srcline(p->src, p->offset);
    return __gab_snprintf_through(dest, n, "<" tGAB_BLOCK " ") +
           gab_svalinspect(dest, n, gab_srcname(p->src), depth) +
           __gab_snprintf_through(dest, n, ":%lu>", line);
  }
  case kGAB_NATIVE: {
    struct gab_onative *native = GAB_VAL_TO_NATIVE(self);
    return __gab_snprintf_through(dest, n, "<" tGAB_NATIVE " ") +
           gab_svalinspect(dest, n, native->name, depth) +
           __gab_snprintf_through(dest, n, ">");
  }
  case kGAB_PROTOTYPE: {
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(self);
    uint64_t line = gab_srcline(p->src, p->offset);
    return __gab_snprintf_through(dest, n, "<" tGAB_PROTOTYPE " ") +
           gab_svalinspect(dest, n, gab_srcname(p->src), depth) +
           __gab_snprintf_through(dest, n, ":%lu>", line);
  }
  default:
    gab_unreachable("Inspecting unrecognized object kind %d in %p\n",
                    gab_valkind(self), gab_valtoo(self));
    return 0;
  }
}

GAB_API int64_t gab_svalinspect(char **dest, uint64_t *n, gab_value value,
                                int depth) {
  return __gab_svalinspect(dest, n, value, depth);
}

GAB_API int64_t gab_fvalinspect(FILE *stream, gab_value self, int depth) {
  for (uint64_t i = 128;; i <<= 1) {
    uint64_t n = i;
    char buf[n];
    char *cursor = buf;
    if (gab_svalinspect(&cursor, &n, self, depth) >= 0) {
      fprintf(stream, "%s", buf);
      return 1;
    };
  }

  return 0;
}

GAB_API void __gab_objdestroy(struct gab_triple gab, struct gab_obj *self) {
  gab.eg->sizes[self->kind] -= __gab_objsize(self);
  gab.eg->counts[self->kind]--;

  switch (self->kind) {
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE: {
    struct gab_ofiber *fib = (struct gab_ofiber *)self;

    /*if (fib->res_values != nullptr)*/
    /*  a_gab_value_destroy(fib->res_values);*/

    v_uint8_t_destroy(&fib->allocator);
    break;
  };
  case kGAB_BOX: {
    struct gab_obox *box = (struct gab_obox *)self;
    if (box->do_destroy)
      box->do_destroy(gab, box->len, box->data);
    break;
  }
  case kGAB_SHAPE:
  case kGAB_SHAPELIST: {
    gab_assert(mtx_trylock(&gab.eg->gc_mtx) == thrd_busy,
               "This thread must be holding the gc_mtx already.");
    // TODO @cgab @opt: Shapes aren't guaranteed to be in here, if they were
    // created intermittently.
    d_shapes_remove(&gab.eg->shapes, (struct gab_oshape *)self);
    // gab_assert(removed, "Must succeed in removing shape %p", self);
    break;
  }
  case kGAB_STRING: {
    gab_assert(mtx_trylock(&gab.eg->gc_mtx) == thrd_busy,
               "This thread must be holding the gc_mtx already.");
    d_strings_remove(&gab.eg->strings, (struct gab_ostring *)self);
    // gab_assert(removed, "Must succeed in removing string");
    break;
  }
  default:
    break;
  }
}

GAB_API gab_value gab_shorstr(uint64_t len, const char *data) {
  gab_precondition(len <= 5, "Cannot make short string with len > 5: %lu", len);

  gab_value v = 0;
  v |= (__GAB_QNAN | (uint64_t)kGAB_STRING << __GAB_TAGOFFSET |
        (((uint64_t)5 - len) << 40));

  for (uint64_t i = 0; i < len; i++) {
    v |= (uint64_t)(0xff & data[i]) << (i * 8);
  }

  return v;
}

GAB_INTERNAL gab_value __gab_shortstrcat(gab_value _a, gab_value _b) {
  gab_precondition(gab_valkind(_a) == kGAB_STRING ||
                       gab_valkind(_a) == kGAB_BINARY,
                   "Invalid kind");

  uint64_t alen = gab_strlen(_a);
  uint64_t blen = gab_strlen(_b);

  gab_assert(alen + blen <= 5, "Shortstr should have len <= 5");

  uint8_t len = alen + blen;

  gab_value v = 0;
  v |= (__GAB_QNAN | (uint64_t)kGAB_STRING << __GAB_TAGOFFSET |
        (((uint64_t)5 - len) << 40));

  for (uint64_t i = 0; i < alen; i++) {
    v |= (uint64_t)(0xff & gab_strdata(&_a)[i]) << (i * 8);
  }

  for (uint64_t i = 0; i < blen; i++) {
    v |= (uint64_t)(0xff & gab_strdata(&_b)[i]) << ((i + alen) * 8);
  }

  gab_assert(gab_valkind(v) == kGAB_STRING,
             "Sanity check that we didn't mess with the tag");

  return v;
}

GAB_INTERNAL gab_value __gab_nstring(struct gab_triple gab, uint64_t hash,
                                     uint64_t len, const char *data) {
  s_char str = s_char_create(data, len);

  struct gab_ostring *self =
      GAB_CREATE_FLEX_OBJ(gab_ostring, char, str.len + 1, kGAB_STRING);

  memcpy(self->data, str.data, str.len * sizeof(char));
  self->len = str.len;
  self->hash = hash;

  uint64_t count = 0;
  bool pass = !__gab_utf8_codepoints((uint8_t *)self->data, &count);

  self->mb_len = pass ? count : -1;

  return __gab_obj(self);
}

GAB_API gab_value gab_tnstring(struct gab_triple gab, uint64_t len,
                               const char *data) {
  if (len <= 5)
    return gab_shorstr(len, data);

#if cGAB_STRING_HASHLEN > 0
  uint64_t hash =
      hash_bytes(len < cGAB_STRING_HASHLEN ? len : cGAB_STRING_HASHLEN,
                 (unsigned char *)data);
#else
  uint64_t hash = hash_bytes(len, (unsigned char *)data);
#endif

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  struct gab_ostring *interned = __gab_egstrfind(gab.eg, hash, len, data);

  mtx_unlock(&gab.eg->gc_mtx);

  if (interned)
    return __gab_obj(interned);

  /*
   * We can't hold the strings_mtx lock here in the call
   * to nstring, because the creation of this object
   * might signal a collection. In that case, the gc needs to hold
   * the strings_mtx for the duration of the collection.
   */
  gab_value s = __gab_nstring(gab, hash, len, data);

  /*
   * TODO @cgab @bug: Potential str mem leak
   * Inbetween the two lock holds here, another thread
   * *could* insert the string we want into the dict.
   * In that case, we'd stomp over the old value
   * and leak its memory.
   *
   * If this is a big deal, we can simply perform another check after locking.
   */

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  d_strings_insert(&gab.eg->strings, GAB_VAL_TO_STRING(s), 0);

  mtx_unlock(&gab.eg->gc_mtx);

  return s;
}

GAB_API gab_value gab_nstring(struct gab_triple gab, uint64_t len,
                              const char *data) {
  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_IGN:
      break;
    case sGAB_TERM:
      // break;
      return gab_cinvalid;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    }

    gab_value str = gab_tnstring(gab, len, data);
    if (str == gab_cinvalid)
      return str;

    if (str == gab_ctimeout)
      continue;

    gab_assert(gab_valkind(str) == kGAB_STRING,
               "We must have created a string");
    return str;
  }
};

GAB_API gab_value gab_strcat(struct gab_triple gab, gab_value _a,
                             gab_value _b) {
  // Helpfully forward cinvalid. This helps strings flow through this code.
  if (_a == gab_cinvalid || _b == gab_cinvalid)
    return gab_cinvalid;

  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_IGN:
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    }

    gab_value str = gab_tstrcat(gab, _a, _b);
    if (str == gab_cinvalid)
      return str;

    if (str == gab_ctimeout)
      continue;

    gab_assert(gab_valkind(str) == kGAB_STRING, "Must have created a string");
    return str;
  }
}

GAB_API inline const char *gab_strdata(gab_value *str) {
  gab_precondition(gab_valkind(*str) == kGAB_STRING ||
                       gab_valkind(*str) == kGAB_MESSAGE ||
                       gab_valkind(*str) == kGAB_BINARY,
                   "Invalid kind");

  if (gab_valiso(*str))
    return GAB_VAL_TO_STRING(*str)->data;

  return ((const char *)str);
}

GAB_API uint64_t gab_strlen(gab_value str) {
  gab_precondition(gab_valkind(str) == kGAB_STRING ||
                       gab_valkind(str) == kGAB_MESSAGE ||
                       gab_valkind(str) == kGAB_BINARY,
                   "Invalid kind");

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->len;

  return 5 - ((str >> 40) & 0xFF);
};

GAB_API uint64_t gab_strmblen(gab_value str) {
  gab_precondition(gab_valkind(str) == kGAB_STRING ||
                       gab_valkind(str) == kGAB_BINARY ||
                       gab_valkind(str) == kGAB_MESSAGE,
                   "Invalid kind");

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->mb_len;

  // This is a small string. No space to store mb_len, so just recompute.
  uint64_t count = 0;
  bool pass = !__gab_utf8_codepoints((uint8_t *)&str, &count);

  return pass ? count : -1;
};

GAB_API uint64_t gab_strhash(gab_value str) {
  gab_precondition(gab_valkind(str) == kGAB_STRING ||
                       gab_valkind(str) == kGAB_BINARY,
                   "Invalid kind");

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->hash;

  // TODO @cgab @bug: Propertly hash the contents of short strings.
  return str;
}

GAB_API int gab_binat(gab_value str, uint64_t idx) {
  gab_precondition(gab_valkind(str) == kGAB_BINARY, "Invalid Kind");

  uint64_t len = gab_strlen(str);

  if (idx >= len)
    return -1;

  return gab_strdata(&str)[idx];
}

/*
  Given two strings, create a third which is the concatenation a+b
*/
GAB_API gab_value gab_tstrcat(struct gab_triple gab, gab_value _a,
                              gab_value _b) {
  gab_precondition(gab_valkind(_a) == kGAB_STRING, "Invalid kind");
  gab_precondition(gab_valkind(_b) == kGAB_STRING, "Invalid kind");

  uint64_t alen = gab_strlen(_a);
  uint64_t blen = gab_strlen(_b);

  if (alen == 0)
    return _b;

  if (blen == 0)
    return _a;

  uint64_t len = alen + blen;

  if (len <= 5)
    return __gab_shortstrcat(_a, _b);

  a_char *buff = a_char_empty(len + 1);

  // Copy the data into the string obj.
  memcpy(buff->data, gab_strdata(&_a), alen);
  memcpy(buff->data + alen, gab_strdata(&_b), blen);

// Pre compute the hash
#if cGAB_STRING_HASHLEN > 0
  uint64_t hash =
      hash_bytes(len < cGAB_STRING_HASHLEN ? len : cGAB_STRING_HASHLEN,
                 (unsigned char *)buff->data);
#else
  uint64_t hash = hash_bytes(len, (unsigned char *)buff->data);
#endif

  /*
    If this string was interned already, return.

    Unfortunately, we can't check for this before copying and computing the
    hash.
  */

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  struct gab_ostring *interned = __gab_egstrfind(gab.eg, hash, len, buff->data);

  mtx_unlock(&gab.eg->gc_mtx);

  if (interned)
    return a_char_destroy(buff), __gab_obj(interned);

  gab_value result = __gab_nstring(gab, hash, len, buff->data);

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  d_strings_insert(&gab.eg->strings, GAB_VAL_TO_STRING(result), 0);

  mtx_unlock(&gab.eg->gc_mtx);

  assert(gab_valkind(result) == kGAB_STRING);
  assert(gab_strlen(result) == len);

  return a_char_destroy(buff), result;
};

GAB_API gab_value gab_prototype(struct gab_triple gab, struct gab_src *src,
                                uint64_t offset, uint64_t len,
                                struct gab_prototype_argt args) {

  struct gab_oprototype *self = GAB_CREATE_FLEX_OBJ(
      gab_oprototype, uint8_t, args.nupvalues, kGAB_PROTOTYPE);

  self->src = src;
  self->offset = offset;
  self->len = args.nupvalues;
  self->len = len;
  self->nslots = args.nslots;
  self->nlocals = args.nlocals;
  self->nupvalues = args.nupvalues;
  self->narguments = args.narguments;
  self->env = args.env;

  if (args.nupvalues > 0) {
    if (args.data) {
      memcpy(self->data, args.data, args.nupvalues * sizeof(uint8_t));
    } else if (args.flags && args.indexes) {
      for (uint8_t i = 0; i < args.nupvalues; i++) {
        bool is_local = args.flags[i] & fLOCAL_LOCAL;
        self->data[i] = (args.indexes[i] << 1) | is_local;
      }
    } else {
      assert(0 && "Invalid arguments to gab_bprototype");
    }
  }

  return __gab_obj(self);
}

GAB_API gab_value gab_prtenv(gab_value prt) {
  assert(gab_valkind(prt) == kGAB_PROTOTYPE);
  return GAB_VAL_TO_PROTOTYPE(prt)->env;
}

GAB_API gab_value gab_prtparams(struct gab_triple gab, gab_value prt) {
  assert(gab_valkind(prt) == kGAB_PROTOTYPE);
  gab_value shp = gab_prtshp(prt);
  uint8_t nargs = GAB_VAL_TO_PROTOTYPE(prt)->narguments;

  gab_value vargs[nargs];

  // TODO @cgab @opt: This is n^2. Can be done in one traversal
  for (uint64_t i = 0; i < nargs; i++)
    vargs[i] = gab_ushpat(shp, i);

  return gab_shape(gab, 1, nargs, vargs);
}

GAB_API gab_value gab_native(struct gab_triple gab, gab_value name,
                             gab_native_f f) {
  assert(gab_valkind(name) == kGAB_STRING || gab_valkind(name) == kGAB_MESSAGE);

  struct gab_onative *self = GAB_CREATE_OBJ(gab_onative, kGAB_NATIVE);

  self->name = name;
  self->function = f;

  return __gab_obj(self);
}

GAB_API gab_value gab_snative(struct gab_triple gab, const char *name,
                              gab_native_f f) {
  return gab_native(gab, gab_string(gab, name), f);
}

GAB_API gab_value gab_block(struct gab_triple gab, gab_value prototype) {
  assert(gab_valkind(prototype) == kGAB_PROTOTYPE);
  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(prototype);

  struct gab_oblock *self =
      GAB_CREATE_FLEX_OBJ(gab_oblock, gab_value, p->nupvalues, kGAB_BLOCK);

  self->p = prototype;
  self->nupvalues = p->nupvalues;

  for (uint8_t i = 0; i < self->nupvalues; i++) {
    self->upvalues[i] = gab_cinvalid;
  }

  return __gab_obj(self);
}

GAB_API gab_value gab_blkproto(gab_value block) {
  assert(gab_valkind(block) == kGAB_BLOCK);
  return GAB_VAL_TO_BLOCK(block)->p;
}

GAB_API gab_value gab_box(struct gab_triple gab, struct gab_box_argt args) {
  struct gab_obox *self =
      GAB_CREATE_FLEX_OBJ(gab_obox, unsigned char, args.size, kGAB_BOX);

  self->do_destroy = args.destructor;
  self->type = args.type;
  self->len = args.size;

  if (args.data) {
    memcpy(self->data, args.data, args.size);
  } else {
    memset(self->data, 0, args.size);
  }

  return __gab_obj(self);
}

GAB_API uint64_t gab_boxlen(gab_value box) {
  assert(gab_valkind(box) == kGAB_BOX);
  return GAB_VAL_TO_BOX(box)->len;
}

GAB_API void *gab_boxdata(gab_value box) {
  assert(gab_valkind(box) == kGAB_BOX);
  return GAB_VAL_TO_BOX(box)->data;
}

GAB_API gab_value gab_boxtype(gab_value value) {
  assert(gab_valkind(value) == kGAB_BOX);
  return GAB_VAL_TO_BOX(value)->type;
}

GAB_INTERNAL gab_value __gab_shape(struct gab_triple gab, uint64_t hash,
                                   uint64_t len, uint32_t nmask, uint32_t lmask,
                                   uint64_t datalen, int64_t adjustment,
                                   gab_value *data) {
  gab_precondition(
      datalen + adjustment >= 0,
      "Shapes must have size >= 0. Got len %lu and adjustment %li.", datalen,
      adjustment);

  uint64_t real_len = datalen + (adjustment * 2);

  gab_precondition(
      real_len <= 64,
      "Due to branching factor, datalen can never be more than 64. "
      "Tried to create %lu",
      real_len);

  gab_precondition(real_len % 2 == 0, "datalen must be a multiple of 2");

  struct gab_oshape *self =
      GAB_CREATE_FLEX_OBJ(gab_oshape, gab_value, real_len, kGAB_SHAPELIST);

  self->hash = hash;
  self->len = len;
  self->datalen = real_len;
  self->nmask = nmask;
  self->lmask = lmask;

  uint64_t count = self->datalen < datalen ? self->datalen : datalen;

  if (datalen && data) {
    memcpy(self->data, data, sizeof(gab_value) * count);
  }

  for (uint64_t i = count; i < self->datalen; i++)
    self->data[i] = gab_cinvalid;

  return __gab_obj(self);
}

GAB_INTERNAL gab_value __gab_shapelist(struct gab_triple gab, uint64_t hash,
                                       uint64_t len, uint32_t nmask,
                                       uint32_t lmask, uint64_t datalen,
                                       int64_t adjustment, gab_value *data) {
  gab_value shp =
      __gab_shape(gab, hash, len, nmask, lmask, datalen, adjustment, data);
  GAB_VAL_TO_SHAPE(shp)->header.kind = kGAB_SHAPELIST;
  return shp;
}

GAB_INTERNAL gab_value __gab_shapenode(struct gab_triple gab, uint32_t nmask,
                                       uint32_t lmask, uint64_t datalen,
                                       int64_t adjustment, gab_value *data) {
  gab_precondition(
      datalen + adjustment > 0,
      "Shape nodes must have size > 0. Got len %lu and adjustment %li.",
      datalen, adjustment);

  uint64_t real_len = datalen + (adjustment * 2);

  if (real_len == 0)
    return gab_cinvalid;

  gab_precondition(
      real_len <= 64,
      "Due to branching factor, datalen can never be more than 64. "
      "Tried to create %lu",
      real_len);

  gab_precondition(real_len % 2 == 0, "datalen must be a multiple of 2");

  struct gab_oshapenode *self =
      GAB_CREATE_FLEX_OBJ(gab_oshapenode, gab_value, real_len, kGAB_SHAPENODE);

  self->datalen = real_len;
  self->nmask = nmask;
  self->lmask = lmask;

  uint64_t count = self->datalen < datalen ? self->datalen : datalen;

  if (datalen && data) {
    memcpy(self->data, data, sizeof(gab_value) * count);
  }

  for (uint64_t i = count; i < self->datalen; i++)
    self->data[i] = gab_cinvalid;

  return __gab_obj(self);
};

GAB_INTERNAL uint64_t __gab_shpnth(gab_value shape, uint64_t midx) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind: %d for %p", gab_valkind(shape),
                   gab_valtoo(shape));
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);
  // Count the number of bits in smask before the bit midx
  // (1 << midx) - 1 creates a mask of ones beneath midx.
  return popcountl(((((uint32_t)1 << midx) - 1) & s->nmask));
}

GAB_INTERNAL bool __gab_shpisn(gab_value shape, uint64_t midx) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);
  return s->nmask & ((uint32_t)1 << midx);
}

GAB_INTERNAL bool __gab_shpisl(gab_value shape, uint64_t midx) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);
  return s->lmask & ((uint32_t)1 << midx);
}

GAB_INTERNAL gab_value __gab_shpkey(gab_value shape, uint64_t sidx) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);
  sidx *= 2;

  gab_precondition(sidx < s->datalen, "Invalid index %lu in len %lu", sidx,
                   s->len);

  return s->data[sidx];
}

GAB_INTERNAL gab_value __gab_shpval(gab_value shape, uint64_t sidx) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);
  sidx *= 2;
  sidx++;

  gab_precondition(sidx < s->datalen, "Invalid index %lu in len %lu", sidx,
                   s->len);

  return s->data[sidx];
}

GAB_INTERNAL bool __gab_validatenode(gab_value shape) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);

  gab_assert(popcountl(s->nmask) * 2 == s->datalen,
             "DATALEN should be popcount * 2. Saw %u for %b", s->datalen,
             s->nmask);

  for (uint64_t midx = 0; midx < 32; midx++) {
    uint32_t sidx = __gab_shpnth(shape, midx);

    if (__gab_shpisl(shape, midx))
      gab_assert(__gab_shpisn(shape, midx), "Leaves must also be nodes");

    if (__gab_shpisn(shape, midx)) {
      if (__gab_shpisl(shape, midx)) {
        gab_assert(__gab_shpkey(shape, sidx) != gab_cinvalid,
                   "Invalid key in leaf");
      } else {
        gab_value c = __gab_shpkey(shape, sidx);
        if (!__gab_validatenode(c))
          return false;
      }
    }
  }

  return true;
}

GAB_INTERNAL gab_value __gab_shpcpy(struct gab_triple gab, gab_value shape,
                                    int64_t adjustment) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);

  switch (s->header.kind) {
  case kGAB_SHAPELIST:
    return __gab_shapelist(gab, s->hash, s->len, s->nmask, s->lmask, s->datalen,
                           adjustment, s->data);
  case kGAB_SHAPE:
    return __gab_shape(gab, s->hash, s->len, s->nmask, s->lmask, s->datalen,
                       adjustment, s->data);
  case kGAB_SHAPENODE:
    return __gab_shapenode(gab, s->nmask, s->lmask, s->datalen, adjustment,
                           s->data);
  default:
    gab_unreachable("Impossible shape kind %d", gab_valkind(shape));
    return gab_cundefined;
  }
}

GAB_INTERNAL void __gab_shpassoc(gab_value shape, gab_value node,
                                 uint64_t sidx) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  gab_precondition(shape != node, "Nonsensical loop");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);

  gab_precondition(sidx < s->datalen, "Invalid index %lu in len %lu", sidx,
                   s->len);

  s->data[sidx] = node;
}

GAB_INTERNAL void __gab_shpbshrink(gab_value shape, uint64_t midx,
                                   gab_value src_shape) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);

  struct gab_oshape *src_s = GAB_VAL_TO_SHAPE(src_shape);

  uint64_t sidx = __gab_shpnth(src_shape, midx);

  gab_value *dst = s->data;
  gab_value *src = src_s->data;
  uint64_t srclen = src_s->datalen;
  uint64_t offset = sidx * 2;

  gab_assert(offset + 2 <= srclen, "Invalid memcpy offset");

  memcpy(dst + offset, src + offset + 2,
         (srclen - 2 - offset) * sizeof(gab_value));

  // Turn off b_midx in the new branch
  s->nmask &= ~((uint32_t)1 << midx);
  s->lmask &= ~((uint32_t)1 << midx);
}

GAB_INTERNAL void __gab_shplext(gab_value shape, uint64_t midx, gab_value key,
                                uint64_t val) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);

  uint64_t sidx = __gab_shpnth(shape, midx);

  uint64_t dataoffset = sidx * 2;

  gab_assert(dataoffset < s->datalen, "Invalid memmove offset");

  // TODO @cgab @opt: This memmove can be included in the original cpy.
  memmove(s->data + dataoffset + 2, s->data + dataoffset,
          (s->datalen - (dataoffset + 2)) * sizeof(gab_value));

  __gab_shpassoc(shape, key, sidx * 2);
  __gab_shpassoc(shape, val, sidx * 2 + 1);

  s->lmask |= ((uint32_t)1 << midx);
  s->nmask |= ((uint32_t)1 << midx);
}

GAB_INTERNAL void __gab_shplprm(gab_value shape, uint64_t midx) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shape);

  s->lmask &= ~((uint32_t)1 << midx);
  s->nmask |= ((uint32_t)1 << midx);
};

// When shift gets above 64, wraps around.
// This is designed to continue shifting, starting at slightly different points
// in the hash, so that we see different slices of the hash every iteration.
GAB_INTERNAL uint64_t __gab_shpishift(uint64_t shift) {
  shift += GAB_PVEC_BITS;

  if (shift >= 64)
    shift = (shift + 1) % GAB_PVEC_BITS;

  return shift;
};

GAB_INTERNAL uint32_t __gab_shpmidx(uint64_t hash, uint64_t shift) {
  return (hash >> shift) & GAB_PVEC_MASK;
}

GAB_API uint64_t gab_shpfind(gab_value shape, gab_value key) {
  gab_value node = shape;

  for (uint64_t shift = 0;; shift = __gab_shpishift(shift)) {
    uint32_t midx = __gab_shpmidx(key, shift);
    uint64_t sidx = __gab_shpnth(node, midx);

    if (!__gab_shpisn(node, midx))
      return -1;

    if (__gab_shpisl(node, midx))
      return gab_valeq(__gab_shpkey(node, sidx), key) ? __gab_shpval(node, sidx)
                                                      : -1;

    node = __gab_shpkey(node, sidx);
  }

  gab_unreachable("Shall not escape above loop");
  return -1;
}

GAB_API gab_value __gab_ushpat(gab_value shape, uint64_t idx) {
  // TODO @cgab @opt: Iterate indices properly, no brute-forcing
  for (uint64_t midx = 0; midx < 32; midx++) {
    uint32_t sidx = __gab_shpnth(shape, midx);

    if (__gab_shpisn(shape, midx)) {
      // We have a node
      if (__gab_shpisl(shape, midx)) {
        // We have a leaf!
        if (__gab_shpval(shape, sidx) == idx)
          // If its a match, return
          return __gab_shpkey(shape, sidx);

        continue;
      }

      // Recurse into branch
      gab_value c = __gab_shpkey(shape, sidx);
      gab_value res = __gab_ushpat(c, idx);

      if (res != gab_cinvalid)
        return res;
    }
  }

  return gab_cinvalid;
}

GAB_INTERNAL int64_t __gab_sshpnodedumpkeys(char **dest, uint64_t *n,
                                            gab_value shape, int depth) {
  for (uint64_t midx = 0; midx < 32; midx++) {
    uint32_t sidx = __gab_shpnth(shape, midx);

    if (__gab_shpisn(shape, midx)) {
      // We have a node
      if (__gab_shpisl(shape, midx)) {
        // We have a leaf!
        if (gab_svalinspect(dest, n, __gab_shpkey(shape, sidx), depth) < 0)
          return -1;

        continue;
      }

      // Recurse into branch
      gab_value c = __gab_shpkey(shape, sidx);
      if (__gab_sshpnodedumpkeys(dest, n, c, depth - 1) < 0)
        return -1;
    }
  }

  return 0;
}

GAB_API gab_value gab_ushpat(gab_value shape, uint64_t idx) {
  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST ||
                       gab_valkind(shape) == kGAB_SHAPENODE,
                   "Invalid kind");

  gab_precondition(idx < gab_shplen(shape), "Index %lu out of range %lu", idx,
                   gab_shplen(shape));

  gab_value res = __gab_ushpat(shape, idx);

  gab_assert(res != gab_cinvalid, "__gab_ushpat failed");
  gab_assert(gab_valkind(res) != kGAB_SHAPENODE, "__gab_ushpat failed");

  return res;
}

GAB_INTERNAL gab_value __gab_rshptake(struct gab_triple gab, gab_value node,
                                      gab_value key, uint64_t shift) {
  uint32_t midx = __gab_shpmidx(key, shift);

  // We are at the leaf.
  if (__gab_shpisl(node, midx)) {
    gab_value newnode = __gab_shpcpy(gab, node, -1);

    // We may shrink ourselves to nothing.
    if (newnode == gab_cinvalid)
      return gab_cinvalid;

    // Perform the shrink
    __gab_shpbshrink(newnode, midx, node);
    return newnode;
  }

  uint64_t sidx = __gab_shpnth(node, midx);
  gab_value branch = __gab_shpkey(node, sidx);

  gab_value newbranch =
      __gab_rshptake(gab, branch, key, __gab_shpishift(shift));

  // We may shrink a node to nothing.
  // In this case, we should shrink ourselves

  if (newbranch == gab_cinvalid) {
    gab_value newnode = __gab_shpcpy(gab, node, -1);

    // We may shrink ourselves to nothing.
    if (newnode == gab_cinvalid)
      return gab_cinvalid;

    // Perform the shrink
    __gab_shpbshrink(newnode, midx, node);
    return newnode;
  }

  // Our node did not shrink to nothing.
  gab_value newnode = __gab_shpcpy(gab, node, 0);
  // We copy and assoc the new branch
  __gab_shpassoc(newnode, newbranch, sidx * 2);
  return newnode;
}

GAB_INTERNAL gab_value __gab_shpput(struct gab_triple gab, gab_value shape,
                                    gab_value key, uint64_t val) {
  bool needs_space = !__gab_shpisn(shape, key & GAB_PVEC_MASK);

  gab_value node = __gab_shpcpy(gab, shape, needs_space);
  gab_value root = node;

  for (uint64_t shift = 0;; shift = __gab_shpishift(shift)) {
    uint32_t midx = __gab_shpmidx(key, shift);

    if (!__gab_shpisn(node, midx)) {
      // No node at this hashed index. copy-extend with a leaf
      __gab_shplext(node, midx, key, val);

      goto done;
    }

    if (!__gab_shpisl(node, midx)) {
      // Branch node at this hash index. copy-continue
      uint64_t sidx = __gab_shpnth(node, midx);
      gab_value branch = __gab_shpkey(node, sidx);

      uint64_t b_midx = __gab_shpmidx(key, __gab_shpishift(shift));

      bool needs_space = !(__gab_shpisn(branch, b_midx));

      branch = __gab_shpcpy(gab, branch, needs_space);

      __gab_shpassoc(node, branch, sidx * 2);

      node = branch;

      continue;
    }

    // Leaf node at this hash index.
    uint64_t sidx = __gab_shpnth(node, midx);

    gab_value colliding_key = __gab_shpkey(node, sidx);
    uint64_t colliding_val = __gab_shpval(node, sidx);

    if (colliding_key == key) {
      __gab_shpassoc(node, val, (sidx * 2) + 1);
      goto done;
    }

    uint64_t ckey_midx = midx;

    do {
      // We promote the leaf at this node to a branch.
      __gab_shplprm(node, midx);
      uint64_t sidx = __gab_shpnth(node, midx);

      shift = __gab_shpishift(shift);

      midx = __gab_shpmidx(key, shift);
      ckey_midx = __gab_shpmidx(colliding_key, shift);

      // Create an empty internal node.
      // We need at least one space - if we only need another internal node.
      // If we are done colliding, we'll need a spot for each kvp.
      int64_t extra_space = 1 + (ckey_midx != midx);
      gab_value branch = __gab_shapenode(gab, 0, 0, 0, extra_space, nullptr);
      __gab_shpassoc(node, branch, sidx * 2);

      node = branch;

    } while (ckey_midx == midx);

    __gab_shplext(node, midx, key, val);
    __gab_shplext(node, ckey_midx, colliding_key, colliding_val);

    goto done;
  }

done:
  gab_verify(gab_shpfind(root, key) == val,
             "root shall find key => val after put completes");
  gab_verify(__gab_validatenode(root), "root shall contain valid tree");
  return root;
}

GAB_INTERNAL gab_value __gab_shptake(struct gab_triple gab, gab_value shape,
                                     gab_value key, gab_value replace,
                                     uint64_t replace_idx) {
  gab_precondition(gab_shplen(shape) != 0, "Can't take from shape of length 0");

  // O(log32(n)) remove given key
  gab_value shp = __gab_rshptake(gab, shape, key, 0);

  gab_assert(shp != gab_cinvalid, "__gab_shptake shall return a valid root");

  gab_verify(gab_shpfind(shp, key) == -1,
             "__gab_shptake shall remove target key");

  if (replace != key) {
    // O(log32(n)) replace last_key's value
    shp = __gab_shpput(gab, shp, replace, replace_idx);
  }

  gab_assert(shp != gab_cinvalid, "__gab_shpput shall return a valid root");

  return shp;
}

// A tiny hashtable used to de-duplicate keys we see in the input table.
GAB_INTERNAL uint64_t __gab_shpprepkeys(uint64_t stride, uint64_t len,
                                        gab_value *keys, gab_value *keys_out) {
  const uint64_t hashset_capacity = len * 2;
  gab_value *hashset = calloc(hashset_capacity, sizeof(gab_value));

  for (uint64_t i = 0; i < hashset_capacity; i++)
    hashset[i] = gab_cinvalid;

  uint64_t widx = 0;
  for (uint64_t ridx = 0; ridx < len; ridx++) {
    gab_value cur = keys[ridx * stride];
    uint64_t slot = cur % hashset_capacity;

    while (hashset[slot] != gab_cinvalid) {
      if (hashset[slot] == cur)
        goto skip;

      slot = (slot + 1) % hashset_capacity;
    }

    hashset[slot] = cur;
    keys_out[widx++] = cur;
  skip:
  }

  free(hashset);
  return widx;
};

GAB_INTERNAL gab_value __gab_shpchkdemote(gab_value shp, gab_value key,
                                          uint64_t n) {
  if (gab_valkind(shp) == kGAB_SHAPELIST && key != gab_number(n))
    GAB_VAL_TO_SHAPE(shp)->header.kind = kGAB_SHAPE;

  return shp;
}

// TODO @cgab @opt: Creates a lot of intermediate garbage shapes
GAB_INTERNAL gab_value __gab_nshape(struct gab_triple gab, uint64_t hash,
                                    uint64_t stride, uint64_t len,
                                    gab_value *keys) {
  gab_value s = __gab_shape(gab, hash, len, 0, 0, 0, 0, nullptr);

  for (uint64_t i = 0; i < len; i++) {
    s = __gab_shpchkdemote(__gab_shpput(gab, s, keys[i * stride], i),
                           keys[i * stride], i);
  }

  return s;
};

GAB_INTERNAL gab_value __gab_record(struct gab_triple gab, uint64_t len,
                                    int64_t adjustment, gab_value *data) {
  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, adjustment + len, kGAB_RECORD);

  gab_precondition(
      len + adjustment > 0,
      "Records must have size >= 0. Got len %lu and adjustment %li.", len,
      adjustment);

  self->len = len + adjustment;
  self->shape = gab_cinvalid;
  self->shift = GAB_PVEC_BITS;

  if (len) {
    gab_precondition(data, "data shall exist when len is not 0");
    uint64_t count = self->len < len ? self->len : len;
    memcpy(self->data, data, sizeof(gab_value) * count);
  }

  for (uint64_t i = len; i < self->len; i++)
    self->data[i] = gab_cinvalid;

  return __gab_obj(self);
}

GAB_INTERNAL gab_value __gab_recordnode(struct gab_triple gab, uint64_t len,
                                        int64_t adjustment, gab_value *data) {
  gab_precondition(
      len + adjustment > 0,
      "Record nodes must have size > 0. Got len %lu and adjustment %li.", len,
      adjustment);

  struct gab_orecnode *self = GAB_CREATE_FLEX_OBJ(
      gab_orecnode, gab_value, adjustment + len, kGAB_RECORDNODE);

  self->len = len + adjustment;

  if (len) {
    gab_precondition(data, "data shall exist when len is not 0");
    uint64_t count = self->len < len ? self->len : len;
    memcpy(self->data, data, sizeof(gab_value) * count);
  }

  for (uint64_t i = len; i < self->len; i++)
    self->data[i] = gab_cinvalid;

  return __gab_obj(self);
}

GAB_INTERNAL uint64_t __gab_reclen(gab_value rec) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE:
    return GAB_VAL_TO_RECNODE(rec)->len;
  case kGAB_RECORD:
    return GAB_VAL_TO_REC(rec)->len;
  case kGAB_PRIMITIVE:
    gab_precondition(rec == gab_cinvalid,
                     "The only valid primitive for a recnode is cinvalid");
    return 0;
  default:
    gab_unreachable("Invalid rec kind %d.", gab_valkind(rec));
    return 0;
  }
}

GAB_INTERNAL gab_value __gab_reccpy(struct gab_triple gab, gab_value r,
                                    int64_t adjustment) {
  switch (gab_valkind(r)) {
  case kGAB_RECORD: {
    struct gab_orec *n = GAB_VAL_TO_REC(r);

    if (n->len + adjustment < 0)
      return gab_cinvalid;

    struct gab_orec *nm =
        GAB_VAL_TO_REC(__gab_record(gab, n->len, adjustment, n->data));

    nm->shift = n->shift;
    nm->shape = n->shape;

    return __gab_obj(nm);
  }
  case kGAB_RECORDNODE: {
    struct gab_orecnode *n = GAB_VAL_TO_RECNODE(r);

    if (n->len + adjustment <= 0)
      return gab_cinvalid;

    return __gab_recordnode(gab, n->len, adjustment, n->data);
  }
    // Saw invalid
  case kGAB_PRIMITIVE:
    gab_precondition(r == gab_cinvalid,
                     "The only valid primitive for a recnode is cinvalid");
    return __gab_recordnode(gab, 0, 1, nullptr);
  default:
    break;
  }

  gab_unreachable("Invalid kind %d", gab_valkind(r));
  return gab_cinvalid;
}

GAB_INTERNAL gab_value __gab_recnth(gab_value rec, uint64_t n) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE: {
    struct gab_orecnode *r = GAB_VAL_TO_RECNODE(rec);
    gab_precondition(n < r->len, "Index %lu out of range %u", n, r->len);
    return r->data[n];
  }
  case kGAB_RECORD: {
    struct gab_orec *r = GAB_VAL_TO_REC(rec);
    gab_precondition(n < r->len, "Index %lu out of range %u", n, r->len);
    return r->data[n];
  }
  default:
    break;
  }

  gab_unreachable("Invalid kind %d", gab_valkind(rec));
  return gab_cinvalid;
}

GAB_INTERNAL void __gab_recassoc(gab_value rec, gab_value v, uint64_t i) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE: {
    struct gab_orecnode *r = GAB_VAL_TO_RECNODE(rec);
    gab_precondition(i < r->len, "Index %lu out of range %u", i, r->len);
    r->data[i] = v;
    return;
  }
  case kGAB_RECORD: {
    struct gab_orec *r = GAB_VAL_TO_REC(rec);
    gab_precondition(i < r->len, "Index %lu out of range %u", i, r->len);
    r->data[i] = v;
    return;
  }
  default:
    break;
  }

  gab_unreachable("Invalid kind %d", gab_valkind(rec));
}

/*
 * Implemented with a recursive algorithm bc its easier.
 * I'd *like* it to be procedural, to line up with other algorithms.
 */
GAB_INTERNAL gab_value __gab_rectake(struct gab_triple gab, int level,
                                     gab_value node, uint64_t i, gab_value v,
                                     gab_value *vout) {
  uint64_t idx = (i >> level) & GAB_PVEC_MASK;
  bool is_leaf = !level;

  bool is_converged = vout && (v != gab_cinvalid);

  // Will return gab_cinvalid if a recnode would have 0 elements.
  if (is_leaf) {
    // Converted rightmost & chosen
    if (is_converged) {
      *vout = __gab_recnth(node, __gab_reclen(node) - 1);
      gab_value leaf = __gab_reccpy(gab, node, -1);
      if (idx < __gab_reclen(leaf))
        __gab_recassoc(leaf, v, idx);
      return leaf;
    }

    // Diverged Rightmost
    if (vout) {
      *vout = __gab_recnth(node, __gab_reclen(node) - 1);
      return __gab_reccpy(gab, node, -1);
    }

    // Diverged Chosen
    gab_precondition(v != gab_cinvalid, "Invalid recusive call");
    gab_value leaf = __gab_reccpy(gab, node, 0);
    __gab_recassoc(leaf, v, idx);
    return leaf;
  }

  // Otherwise, we recurse on the rightmost node.

  uint64_t rightmost_idx = __gab_reclen(node) - 1;

  bool diverging = idx != rightmost_idx;

  // Converged
  if (is_converged && !diverging) {
    gab_value child = __gab_rectake(gab, level - GAB_PVEC_BITS,
                                    __gab_recnth(node, idx), i, v, vout);

    // If this recursion is empty, we return a copy of ourselves with one node
    // trimmed. This may itself return an empty node.
    if (child == gab_cinvalid)
      return __gab_reccpy(gab, node, -1);

    // If this recursion is not empty, we copy our node and replace rightmost
    // child.
    gab_value newnode = __gab_reccpy(gab, node, 0);
    __gab_recassoc(newnode, child, rightmost_idx);
    return newnode;
  }

  // Diverging now
  if (is_converged && diverging) {

    gab_value rightmost_child =
        __gab_rectake(gab, level - GAB_PVEC_BITS,
                      __gab_recnth(node, rightmost_idx), i, gab_cinvalid, vout);

    gab_precondition(v != gab_cinvalid,
                     "Cannot recurse into chosen branch with invalid v");
    gab_value chosen_child = __gab_rectake(
        gab, level - GAB_PVEC_BITS, __gab_recnth(node, idx), i, v, nullptr);

    // Shrink if we saw an empty rightmost node.
    gab_value newnode =
        __gab_reccpy(gab, node, -1 * (rightmost_child == gab_cinvalid));

    // Assoc our new chosen child
    __gab_recassoc(newnode, chosen_child, idx);

    // Assoc our new rightmost child, if they exist
    if (rightmost_child != gab_cinvalid)
      __gab_recassoc(newnode, rightmost_child, rightmost_idx);

    return newnode;
  }

  // Diverged rightmost branch
  if (vout) {
    gab_value rightmost_child =
        __gab_rectake(gab, level - GAB_PVEC_BITS,
                      __gab_recnth(node, rightmost_idx), i, gab_cinvalid, vout);

    // Shrink if we saw an empty rightmost node.
    gab_value newnode =
        __gab_reccpy(gab, node, -1 * (rightmost_child == gab_cinvalid));

    if (rightmost_child != gab_cinvalid)
      __gab_recassoc(newnode, rightmost_child, rightmost_idx);

    return newnode;
  }

  // Diverged chosen branch
  gab_precondition(v != gab_cinvalid, "Invalid recusive call");
  gab_value chosen_child = __gab_rectake(
      gab, level - GAB_PVEC_BITS, __gab_recnth(node, idx), i, v, nullptr);

  gab_value newnode = __gab_reccpy(gab, node, 0);

  // Assoc our new chosen child
  __gab_recassoc(newnode, chosen_child, idx);
  return newnode;
}

GAB_API gab_value gab_uvrecat(gab_value rec, uint64_t i) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));

  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  gab_value node = rec;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;

    gab_value next_node = __gab_recnth(node, idx);

    gab_precondition(gab_valkind(next_node) == kGAB_RECORDNODE ||
                         gab_valkind(next_node) == kGAB_RECORD,
                     "Invalid kind %d", gab_valkind(next_node));

    node = next_node;
  }

  node = __gab_recnth(node, i & GAB_PVEC_MASK);

  gab_assert(gab_valkind(node) != kGAB_RECORDNODE, "Final node shall be leaf");

  return node;
}

GAB_INTERNAL bool __gab_recneedsspace(gab_value rec, uint64_t i) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));

  struct gab_orec *r = GAB_VAL_TO_REC(rec);
  uint64_t idx = (i >> r->shift) & GAB_PVEC_MASK;
  return idx >= r->len;
}

GAB_INTERNAL gab_value __gab_recsetshp(gab_value rec, gab_value shp) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));

  struct gab_orec *r = GAB_VAL_TO_REC(rec);
  r->shape = shp;
  return rec;
}

GAB_INTERNAL gab_value __gab_recput(struct gab_triple gab, gab_value rec,
                                    gab_value v, uint64_t i) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));

  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  gab_value node = rec;
  gab_value root = node;
  gab_value path = root;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;
    uint64_t nidx = (i >> (level - GAB_PVEC_BITS)) & GAB_PVEC_MASK;

    if (idx < __gab_reclen(node))
      node = __gab_reccpy(gab, __gab_recnth(node, idx),
                          nidx >= __gab_reclen(__gab_recnth(node, idx)));
    else
      node = __gab_recordnode(gab, 0, 1, nullptr);

    __gab_recassoc(path, node, idx);
    path = node;
  }

  gab_assert(node != gab_cinvalid, "Node shall exist");
  __gab_recassoc(node, v, i & GAB_PVEC_MASK);
  return root;
}

GAB_INTERNAL void __gab_mrecput(struct gab_triple gab, gab_value rec,
                                gab_value v, uint64_t i) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  gab_precondition(i < gab_reclen(rec),
                   "put shall be within range %lu. Got %lu", gab_reclen(rec),
                   i);

  gab_value node = rec;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;

    node = __gab_recnth(node, idx);
  }

  gab_assert(node != gab_cinvalid, "Node shall exist");
  __gab_recassoc(node, v, i & GAB_PVEC_MASK);

  return;
}

GAB_INTERNAL gab_value __gab_reccons(struct gab_triple gab, gab_value rec,
                                     gab_value v, gab_value shp) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  uint64_t i = gab_reclen(rec);

  // Overflow root
  if ((i >> GAB_PVEC_BITS) >= ((uint64_t)1 << r->shift)) {
    gab_value new_root = __gab_record(gab, 1, 1, &rec);

    struct gab_orec *new_r = GAB_VAL_TO_REC(new_root);

    new_r->shape = shp;
    new_r->shift = r->shift + 5;

#ifndef NDEBUG
    for (uint64_t j = 0; j < i; j++)
      gab_uvrecat(new_root, j);
#endif

    __gab_recput(gab, new_root, v, i);

#ifndef NDEBUG
    for (uint64_t j = 0; j < i; j++)
      gab_uvrecat(new_root, j);
#endif

    return new_root;
  }

  gab_value record = __gab_recsetshp(
      __gab_recput(gab, __gab_reccpy(gab, rec, __gab_recneedsspace(rec, i)), v,
                   i),
      shp);

#ifndef NDEBUG
  for (uint64_t j = 0; j < i; j++)
    gab_uvrecat(record, j);
#endif

  return record;
}

GAB_API gab_value gab_recput(struct gab_triple gab, gab_value rec,
                             gab_value key, gab_value val) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));

  uint64_t idx = gab_recfind(rec, key);

  gab_gclock(gab);

  if (idx == -1) {
    gab_value newshp = gab_shpwith(gab, gab_recshp(rec), key);

    gab_value result = __gab_reccons(gab, rec, val, newshp);

    return gab_gcunlock(gab), result;
  }

  gab_value result = __gab_recput(
      gab, __gab_reccpy(gab, rec, __gab_recneedsspace(rec, idx)), val, idx);

  return gab_gcunlock(gab), result;
}

GAB_API gab_value gab_rectake(struct gab_triple gab, gab_value rec,
                              gab_value key, gab_value *out_val) {
  /*
   * TODO @cgab @perf: This can be optimized.
   *
   * There is no need to allocate a new record here, we can reuse the original
   * record with a *new shape*.
   *
   * This may need to include *tombstone* kind of value, which we can replace
   * the original key in the shape with a gab_ctombstone. This will need to be
   * skipped in a lot of other gab_shape logic.
   */
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));

  uint64_t idx = gab_recfind(rec, key);

  if (idx == -1) {
    if (out_val)
      *out_val = gab_nil;

    return rec;
  }

  gab_gclock(gab);

  gab_value val = gab_uvrecat(rec, idx);

  if (out_val)
    *out_val = val;

  // gab_value r = dissoc(gab, reccpy(gab, rec, 0), idx);
  if (gab_reclen(rec) == 1)
    return gab_gcunlock(gab), gab_erecord(gab);

  gab_value dissoc_out;
  gab_value result =
      __gab_rectake(gab, GAB_VAL_TO_REC(rec)->shift, rec, idx,
                    gab_uvrecat(rec, gab_reclen(rec) - 1), &dissoc_out);

  gab_value s = gab_shpwithout(gab, gab_recshp(rec), key);

  result = __gab_recsetshp(result, s);

  return gab_gcunlock(gab), result;
}

GAB_API gab_value gab_nlstpush(struct gab_triple gab, gab_value list,
                               uint64_t len, gab_value *values) {
  gab_precondition(gab_valkind(list) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(list));

  uint64_t start = gab_reclen(list);

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    gab_value key = gab_number(start + i);
    gab_value val = values[i];
    list = gab_recput(gab, list, key, val);
  }

  return gab_gcunlock(gab), list;
}

GAB_API gab_value gab_urecput(struct gab_triple gab, gab_value rec, uint64_t i,
                              gab_value v) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(rec));

  gab_precondition(i < gab_reclen(rec), "Index %lu shall be within range %lu\n",
                   i, gab_reclen(rec));

  gab_gclock(gab);

  gab_value result = __gab_recput(gab, __gab_reccpy(gab, rec, 0), v, i);

  return gab_gcunlock(gab), result;
}

GAB_INTERNAL uint64_t __gab_pveclen(uint64_t n, uint64_t shift) {
  // IDK why this is here.
  if (n)
    n--;

  return ((n >> shift) & GAB_PVEC_MASK) + 1;
}

GAB_INTERNAL void __gab_recfillchildren(struct gab_triple gab, gab_value rec,
                                        uint64_t shift, uint64_t n,
                                        uint64_t len, bool rightmost) {
  gab_precondition(len > 0, "Shall not fill children for record with len 0");

  if (shift == 0)
    return;

  for (uint64_t l = 0; l < len - rightmost; l++) {
    gab_value lhs_child = __gab_recordnode(gab, 0, GAB_PVEC_SIZE, nullptr);

    __gab_recfillchildren(gab, lhs_child, shift - GAB_PVEC_BITS, n,
                          GAB_PVEC_SIZE, false);

    __gab_recassoc(rec, lhs_child, l);
  }

  if (rightmost) {
    uint64_t rhs_childlen = __gab_pveclen(n, shift - GAB_PVEC_BITS);

    gab_value rhs_child = __gab_recordnode(gab, 0, rhs_childlen, nullptr);

    __gab_recfillchildren(gab, rhs_child, shift - GAB_PVEC_BITS, n,
                          rhs_childlen, true);

    __gab_recassoc(rec, rhs_child, len - 1);
  }
}

GAB_INTERNAL uint64_t __gab_pvecshift(uint64_t n) {
  uint64_t shift = 0;

  // IDK why this is here.
  if (n)
    n--;

  while ((n >> GAB_PVEC_BITS) >= (1 << shift)) {
    shift += 5;
  }

  return shift;
}

GAB_API gab_value gab_shptorec(struct gab_triple gab, gab_value shp) {
  gab_precondition(gab_valkind(shp) == kGAB_SHAPE ||
                       gab_valkind(shp) == kGAB_SHAPELIST,
                   "Invalid kind %d", gab_valkind(shp));

  uint64_t len = gab_shplen(shp);

  gab_gclock(gab);

  uint64_t shift = __gab_pvecshift(len);

  uint64_t rootlen = __gab_pveclen(len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = shp;
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  if (len) {
    __gab_recfillchildren(gab, res, shift, len, rootlen, true);

    for (uint64_t i = 0; i < len; i++)
      __gab_mrecput(gab, res, gab_nil, i);
  }

  return gab_gcunlock(gab), res;
}

GAB_API gab_value gab_recordfrom(struct gab_triple gab, gab_value shape,
                                 uint64_t stride, uint64_t len,
                                 gab_value *vals) {
  if (shape == gab_ctimeout || shape == gab_cinvalid)
    return shape;

  gab_precondition(gab_valkind(shape) == kGAB_SHAPE ||
                       gab_valkind(shape) == kGAB_SHAPELIST,
                   "Invalid kind %", gab_valkind(shape));

  gab_gclock(gab);

  uint64_t real_len = gab_shplen(shape);

  uint64_t shift = __gab_pvecshift(real_len);

  uint64_t rootlen = __gab_pveclen(real_len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = shape;
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  len = real_len < len ? real_len : len;

  if (real_len) {
    __gab_recfillchildren(gab, res, shift, real_len, rootlen, true);

    uint64_t real_i = 0;

    /* Use all provided values in array */
    for (uint64_t i = 0; i < len; i++) {
      __gab_mrecput(gab, res, vals[i * stride], real_i++);
    }

    /* Fill remaining with nil */
    for (uint64_t i = len; i < real_len; i++) {
      __gab_mrecput(gab, res, gab_nil, real_i++);
    }

    gab_precondition(
        real_len == real_i,
        "Shall not encounter duplicate keys in construction of shape. (%lu, "
        "expected %lu)",
        real_i, real_len);
  }

  return gab_gcunlock(gab), res;
}

GAB_API gab_value gab_record(struct gab_triple gab, uint64_t stride,
                             uint64_t len, gab_value *keys, gab_value *vals) {
  gab_gclock(gab);

  // TODO @cgab @bug: Handle duplicate values somehow.
  gab_value shp = gab_shape(gab, stride, len, keys);

  if (shp == gab_ctimeout || shp == gab_cinvalid)
    return gab_gcunlock(gab), shp;

  uint64_t actual_len = gab_shplen(shp);

  if (actual_len < len) {
    // In the slow case where we saw duplicate keys
    gab_value dedup_values[actual_len];

    // O(n) iterate through each key
    for (uint64_t i = 0; i < len; i++) {
      gab_value key = keys[i * stride];

      // O(log32(n)) find the index of the nth key
      uint64_t idx = gab_shpfind(shp, key);

      // Overwite the value at that index with nth val
      dedup_values[idx] = vals[i * stride];
    }

    gab_value rec = gab_recordfrom(gab, shp, 1, actual_len, dedup_values);
    return gab_gcunlock(gab), rec;
  }

  gab_value rec = gab_recordfrom(gab, shp, stride, len, vals);

  return gab_gcunlock(gab), rec;
}

GAB_API gab_value gab_recshp(gab_value record) {
  gab_precondition(gab_valkind(record) == kGAB_RECORD, "Invalid kind %d",
                   gab_valkind(record));
  return GAB_VAL_TO_REC(record)->shape;
};

GAB_INTERNAL gab_value __gab_recnthamongst(uint64_t n, uint64_t len,
                                           gab_value records[static len]) {
  gab_precondition(len > 0, "Shall have len > 0");

  uint64_t r = 0;
  uint64_t i = 0;

  while (r < len && n >= i + gab_reclen(records[r]))
    i += gab_reclen(records[r++]);

  return gab_uvrecat(records[r], n - i);
}

GAB_API gab_value gab_nlstcat(struct gab_triple gab, uint64_t len,
                              gab_value records[static len]) {
  if (len == 0)
    return gab_erecord(gab);

  uint64_t total_len = 0;

  for (uint64_t i = 0; i < len; i++)
    total_len += gab_reclen(records[i]);

  if (total_len == 0)
    return gab_erecord(gab);

  gab_value total_keys[total_len];
  for (uint64_t i = 0; i < total_len; i++)
    total_keys[i] = gab_number(i);

  gab_gclock(gab);

  uint64_t shift = __gab_pvecshift(total_len);

  uint64_t rootlen = __gab_pveclen(total_len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = gab_shape(gab, 1, total_len, total_keys);
  self->shift = shift;
  self->len = rootlen;

  gab_assert(total_len == gab_shplen(self->shape),
             "Total length shall match constructed shape length");

  gab_value res = __gab_obj(self);

  if (total_len) {
    __gab_recfillchildren(gab, res, shift, total_len, rootlen, true);

    for (uint64_t i = 0; i < total_len; i++)
      __gab_mrecput(gab, res, __gab_recnthamongst(i, total_len, records), i);
  }

  return gab_gcunlock(gab), res;
}

GAB_API gab_value gab_nreccat(struct gab_triple gab, uint64_t len,
                              gab_value *records) {

  if (len == 0)
    return gab_recordof(gab);

  gab_gclock(gab);

  gab_value shapes[len];
  for (uint64_t i = 0; i < len; i++)
    shapes[i] = gab_recshp(records[i]);

  gab_value new_shp = gab_nshpcat(gab, len, shapes);

  uint64_t total_len = gab_shplen(new_shp);
  uint64_t shift = __gab_pvecshift(total_len);
  uint64_t rootlen = __gab_pveclen(total_len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = new_shp;
  self->shift = shift;
  self->len = rootlen;

  gab_assert(total_len == gab_shplen(self->shape),
             "Total length shall match constructed shape length");

  gab_value res = __gab_obj(self);

  if (total_len) {
    __gab_recfillchildren(gab, res, shift, total_len, rootlen, true);

    for (uint64_t i = 0; i < total_len; i++) {
      gab_value key = gab_ushpat(new_shp, i);
      for (uint64_t j = 0; j < len; j++) {
        gab_value rec = records[j];
        gab_value val = gab_recat(rec, key);
        if (val != gab_cundefined)
          __gab_mrecput(gab, res, val, i);
      }
    }
  }

  return gab_gcunlock(gab), res;
}

GAB_API gab_value gab_nvstring(struct gab_triple gab, uint64_t n,
                               gab_value *data) {
  if (n == 0)
    return gab_string(gab, "");

  gab_value str = gab_valintostr(gab, data[0]);

  for (uint64_t i = 1; i < n; i++)
    str = gab_strcat(gab, str, gab_valintostr(gab, data[i]));

  return str;
}

GAB_API gab_value gab_nvbinary(struct gab_triple gab, uint64_t n,
                               gab_value *data) {
  if (n == 0)
    return gab_binary(gab, (uint8_t *)"");

  gab_value str = gab_valintobin(gab, data[0]);

  for (uint64_t i = 1; i < n; i++)
    str = gab_bincat(gab, str, gab_valintobin(gab, data[i]));

  return str;
}

GAB_API gab_value gab_list(struct gab_triple gab, uint64_t stride, uint64_t len,
                           gab_value *values) {
  if (!len)
    return gab_record(gab, 0, 0, nullptr, nullptr);

  gab_value keys[len * stride];

  for (uint64_t i = 0; i < len; i++)
    keys[i * stride] = gab_number(i);

  gab_value v = gab_record(gab, stride, len, keys, values);

  return v;
}

// TODO @cgab @opt: See gab_tnstring. Same stuff applies.
GAB_API gab_value gab_tshape(struct gab_triple gab, uint64_t stride,
                             uint64_t len, gab_value *data) {
  gab_value newdata[len + 1];
  uint64_t newlen = __gab_shpprepkeys(stride, len, data, newdata);

  // TODO @cgab @bug: Handle duplicate keys correctly.
  uint64_t hash = hash_words(newlen, newdata);

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  struct gab_oshape *interned =
      __gab_egshpfind(gab.eg, hash, 1, newlen, newdata);

  mtx_unlock(&gab.eg->gc_mtx);

  if (interned)
    return __gab_obj(interned);

  gab_gclock(gab);

  gab_value s = __gab_nshape(gab, hash, 1, newlen, newdata);

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_gcunlock(gab), gab_ctimeout;
  case thrd_error:
    return gab_gcunlock(gab), gab_cinvalid;
  }

  d_shapes_insert(&gab.eg->shapes, GAB_VAL_TO_SHAPE(s), 0);

  mtx_unlock(&gab.eg->gc_mtx);

  // TODO @cgab @opt: Find more efficient fix
  // When creating and interning shapes, we need to explicitly increment
  // This shape so that all of its children are acknowledged.
  // This bug is present because shapes are persistent and share nodes.
  // If a shape is created which shares a node with
  // another shape created in an earlier epoch, and *that* shape is collected,
  // the shared node will be collected out from underneath the new shape.
  // this inc/dec pattern guarantees that each shape *acknowledges ownership* of
  // its children. This is a problem because the intern table holds *weak
  // references* to its shapes. So a shape can be re-used from the table without
  // ever having been incremented (and therefore, kept its children alive).

  // These must occur after unlocking the mutex, as they may trigger a
  // collection.
  gab_iref(gab, s);
  gab_dref(gab, s);

  return gab_gcunlock(gab), s;
}

// TODO @opt @cgab: Don't hash in tshape, we loop that fn. Same for str.
GAB_API gab_value gab_shape(struct gab_triple gab, uint64_t stride,
                            uint64_t len, gab_value *data) {
  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_IGN:
      break;
    case sGAB_TERM:
      // break;
      return gab_cinvalid;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    }

    gab_value shp = gab_tshape(gab, stride, len, data);

    if (shp == gab_cinvalid)
      return shp;

    if (shp == gab_ctimeout)
      continue;

    gab_assert(gab_valkind(shp) == kGAB_SHAPE ||
                   gab_valkind(shp) == kGAB_SHAPELIST,
               "Invalid kind: %d", gab_valkind(shp));

    return shp;
  }
}

GAB_API uint64_t gab_shplen(gab_value shp) {
  gab_precondition(gab_valkind(shp) == kGAB_SHAPE ||
                       gab_valkind(shp) == kGAB_SHAPELIST,
                   "Invalid kind %d", gab_valkind(shp));

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);
  return s->len;
}

GAB_API gab_value gab_nshpcat(struct gab_triple gab, uint64_t len,
                              gab_value shapes[static len]) {
  gab_precondition(len > 0, "Cannot concat 0 shapes");
  gab_value shp = shapes[0];

  gab_gclock(gab);

  for (uint64_t i = 1; i < len; i++)
    for (uint64_t k = 0; k < gab_shplen(shapes[i]); k++)
      shp = gab_shpwith(gab, shp, gab_ushpat(shapes[i], k));

  return gab_gcunlock(gab), shp;
}

struct popkey_res {
  uint64_t found;
  bool promotable;
};

GAB_INTERNAL struct popkey_res
__gab_shppopulate_keys(gab_value node, gab_value skip, gab_value replace,
                       uint64_t found, bool promotable, gab_value *out) {
  for (uint64_t midx = 0; midx < 32; midx++) {
    uint32_t sidx = __gab_shpnth(node, midx);

    if (__gab_shpisn(node, midx)) {
      // We have a node
      if (__gab_shpisl(node, midx)) {
        // We have a leaf!
        uint64_t sidx = __gab_shpnth(node, midx);

        gab_value key = __gab_shpkey(node, sidx);
        uint64_t n = __gab_shpval(node, sidx);

        if (key == skip)
          (found = n), (out[n] = replace);
        else
          out[n] = key;

        promotable = promotable && (out[n] == gab_number(n));

        continue;
      }

      // Recurse into branch
      gab_value c = __gab_shpkey(node, sidx);
      struct popkey_res res =
          __gab_shppopulate_keys(c, skip, replace, found, promotable, out);
      found = found != -1 ? found : res.found;
      promotable = promotable && res.promotable;
    }
  }

  return (struct popkey_res){found, promotable};
};

/*
 * This needs to mimic the swap-and-pop that records do to actually pop values
 */
GAB_API gab_value gab_tshpwithout(struct gab_triple gab, gab_value shape,
                                  gab_value key) {

  // We may be passed an empty shape.
  uint64_t len = gab_shplen(shape);

  if (!len)
    return gab_shape(gab, 0, 0, nullptr);

  gab_value last_key = gab_ushpat(shape, len - 1);

  gab_value newdata[len];
  struct popkey_res res =
      __gab_shppopulate_keys(shape, key, last_key, -1, true, newdata);

  bool found = res.found != -1;

  uint64_t newlen = len - found;

  if (!found)
    return shape;

  uint64_t hash = hash_words(newlen, newdata);

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  struct gab_oshape *interned =
      __gab_egshpfind(gab.eg, hash, 1, newlen, newdata);

  mtx_unlock(&gab.eg->gc_mtx);

  if (interned)
    return __gab_obj(interned);

  gab_gclock(gab);

  gab_value new_shape = __gab_shptake(gab, shape, key, last_key, res.found);

  struct gab_oshape *self = GAB_VAL_TO_SHAPE(new_shape);
  self->hash = hash;
  self->len--;

  GAB_VAL_TO_SHAPE(new_shape)->header.kind =
      res.promotable ? kGAB_SHAPELIST : kGAB_SHAPE;

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    // fprintf(stderr, "(%i) TAKE BUSY DROP_ON_FLOOR %p\n", gab.wkid, self);
    return gab_gcunlock(gab), gab_ctimeout;
  case thrd_error:
    // fprintf(stderr, "(%i) TAKE ERR DROP_ON_FLOOR %p\n", gab.wkid, self);
    return gab_gcunlock(gab), gab_cinvalid;
  }

  d_shapes_insert(&gab.eg->shapes, self, 0);
  // fprintf(stderr, "(%i) INSERT %p\n", gab.wkid, self);

  mtx_unlock(&gab.eg->gc_mtx);

  // These must occur after unlocking the mutex, as they may trigger a
  // collection.
  gab_iref(gab, new_shape);
  gab_dref(gab, new_shape);

  return gab_gcunlock(gab), new_shape;
}

GAB_API gab_value gab_shpwithout(struct gab_triple gab, gab_value shape,
                                 gab_value key) {
  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_IGN:
      break;
    case sGAB_TERM:
      // break;
      return gab_cinvalid;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    }

    gab_value shp = gab_tshpwithout(gab, shape, key);

    if (shp == gab_cinvalid)
      return shp;

    if (shp == gab_ctimeout)
      continue;

    gab_assert(gab_valkind(shp) == kGAB_SHAPE ||
                   gab_valkind(shp) == kGAB_SHAPELIST,
               "Invalid kind");

    return shp;
  }
}

// TODO @cgab @opt: See gab_tnstring. Same stuff applies.
GAB_API gab_value gab_tshpwith(struct gab_triple gab, gab_value shp,
                               gab_value key) {
  gab_precondition(gab_valkind(shp) == kGAB_SHAPE ||
                       gab_valkind(shp) == kGAB_SHAPELIST,
                   "Invalid kind");

  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);

  uint64_t idx = gab_shpfind(shp, key);
  if (idx != -1)
    return shp;

  uint64_t hash = continue_hash_words(s->hash, 1, &key);

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    return gab_ctimeout;
  case thrd_error:
    return gab_cinvalid;
  }

  struct gab_oshape *interned =
      __gab_legshpfind(gab.eg, hash, s->len, shp, key);

  mtx_unlock(&gab.eg->gc_mtx);

  if (interned)
    return __gab_obj(interned);

  gab_gclock(gab);

  gab_value new_shape = __gab_shpput(gab, shp, key, s->len);

  struct gab_oshape *self = GAB_VAL_TO_SHAPE(new_shape);
  self->hash = hash;
  self->len++;

  // demote to a SHAPE if we were shapelist, but put the wrong key.
  new_shape = __gab_shpchkdemote(new_shape, key, s->len);

  switch (mtx_trylock(&gab.eg->gc_mtx)) {
  case thrd_success:
    break;
  case thrd_busy:
    // fprintf(stderr, "(%i) PUT BUSY DROP_ON_FLOOR %p\n", gab.wkid, self);
    return gab_gcunlock(gab), gab_ctimeout;
  case thrd_error:
    // fprintf(stderr, "(%i) PUT ERR DROP_ON_FLOOR %p\n", gab.wkid, self);
    return gab_gcunlock(gab), gab_cinvalid;
  }

  d_shapes_insert(&gab.eg->shapes, self, 0);
  // fprintf(stderr, "(%i) INSERT %p\n", gab.wkid, self);

  mtx_unlock(&gab.eg->gc_mtx);

  // These must occur after unlocking the mutex, as they may trigger a
  // collection.
  gab_iref(gab, new_shape);
  gab_dref(gab, new_shape);

  return gab_gcunlock(gab), new_shape;
}

GAB_API gab_value gab_shpwith(struct gab_triple gab, gab_value shape,
                              gab_value data) {
  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_IGN:
      break;
    case sGAB_TERM:
      // break;
      return gab_cinvalid;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    }

    gab_value shp = gab_tshpwith(gab, shape, data);

    if (shp == gab_cinvalid)
      return shp;

    if (shp == gab_ctimeout)
      continue;

    gab_assert(gab_valkind(shp) == kGAB_SHAPE ||
                   gab_valkind(shp) == kGAB_SHAPELIST,
               "Invalid kind");

    return shp;
  }
}

GAB_INTERNAL gab_value __gab_fibsetup(struct gab_triple gab,
                                      struct gab_ofiber *self) {
  struct gab_vm *vm = &self->vm;

  memcpy(self->virtual_frame_bc,
         // TODO @cgab @bug: Primitives don't handle being tailcalled well.
         &(uint8_t[]){
             OP_SEND,
             // These two bytes make up a short argument, with the highest bit set.
             fHAVE_TAIL,
             0,
             // This bit is used to determine if the send should tailcall.
             // The rest of the bits are for the k offset.
             OP_RETURN,
         },
         sizeof(self->virtual_frame_bc));

  memcpy(self->virtual_frame_ks,
         &(gab_value[]){
             self->data[0],
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
         },
         sizeof(self->virtual_frame_ks));

  vm->ip = self->virtual_frame_bc;
  vm->kb = self->virtual_frame_ks;

  return __gab_obj(self);
}

GAB_API gab_value gab_fiber(struct gab_triple gab, struct gab_fiber_argt args) {
  gab_precondition(gab_valkind(args.message) == kGAB_MESSAGE, "Invalid kind");

  struct gab_ofiber *self =
      GAB_CREATE_FLEX_OBJ(gab_ofiber, gab_value, args.argc + 2, kGAB_FIBER);

  self->len = args.argc + 2;

  if (args.argc) {
    gab_precondition(args.argv,
                     "When argc is non-zero, argv shall be non-zero as well");
    memcpy(self->data + 2, args.argv, args.argc * sizeof(gab_value));
  }

  self->data[0] = args.message;
  self->data[1] = args.receiver;

  self->flags = gab.flags | args.flags;

  // self->vm.sb = self->vm.initial;
  self->vm.sp = self->vm.sb;

  self->vm.sp += 3;          // Return frame data
  self->vm.fp = self->vm.sp; // Frame pointer

  // Setup main and args
  *self->vm.sp++ = args.receiver; // self
  for (uint8_t i = 0; i < args.argc; i++)
    *self->vm.sp++ = args.argv[i]; // i'th argument

  *self->vm.sp = args.argc + 1; // have

  self->vm.ip = nullptr;
  self->res_env = gab_cinvalid;

  return __gab_fibsetup(gab, self);
}

GAB_API struct gab_vm *gab_fibvm(gab_value fiber) {
  gab_precondition(gab_valkind(fiber) >= kGAB_FIBER &&
                       gab_valkind(fiber) <= kGAB_FIBERRUNNING,
                   "Invalid Kind");
  return &GAB_VAL_TO_FIBER(fiber)->vm;
}

GAB_API union gab_value_pair gab_tfibawait(struct gab_triple gab, gab_value f,
                                           uint64_t tries) {
  gab_precondition(gab_valkind(f) >= kGAB_FIBER &&
                       gab_valkind(f) <= kGAB_FIBERRUNNING,
                   "Invalid kind");
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  uint64_t sofar = 0;

  while (fiber->header.kind != kGAB_FIBERDONE) {
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_union_cinvalid;
    default:
      break;
    }

    sofar++;
    if (sofar > tries)
      return (union gab_value_pair){.status = gab_ctimeout, .vresult = f};
  }

  return fiber->res_values;
}

GAB_API union gab_value_pair gab_fibawait(struct gab_triple gab, gab_value f) {
  // When not on the main thread, just block
  if (gab.wkid != 1)
    return gab_tfibawait(gab, f, -1);

  // When already running gab code, just block
  if (__gab_jbisrunning(gab, gab.eg->jobs + gab.wkid))
    return gab_tfibawait(gab, f, -1);

  // At this point, we are safe to do some work.
  for (;;) {
    union gab_value_pair res = gab_tfibawait(gab, f, 1);

    if (res.status != gab_ctimeout)
      return res;

    if (!__gab_jbisalive(gab, gab.wkid) ||
        !__gab_jbstep(gab, gab.eg->jobs + gab.wkid))
      return __gab_jbbail(gab, gab.eg->jobs + gab.wkid),
             gab_tfibawait(gab, f, 1);
  }

  gab_unreachable("Should not break out of above loop");
}

GAB_API void *gab_fibmalloc(gab_value f, uint64_t n) {
  gab_precondition(gab_valkind(f) >= kGAB_FIBER &&
                       gab_valkind(f) <= kGAB_FIBERRUNNING,
                   "Invalid kind");
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  v_uint8_t_cap(&fiber->allocator, fiber->len + n + 1);

  fiber->allocator.len += n;
  uint8_t *ptr = v_uint8_t_ref_at(&fiber->allocator, fiber->allocator.len - n);
  return ptr;
}

GAB_API uint64_t gab_fibpush(gab_value f, uint8_t b) {
  gab_precondition(gab_valkind(f) >= kGAB_FIBER &&
                       gab_valkind(f) <= kGAB_FIBERRUNNING,
                   "Invalid kind");
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  return v_uint8_t_push(&fiber->allocator, b);
}

GAB_API uint64_t gab_wfibpush(gab_value f, uint64_t w) {
  gab_precondition(gab_valkind(f) >= kGAB_FIBER &&
                       gab_valkind(f) <= kGAB_FIBERRUNNING,
                   "Invalid kind");
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  uint64_t idx = v_uint8_t_push(&fiber->allocator, w & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 8) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 16) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 24) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 32) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 40) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 48) & 0xff);
  v_uint8_t_push(&fiber->allocator, (w >> 56) & 0xff);
  return idx;
}

GAB_API void *gab_fibat(gab_value f, uint64_t n) {
  gab_precondition(gab_valkind(f) >= kGAB_FIBER &&
                       gab_valkind(f) <= kGAB_FIBERRUNNING,
                   "Invalid kind");
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  return v_uint8_t_ref_at(&fiber->allocator, n);
}

GAB_API uint64_t gab_fibsize(gab_value f) {
  gab_precondition(gab_valkind(f) >= kGAB_FIBER &&
                       gab_valkind(f) <= kGAB_FIBERRUNNING,
                   "Invalid kind");
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  return fiber->allocator.len;
}

GAB_API void gab_fibclear(gab_value f) {
  gab_precondition(gab_valkind(f) >= kGAB_FIBER &&
                       gab_valkind(f) <= kGAB_FIBERRUNNING,
                   "Invalid kind");
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  fiber->allocator.len = 0;
}

GAB_API gab_value gab_fibawaite(struct gab_triple gab, gab_value f) {
  gab_precondition(gab_valkind(f) >= kGAB_FIBER &&
                       gab_valkind(f) <= kGAB_FIBERRUNNING,
                   "Invalid kind");

  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  while (fiber->header.kind != kGAB_FIBERDONE)
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      break;
    }

  return fiber->res_env;
}

GAB_API gab_value gab_channel(struct gab_triple gab) {
  struct gab_ochannel *self = GAB_CREATE_OBJ(gab_ochannel, kGAB_CHANNEL);

  atomic_init(&self->data, nullptr);
  atomic_init(&self->spinlock, 0);
  atomic_init(&self->len, 0);

  return __gab_obj(self);
}

GAB_API void gab_chnclose(gab_value c) {
  gab_precondition(gab_valkind(c) >= kGAB_CHANNEL &&
                       gab_valkind(c) <= kGAB_CHANNELCLOSED,
                   "Invalid kind");

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  channel->header.kind = kGAB_CHANNELCLOSED;
}

GAB_API bool gab_chnisclosed(gab_value c) {
  gab_precondition(gab_valkind(c) >= kGAB_CHANNEL &&
                       gab_valkind(c) <= kGAB_CHANNELCLOSED,
                   "Invalid kind");

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  return channel->header.kind == kGAB_CHANNELCLOSED;
};

/*
 * The channel implementation is subtle here. There are two atomic components:
 *  - data (An atomic gab_value* which points to the beginning of the slice of
 * values in the channel)
 *  - len (An atomic uint64 which contains the number of values in the channel's
 * slice)
 *
 * Because there are *two* pieces of atomic state that need to be synced, the
 * implementation is a little more nuanced.
 *
 * "Putters" need to wait until the *data* ptr is null. This is how
 * gab_chnisfull() works. Therefore, "putters" wait in a loop like this:
 *
 * while(gab_chnisfull(channel))
 *  yield()
 *
 *  "Takers" need to wait until the *len*  is not zero. This is how
 * gab_chnisempty() works. Therefore, "takers" wait in a loop liek this:
 *
 *  while(gab_chnisempty(channel))
 *    yield()
 *
 * This way, Putters don't stomp over other putters, and they also don't stomp
 * over other takers. THis is because other putters are prevented from acting as
 * they don't have the data ptr, and takers cant act until they have the len.
 * This guarantees that no one sees the channel (Other than the putter who
 * succeeded) until the data is completely ready.
 *
 * The inverse is true for takers. Once a taker succeeds in taking the len, no
 * other takers will try. And no putters can act until the taker restores the
 * *data* atomic.
 *
 * TODO @cgab @qol: Refactor channel implementation.
 * Implement a spinlock surrounding the channel mutating operations.
 * It isn't totally functional under contention as it is.
 */

GAB_API bool gab_chnmatches(gab_value c, gab_value *ptr) {
  gab_precondition(gab_valkind(c) >= kGAB_CHANNEL &&
                       gab_valkind(c) <= kGAB_CHANNELCLOSED,
                   "Invalid kind");
  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);
  return atomic_load(&channel->spinlock) ||
         (atomic_load(&channel->data) == ptr);
}

GAB_API bool gab_chnisempty(gab_value c) {
  gab_precondition(gab_valkind(c) >= kGAB_CHANNEL &&
                       gab_valkind(c) <= kGAB_CHANNELCLOSED,
                   "Invalid kind");

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);
  return !atomic_load(&channel->spinlock) &&
         (atomic_load(&channel->data) == nullptr);
};

GAB_API bool gab_chnisfull(gab_value c) {
  gab_precondition(gab_valkind(c) >= kGAB_CHANNEL &&
                       gab_valkind(c) <= kGAB_CHANNELCLOSED,
                   "Invalid kind");

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);
  return !atomic_load(&channel->spinlock) &&
         (atomic_load(&channel->data) != nullptr);
};

GAB_INTERNAL bool __gab_chntrylock(struct gab_ochannel *channel) {
  return !(
      atomic_load(&channel->spinlock) ||
      atomic_exchange_explicit(&channel->spinlock, 1, memory_order_acquire));
}

GAB_INTERNAL void __gab_chnunlock(struct gab_ochannel *channel) {
  return atomic_store_explicit(&channel->spinlock, 0, memory_order_release);
}

/*
 * Abandon a channel. You must *know* the len you're abandoning atomically
 * for this to be correct.
 */
GAB_INTERNAL bool __gab_bchnabandon(struct gab_triple gab,
                                    struct gab_ochannel *channel,
                                    gab_value *data, uint64_t len) {
  while (!__gab_chntrylock(channel))
    gab_busywait(gab);

  gab_verify(atomic_load(&channel->spinlock) == 1, "Shall be locked");

  // Reset values
  gab_value *src = atomic_load_explicit(&channel->data, memory_order_acquire);
  uint64_t avail = atomic_load_explicit(&channel->len, memory_order_acquire);

  gab_assert(!avail == !src, "Shall have both src and avail, or neither");

  if (src != data)
    return __gab_chnunlock(channel), false;

  atomic_store_explicit(&channel->len, 0, memory_order_release);
  atomic_store_explicit(&channel->data, nullptr, memory_order_release);

  return __gab_chnunlock(channel), true;
}

/*
 * Try to put a slice into a channel.
 */
GAB_INTERNAL bool __gab_chnput(struct gab_ochannel *channel, uint64_t len,
                               gab_value *vs) {
  // Acquire spinlock
  if (!__gab_chntrylock(channel))
    return false;

  // Load our values now that we have the lock.
  gab_value *src = atomic_load_explicit(&channel->data, memory_order_acquire);
  uint64_t avail = atomic_load_explicit(&channel->len, memory_order_acquire);

  gab_assert(!avail == !src, "Shall have both src and avail, or neither");

  // If we still don't have an empty channel, bail
  if (avail || src)
    return __gab_chnunlock(channel), false;

  // Store values
  atomic_store_explicit(&channel->data, vs, memory_order_release);
  atomic_store_explicit(&channel->len, len, memory_order_release);

  return __gab_chnunlock(channel), true;
}

/*
 * Try to load up to n values from the channel into dest.
 * If successful, return a gab_number of the number of values actually loaded.
 * Else return gab_cundefined.
 *
 * TODO @cgab @bug: Somehow it is still possible for this race
 */
GAB_INTERNAL gab_value __gab_chntake(struct gab_ochannel *channel, uint64_t n,
                                     gab_value *dest) {
  // Acquire spinlock
  if (!__gab_chntrylock(channel))
    return gab_cundefined;

  // Reset values
  uint64_t avail =
      atomic_exchange_explicit(&channel->len, 0, memory_order_acquire);
  gab_value *src =
      atomic_exchange_explicit(&channel->data, nullptr, memory_order_acquire);

  gab_assert(!avail == !src, "Shall have both src and avail, or neither");

  // We got the lock, but someone else took the values beforehand.
  if (!(avail && src))
    return __gab_chnunlock(channel), gab_cundefined;

  uint64_t len = n < avail ? n : avail;
  memcpy(dest, src, sizeof(gab_value) * len);

  gab_assert(len, "Shall take at least 1 value");

  return __gab_chnunlock(channel), gab_number(avail);
}

// Waits until the channel is empty
GAB_INTERNAL gab_value __gab_chnwaitempty(struct gab_triple gab,
                                          struct gab_ochannel *channel,
                                          gab_value c, uint64_t tries,
                                          uint64_t *sofar) {
  while (gab_chnisfull(c)) {
    if (gab_chnisclosed(c))
      return gab_cundefined;

    *sofar = *sofar + 1;

    if (*sofar > tries)
      return gab_ctimeout;

    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      gab_busywait(gab);
      break;
    }
  }

  return gab_cvalid;
}

GAB_INTERNAL gab_value __gab_chnwaitmatches(struct gab_triple gab,
                                            gab_value *data, gab_value c,
                                            uint64_t tries, uint64_t *sofar) {
  while (gab_chnmatches(c, data)) {
    if (gab_chnisclosed(c))
      return gab_cundefined;

    *sofar = *sofar + 1;

    if (*sofar > tries)
      return gab_ctimeout;

    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      gab_busywait(gab);
      break;
    }
  }

  return gab_cvalid;
}

// Waits until the channel is full
GAB_INTERNAL gab_value __gab_chnwaitfull(struct gab_triple gab,
                                         struct gab_ochannel *channel,
                                         gab_value c, uint64_t tries,
                                         uint64_t *sofar) {
  while (gab_chnisempty(c)) {
    *sofar = *sofar + 1;

    if (gab_chnisclosed(c))
      return gab_cundefined;

    if (*sofar > tries)
      return gab_ctimeout;

    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      gab_busywait(gab);
      break;
    }
  }

  return gab_cvalid;
}

// Unsafe, blocking channel put
GAB_INTERNAL gab_value __gab_ubchnput(struct gab_triple gab,
                                      struct gab_ochannel *channel, gab_value c,
                                      uint64_t len, gab_value *vs,
                                      uint64_t tries, uint64_t *sofar) {
  gab_value res = gab_cundefined;

  while (!gab_chnisclosed(c)) {
    res = __gab_chnwaitempty(gab, channel, c, tries, sofar);

    if (res != gab_cvalid)
      return res;

    if (__gab_chnput(channel, len, vs))
      return res;
  }

  return gab_cinvalid;
}

// Blocking put
GAB_INTERNAL gab_value __gab_bchnput(struct gab_triple gab,
                                     struct gab_ochannel *channel, gab_value c,
                                     uint64_t len, gab_value *vs,
                                     uint64_t tries) {
  uint64_t sofar = 0;

  gab_value res = __gab_ubchnput(gab, channel, c, len, vs, tries, &sofar);

  // In any of these cases, we failed to put and
  // can forward the error.
  switch (res) {
  case gab_ctimeout:
  case gab_cinvalid:
  case gab_cundefined:
    return res;
  }
  // TODO @cgab @bug: What if *before we start waiting*, someone takes, and some
  // one puts? Then we accidentally wait for a put which isn't ours, and may
  // timeout?

  // Wait for a taker.
  res = __gab_chnwaitmatches(gab, vs, c, tries, &sofar);

  switch (res) {
  // We were interrupted, timed out, or the channel closed.
  case gab_cinvalid:
  case gab_cundefined:
    return res;
    // If a taker never arrives, we should remove our value as if our put
    // failed and return a timeout.
    // If we fail to abandon, fallthrough as a taker must have arrived.
  case gab_ctimeout:
    if (__gab_bchnabandon(gab, channel, vs, len))
      return res;
  // A taker arrived.
  default:
    return gab_cvalid;
  }
}

/*
 * Returns
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * positive gab_number containing number of values written on success
 * negative gab_number containing number of values *would* have written, but
 * didn't have space. (failure).
 *
 * Blocking take.
 */
GAB_INTERNAL gab_value __gab_bchntake(struct gab_triple gab,
                                      struct gab_ochannel *channel, gab_value c,
                                      uint64_t len, gab_value *vs,
                                      uint64_t tries) {
  gab_value res = gab_cundefined;

  uint64_t sofar = 0;

  while (!gab_chnisclosed(c) && res == gab_cundefined) {
    res = __gab_chnwaitfull(gab, channel, c, tries, &sofar);

    if (res != gab_cvalid)
      return res;

    res = __gab_chntake(channel, len, vs);
  }

  return res;
}

/*
 * Returns
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * gab_cvalid on success
 */
GAB_API gab_value gab_ntchnput(struct gab_triple gab, gab_value c, uint64_t len,
                               gab_value *vs, uint64_t tries) {
  gab_precondition(gab_valkind(c) >= kGAB_CHANNEL &&
                       gab_valkind(c) <= kGAB_CHANNELCLOSED,
                   "Invalid kind");

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  switch (channel->header.kind) {
  case kGAB_CHANNEL:
    return __gab_bchnput(gab, channel, c, len, vs, tries);
  case kGAB_CHANNELCLOSED:
    return gab_cundefined;
  default:
    gab_unreachable("Impossible channel kind");
    return gab_cinvalid;
  }
}

GAB_API gab_value gab_untchnput(struct gab_triple gab, gab_value c,
                                uint64_t len, gab_value *vs, uint64_t tries) {
  gab_precondition(gab_valkind(c) >= kGAB_CHANNEL &&
                       gab_valkind(c) <= kGAB_CHANNELCLOSED,
                   "Invalid kind");

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  switch (channel->header.kind) {
  case kGAB_CHANNEL: {
    uint64_t sofar = 0;
    return __gab_ubchnput(gab, channel, c, len, vs, tries, &sofar);
  }
  case kGAB_CHANNELCLOSED:
    return gab_cundefined;
  default:
    gab_unreachable("Impossible channel kind");
    return gab_cinvalid;
  }
}

GAB_API gab_value gab_tchnput(struct gab_triple gab, gab_value c,
                              gab_value value, uint64_t tries) {
  return gab_ntchnput(gab, c, 1, &value, tries);
}

GAB_API gab_value gab_nchnput(struct gab_triple gab, gab_value channel,
                              uint64_t len, gab_value *vs) {
  gab_value v = gab_ntchnput(gab, channel, len, vs, (uint64_t)-1);

  gab_assert(v != gab_ctimeout,
             "Shall not return ctimeout in blocking channel put");

  return v;
}

GAB_API gab_value gab_chnput(struct gab_triple gab, gab_value c,
                             gab_value value) {
  if (gab.wkid != 1)
    return gab_tchnput(gab, c, value, (uint64_t)-1);

  if (__gab_jbisrunning(gab, gab.eg->jobs + gab.wkid))
    return gab_tchnput(gab, c, value, (uint64_t)-1);

  for (;;) {
    gab_value res = gab_tchnput(gab, c, value, 1);

    if (res != gab_ctimeout)
      return res;

    if (!__gab_jbisalive(gab, gab.wkid) ||
        !__gab_jbstep(gab, gab.eg->jobs + gab.wkid))
      return __gab_jbbail(gab, gab.eg->jobs + gab.wkid),
             gab_tchnput(gab, c, value, 1);
  }
}

/*
 * Returns:
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * a gab_number on success. This number will contain the
 * amount of values the channel *had available to write*,
 * not the number that was *actually written*. To obtain the amount
 * actually written use MIN(result, len).
 */
GAB_API gab_value gab_ntchntake(struct gab_triple gab, gab_value c,
                                uint64_t len, gab_value *data, uint64_t tries) {
  gab_precondition(gab_valkind(c) >= kGAB_CHANNEL &&
                       gab_valkind(c) <= kGAB_CHANNELCLOSED,
                   "Invalid kind");

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  switch (channel->header.kind) {
  case kGAB_CHANNEL:
    gab_value res = __gab_bchntake(gab, channel, c, len, data, tries);
    return res;
  case kGAB_CHANNELCLOSED:
    return gab_cundefined;
  default:
    gab_unreachable("Impossible channel kind");
    return gab_cinvalid;
  }
};

GAB_API gab_value gab_tchntake(struct gab_triple gab, gab_value channel,
                               uint64_t tries) {
  gab_value out;
  gab_value res = gab_ntchntake(gab, channel, 1, &out, tries);

  if (gab_valkind(res) != kGAB_NUMBER)
    return res;

  gab_int n = gab_valtoi(res);

  gab_assert(n >= 1, "We should always receive at least one value.");

  return out;
};

GAB_API gab_value gab_nchntake(struct gab_triple gab, gab_value channel,
                               uint64_t len, gab_value *data) {
  return gab_ntchntake(gab, channel, len, data, (uint64_t)-1);
}

GAB_API gab_value gab_chntake(struct gab_triple gab, gab_value c) {
  if (gab.wkid != 1)
    return gab_tchntake(gab, c, (uint64_t)-1);

  if (__gab_jbisrunning(gab, gab.eg->jobs + gab.wkid))
    return gab_tchntake(gab, c, (uint64_t)-1);

  for (;;) {
    gab_value res = gab_tchntake(gab, c, 1);

    if (res != gab_ctimeout)
      return res;

    if (!__gab_jbisalive(gab, gab.wkid) ||
        !__gab_jbstep(gab, gab.eg->jobs + gab.wkid))
      return __gab_jbbail(gab, gab.eg->jobs + gab.wkid),
             gab_tchntake(gab, c, 1);
  }

  gab_unreachable("Should not break out of above loop");
}

GAB_INTERNAL uint64_t __gab_insdump(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset);

GAB_INTERNAL uint64_t __gab_insdumpsimple(FILE *stream,
                                          struct gab_oprototype *self,
                                          uint64_t offset) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s\n", name);
  return offset + 1;
}

GAB_INTERNAL uint64_t __gab_insdumpsend(FILE *stream,
                                        struct gab_oprototype *self,
                                        uint64_t offset) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  uint16_t constant =
      ((uint16_t)v_uint8_t_val_at(&self->src->bytecode, offset + 1)) << 8 |
      v_uint8_t_val_at(&self->src->bytecode, offset + 2);

  gab_value msg = v_gab_value_val_at(&self->src->constants,
                                     constant & (~(fHAVE_TAIL << 8)));

  bool tail = ((constant & (fHAVE_TAIL << 8)) != 0);

  fprintf(stream, "%-25s" GAB_BLUE, name);
  gab_fvalinspect(stream, msg, 0);
  fprintf(stream, GAB_RESET " %s\n", tail ? " [TAILCALL]" : "");

  return offset + 3;
}

GAB_INTERNAL uint64_t __gab_insdumptwobyte(FILE *stream,
                                           struct gab_oprototype *self,
                                           uint64_t offset) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  uint8_t operand_a = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  uint8_t operand_b = v_uint8_t_val_at(&self->src->bytecode, offset + 2);
  fprintf(stream, "%-25s%hhx %hhx\n", name, operand_a, operand_b);
  return offset + 3;
}

GAB_INTERNAL uint64_t __gab_insdumponebyte(FILE *stream,
                                           struct gab_oprototype *self,
                                           uint64_t offset, bool extra) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  offset += extra;

  uint8_t operand = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  fprintf(stream, "%-25s%hhx\n", name, operand);
  return offset + 2;
}

GAB_INTERNAL uint64_t __gab_insdumptrim(FILE *stream,
                                        struct gab_oprototype *self,
                                        uint64_t offset) {
  uint8_t wantbyte = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s%hhx\n", name, wantbyte);
  return offset + 2;
}

GAB_INTERNAL uint64_t __gab_insdumpreturn(FILE *stream,
                                          struct gab_oprototype *self,
                                          uint64_t offset) {
  fprintf(stream, "%-25s\n", "RETURN");
  return offset + 1;
}

GAB_INTERNAL uint64_t __gab_insdumppack(FILE *stream,
                                        struct gab_oprototype *self,
                                        uint64_t offset) {
  uint8_t operandA = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  uint8_t operandB = v_uint8_t_val_at(&self->src->bytecode, offset + 2);
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s -> %hhx %hhx\n", name, operandA, operandB);
  return offset + 3;
}

GAB_INTERNAL uint64_t __gab_insdumpconstant(FILE *stream,
                                            struct gab_oprototype *self,
                                            uint64_t offset, bool extra) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  offset += extra;

  uint16_t constant =
      ((uint16_t)v_uint8_t_val_at(&self->src->bytecode, offset + 1)) << 8 |
      v_uint8_t_val_at(&self->src->bytecode, offset + 2);

  fprintf(stream, "%-25s", name);
  gab_fvalinspect(stdout, v_gab_value_val_at(&self->src->constants, constant),
                  0);

  fprintf(stream, "\n");
  return offset + 3;
}

GAB_INTERNAL uint64_t __gab_insdumpnconstant(FILE *stream,
                                             struct gab_oprototype *self,
                                             uint64_t offset, bool extra) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  offset += extra;

  fprintf(stream, "%-25s", name);

  uint8_t n = v_uint8_t_val_at(&self->src->bytecode, offset + 1);

  for (int i = 0; i < n; i++) {
    uint16_t constant =
        ((uint16_t)v_uint8_t_val_at(&self->src->bytecode, offset + 2 + (2 * i)))
            << 8 |
        v_uint8_t_val_at(&self->src->bytecode, offset + 3 + (2 * i));

    gab_fvalinspect(stdout, v_gab_value_val_at(&self->src->constants, constant),
                    0);

    if (i < n - 1)
      fprintf(stream, ", ");
  }

  fprintf(stream, "\n");
  return offset + 2 + (2 * n);
}

GAB_INTERNAL uint64_t __gab_insdump(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset) {
  uint8_t op = v_uint8_t_val_at(&self->src->bytecode, offset);
  switch (op) {
  case OP_POP:
  case OP_TUPLE:
  case OP_NOP:
    return __gab_insdumpsimple(stream, self, offset);
  case OP_PACK_DICT:
  case OP_PACK_LIST:
    return __gab_insdumppack(stream, self, offset);
  case OP_TUPLE_NCONSTANT:
  case OP_NCONSTANT:
    return __gab_insdumpnconstant(stream, self, offset, false);
  case OP_TUPLE_CONSTANT:
  case OP_CONSTANT:
    return __gab_insdumpconstant(stream, self, offset, false);
  case OP_NTUPLE_CONSTANT:
    return __gab_insdumpconstant(stream, self, offset, true);
  case OP_NTUPLE_NCONSTANT:
    return __gab_insdumpnconstant(stream, self, offset, true);
  case OP_SEND:
  case OP_SEND_BLOCK:
  case OP_SEND_NATIVE:
  case OP_SEND_PROPERTY:
  case OP_SEND_PRIMITIVE_CONCAT:
  case OP_SEND_PRIMITIVE_SPLATLIST:
  case OP_SEND_PRIMITIVE_SPLATDICT:
  case OP_SEND_PRIMITIVE_ADD:
  case OP_SEND_PRIMITIVE_SUB:
  case OP_SEND_PRIMITIVE_MUL:
  case OP_SEND_PRIMITIVE_DIV:
  case OP_SEND_PRIMITIVE_MOD:
  case OP_SEND_PRIMITIVE_EQ:
  case OP_SEND_PRIMITIVE_LT:
  case OP_SEND_PRIMITIVE_LTE:
  case OP_SEND_PRIMITIVE_GT:
  case OP_SEND_PRIMITIVE_GTE:
  case OP_SEND_PRIMITIVE_CALL_BLOCK:
  case OP_SEND_PRIMITIVE_CALL_NATIVE:
  case OP_TAILSEND_BLOCK:
  case OP_TAILSEND_PRIMITIVE_CALL_BLOCK:
  case OP_LOCALSEND_BLOCK:
  case OP_LOCALTAILSEND_BLOCK:
  case OP_MATCHSEND_BLOCK:
  case OP_MATCHTAILSEND_BLOCK:
    return __gab_insdumpsend(stream, self, offset);
  case OP_NTUPLE:
  case OP_POP_N:
  case OP_STORE_LOCAL:
  case OP_POPSTORE_LOCAL:
  case OP_LOAD_UPVALUE:
  case OP_LOAD_LOCAL:
  case OP_TUPLE_LOAD_LOCAL:
    return __gab_insdumponebyte(stream, self, offset, false);
  case OP_NTUPLE_LOAD_LOCAL:
    return __gab_insdumptwobyte(stream, self, offset);
  case OP_NTUPLE_NLOAD_LOCAL: {
    const char *name =
        gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

    uint8_t tuple_operand = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
    uint8_t operand = v_uint8_t_val_at(&self->src->bytecode, offset + 2);

    fprintf(stream, "%-25s(%hhx)%hhx: ", name, tuple_operand, operand);

    for (int i = 0; i < operand - 1; i++) {
      fprintf(stream, "%hhx, ",
              v_uint8_t_val_at(&self->src->bytecode, offset + 3 + i));
    }

    fprintf(stream, "%hhx\n",
            v_uint8_t_val_at(&self->src->bytecode, offset + 2 + operand));

    return offset + 3 + operand;
  }
  case OP_NPOPSTORE_LOCAL:
  case OP_NPOPSTORE_STORE_LOCAL:
  case OP_NLOAD_UPVALUE:
  case OP_NLOAD_LOCAL:
  case OP_TUPLE_NLOAD_LOCAL: {
    const char *name =
        gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

    uint8_t operand = v_uint8_t_val_at(&self->src->bytecode, offset + 1);

    fprintf(stream, "%-25s%hhx: ", name, operand);

    for (int i = 0; i < operand - 1; i++) {
      fprintf(stream, "%hhx, ",
              v_uint8_t_val_at(&self->src->bytecode, offset + 2 + i));
    }

    fprintf(stream, "%hhx\n",
            v_uint8_t_val_at(&self->src->bytecode, offset + 1 + operand));

    return offset + 2 + operand;
  }
  case OP_RETURN:
  case OP_RETURN_1:
  case OP_RETURN_2:
  case OP_RETURN_3:
  case OP_RETURN_4:
  case OP_RETURN_5:
  case OP_RETURN_6:
  case OP_RETURN_7:
  case OP_RETURN_8:
  case OP_RETURN_9:
    return __gab_insdumpreturn(stream, self, offset);
  case OP_BLOCK: {
    offset++;

    uint16_t proto_constant =
        (((uint16_t)self->src->bytecode.data[offset] << 8) |
         self->src->bytecode.data[offset + 1]);

    offset += 2;

    gab_value pval = v_gab_value_val_at(&self->src->constants, proto_constant);

    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(pval);

    fprintf(stream, "%-25s" GAB_CYAN "%-20s\n" GAB_RESET, "OP_BLOCK",
            gab_strdata(&p->src->name));

    for (int j = 0; j < p->nupvalues; j++) {
      int isLocal = p->data[j] & fLOCAL_LOCAL;
      uint8_t index = p->data[j] >> 1;
      fprintf(stream, "      |                   %d %s\n", index,
              isLocal ? "local" : "upvalue");
    }
    return offset;
  }
  case OP_TRIM_UP1:
  case OP_TRIM_UP2:
  case OP_TRIM_UP3:
  case OP_TRIM_UP4:
  case OP_TRIM_UP5:
  case OP_TRIM_UP6:
  case OP_TRIM_UP7:
  case OP_TRIM_UP8:
  case OP_TRIM_UP9:
  case OP_TRIM_DOWN1:
  case OP_TRIM_DOWN2:
  case OP_TRIM_DOWN3:
  case OP_TRIM_DOWN4:
  case OP_TRIM_DOWN5:
  case OP_TRIM_DOWN6:
  case OP_TRIM_DOWN7:
  case OP_TRIM_DOWN8:
  case OP_TRIM_DOWN9:
  case OP_TRIM_EXACTLY0:
  case OP_TRIM_EXACTLY1:
  case OP_TRIM_EXACTLY2:
  case OP_TRIM_EXACTLY3:
  case OP_TRIM_EXACTLY4:
  case OP_TRIM_EXACTLY5:
  case OP_TRIM_EXACTLY6:
  case OP_TRIM_EXACTLY7:
  case OP_TRIM_EXACTLY8:
  case OP_TRIM_EXACTLY9:
  case OP_TRIM: {
    return __gab_insdumptrim(stream, self, offset);
  }
  default: {
    uint8_t code = v_uint8_t_val_at(&self->src->bytecode, offset);
    printf("Unknown opcode %d (%s?)\n", code, gab_opcode_names[code]);
    return offset + 1;
  }
  }
}

GAB_API int64_t gab_fmodinspect(FILE *stream, gab_value module) {
  struct gab_oprototype *proto = nullptr;

  switch (gab_valkind(module)) {
  case kGAB_BLOCK:
    proto = GAB_VAL_TO_PROTOTYPE(GAB_VAL_TO_BLOCK(module)->p);
    break;
  case kGAB_PROTOTYPE:
    proto = GAB_VAL_TO_PROTOTYPE(module);
    break;
  default:
    return -1;
  }

  uint64_t offset = proto->offset;

  uint64_t end = proto->offset + proto->len;

  gab_fvalinspect(stream, proto->src->name, 0);
  fputc('\n', stream);

  while (offset < end) {
    fprintf(stream, GAB_YELLOW "%04" PRIu64 " " GAB_RESET, offset);
    offset = __gab_insdump(stream, proto, offset);
  }

  return 0;
}

/*
 *
 * PRETTY PRINTING GAB OBJECTS
 *
 * An object produces a vector of gab_pprint structs. These are laid out
 * with a layout algorithm.
 */
enum gab_pprint_k {
  kPPRINT_VALUE,
  kPPRINT_ADDRESS,
  kPPRINT_STRING,
  kPPRINT_BINARY,
  kPPRINT_BREAK,
  kPPRINT_SPACE,
  kPPRINT_COMMA,
  kPPRINT_INDENT,
  kPPRINT_DEDENT,
};

struct gab_pprint {
  enum gab_pprint_k k; /* Kind of the token */
  int32_t width;       /* Pre-computed width of the token */
  union gab_pprint_d {
    gab_value val; /* Gab value to be printed with this token. This should be a
                 primitive value, not a nested one. */
    char c;
    const char *s;
    void *addr;
  } as;
};

#define T struct gab_pprint
#define NAME gab_pprint
#include "vector.h"

GAB_INTERNAL int64_t __gab_pprintw(gab_value val) {
  switch (gab_valkind(val)) {
  case kGAB_STRING:
    return gab_strlen(val);
  case kGAB_MESSAGE:
    return gab_strlen(val) + 1;
  case kGAB_BINARY:
    return gab_strlen(val) * 2 + 15;
  case kGAB_NATIVE:
    return gab_strlen(GAB_VAL_TO_NATIVE(val)->name) + 13;
  case kGAB_BOX:
    return __gab_pprintw(GAB_VAL_TO_BOX(val)->type) + 10;
  case kGAB_PRIMITIVE:
    switch (val) {
    case gab_cundefined:
      return strlen("cundefined");
    case gab_cinvalid:
      return strlen("cinvalid");
    case gab_ctimeout:
      return strlen("ctimeout");
    case gab_cvalid:
      return strlen("cvalid");
    default:
      return strlen(gab_opcode_names[gab_valtop(val)]) + 3;
    }
  case kGAB_BLOCK: {
    struct gab_oblock *blk = GAB_VAL_TO_BLOCK(val);
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);
    return gab_strlen(gab_srcname(p->src)) + 4 + 4;
  }
  case kGAB_NUMBER:
    char buf[20] = {};
    // Maybe I can just return number of bytes written?
    snprintf(buf, sizeof(buf), "%lg", gab_valtof(val));
    return strlen(buf);
  case kGAB_CHANNEL:
  case kGAB_CHANNELCLOSED:
    return 13; // Hardcoded and never change
  case kGAB_FIBER:
  case kGAB_FIBERDONE:
  case kGAB_FIBERRUNNING:
    return 11; // Needs to be updated to match
  default:
    gab_unreachable("Invalid kind %d", gab_valkind(val));
    return 0;
  }
}

GAB_INTERNAL void __gab_pppushv(v_gab_pprint *self, gab_value val) {
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = kPPRINT_VALUE,
                              .width = __gab_pprintw(val),
                              .as.val = val,
                          });
}

GAB_INTERNAL void __gab_pppushp(v_gab_pprint *self, void *p) {
  char buf[30] = {};
  // Maybe I can just return number of bytes written?
  snprintf(buf, sizeof(buf), "%p", p);
  int width = strlen(buf);

  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = kPPRINT_ADDRESS,
                              .width = width,
                              .as.addr = p,
                          });
}

GAB_INTERNAL void __gab_pppushk(v_gab_pprint *self, enum gab_pprint_k k) {
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = k,
                              .width = 1,
                              .as.val = gab_nil,
                          });
}

GAB_INTERNAL void __gab_pppushs(v_gab_pprint *self, const char *s) {
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = kPPRINT_STRING,
                              .width = strlen(s),
                              .as.s = s,
                          });
}

GAB_INTERNAL void __gab_pppushb(v_gab_pprint *self, const char *s) {}

GAB_INTERNAL void __gab_pppushkd(v_gab_pprint *self, enum gab_pprint_k k,
                                 union gab_pprint_d d) {
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = k,
                              .width = 1,
                              .as = d,
                          });
}

GAB_INTERNAL bool __gab_pptokify(v_gab_pprint *self, gab_value val);

GAB_INTERNAL void __gab_pppushrec(v_gab_pprint *self, gab_value rec) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind");

  __gab_pppushkd(self, kPPRINT_INDENT, (union gab_pprint_d){'{'});
  __gab_pppushk(self, kPPRINT_SPACE);

  uint64_t len = gab_reclen(rec);

  for (uint64_t i = 0; i < len; i++) {
    __gab_pptokify(self, gab_ukrecat(rec, i));

    __gab_pppushs(self, " ");

    __gab_pptokify(self, gab_uvrecat(rec, i));

    if (i + 1 < len)
      __gab_pppushk(self, kPPRINT_COMMA);

    __gab_pppushk(self, kPPRINT_SPACE);
  }

  __gab_pppushkd(self, kPPRINT_DEDENT, (union gab_pprint_d){'}'});
};

GAB_INTERNAL void __gab_pppushchn(v_gab_pprint *self, gab_value chan) {
  gab_precondition(gab_valkind(chan) == kGAB_CHANNEL ||
                       gab_valkind(chan) == kGAB_CHANNELCLOSED,
                   "Invalid kind");

  struct gab_ochannel *ch = GAB_VAL_TO_CHANNEL(chan);
  __gab_pppushkd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  __gab_pppushs(self, tGAB_CHANNEL);
  __gab_pppushk(self, kPPRINT_SPACE);
  __gab_pppushp(self, ch);
  __gab_pppushkd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

GAB_INTERNAL void __gab_pppushfib(v_gab_pprint *self, gab_value fib) {
  gab_precondition(gab_valkind(fib) == kGAB_FIBER ||
                       gab_valkind(fib) == kGAB_FIBERRUNNING ||
                       gab_valkind(fib) == kGAB_FIBERDONE,
                   "Invalid kind");

  struct gab_ofiber *v = GAB_VAL_TO_FIBER(fib);

  gab_value msg = v->data[0];
  gab_value rec = v->data[1];

  __gab_pppushkd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  __gab_pppushs(self, tGAB_FIBER);
  __gab_pppushk(self, kPPRINT_SPACE);
  __gab_pppushp(self, v);
  __gab_pppushk(self, kPPRINT_SPACE);
  __gab_pptokify(self, msg);
  __gab_pppushk(self, kPPRINT_SPACE);
  __gab_pptokify(self, rec);
  __gab_pppushkd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

GAB_INTERNAL void __gab_pppushbox(v_gab_pprint *self, gab_value box) {
  gab_precondition(gab_valkind(box) == kGAB_BOX, "Invalid kind");

  struct gab_obox *v = GAB_VAL_TO_BOX(box);
  __gab_pppushkd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  __gab_pppushs(self, tGAB_BOX);
  __gab_pppushk(self, kPPRINT_SPACE);
  __gab_pptokify(self, v->type);
  __gab_pppushkd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

GAB_INTERNAL void __gab_pppushbin(v_gab_pprint *self, gab_value bin) {
  gab_precondition(gab_valkind(bin) == kGAB_BINARY, "Invalid kind");

  __gab_pppushkd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  __gab_pppushs(self, tGAB_BINARY);
  __gab_pppushk(self, kPPRINT_SPACE);
  __gab_pppushs(self, "0x");
  v_gab_pprint_push(self, (struct gab_pprint){
                              .k = kPPRINT_BINARY,
                              .width = gab_strlen(bin),
                              .as.val = bin,
                          });
  __gab_pppushkd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

GAB_INTERNAL void __gab_pppushntv(v_gab_pprint *self, gab_value ntv) {
  gab_precondition(gab_valkind(ntv) == kGAB_NATIVE, "Invalid kind");

  struct gab_onative *v = GAB_VAL_TO_NATIVE(ntv);
  __gab_pppushkd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  __gab_pppushs(self, tGAB_NATIVE);
  __gab_pppushk(self, kPPRINT_SPACE);
  __gab_pppushv(self, v->name);
  __gab_pppushkd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

GAB_INTERNAL void __gab_pppushblk(v_gab_pprint *self, gab_value block) {
  gab_precondition(gab_valkind(block) == kGAB_BLOCK, "Invalid kind");

  struct gab_oblock *blk = GAB_VAL_TO_BLOCK(block);
  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);
  uint64_t line = gab_srcline(p->src, p->offset);

  __gab_pppushkd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  __gab_pppushs(self, tGAB_BLOCK);
  __gab_pppushk(self, kPPRINT_SPACE);
  __gab_pppushv(self, gab_srcname(p->src));
  __gab_pppushk(self, kPPRINT_SPACE);
  __gab_pppushv(self, gab_number(line));
  __gab_pppushkd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

GAB_INTERNAL void __gab_pppushshp(v_gab_pprint *self, gab_value shp) {
  gab_precondition(gab_valkind(shp) == kGAB_SHAPE ||
                       gab_valkind(shp) == kGAB_SHAPELIST,
                   "Invalid kind");

  __gab_pppushkd(self, kPPRINT_INDENT, (union gab_pprint_d){'<'});
  __gab_pppushs(self, tGAB_SHAPE);
  __gab_pppushk(self, kPPRINT_SPACE);

  uint64_t len = gab_shplen(shp);
  for (uint64_t i = 0; i < len; i++) {
    __gab_pptokify(self, gab_ushpat(shp, i));
    __gab_pppushk(self, kPPRINT_SPACE);
  }

  __gab_pppushkd(self, kPPRINT_DEDENT, (union gab_pprint_d){'>'});
}

GAB_INTERNAL void __gab_pppushlst(v_gab_pprint *self, gab_value rec) {
  gab_precondition(gab_valkind(rec) == kGAB_RECORD, "Invalid kind");

  __gab_pppushkd(self, kPPRINT_INDENT, (union gab_pprint_d){'['});
  __gab_pppushk(self, kPPRINT_SPACE);

  uint64_t len = gab_reclen(rec);

  for (uint64_t i = 0; i < len; i++) {
    __gab_pptokify(self, gab_uvrecat(rec, i));

    if (i + 1 < len)
      __gab_pppushk(self, kPPRINT_COMMA);

    __gab_pppushk(self, kPPRINT_SPACE);
  }

  __gab_pppushkd(self, kPPRINT_DEDENT, (union gab_pprint_d){']'});
}

GAB_INTERNAL bool __gab_pptokify(v_gab_pprint *self, gab_value val) {
  switch (gab_valkind(val)) {
  case kGAB_SHAPE:
  case kGAB_SHAPELIST:
    return __gab_pppushshp(self, val), true;
  case kGAB_BLOCK:
    return __gab_pppushblk(self, val), false;
  case kGAB_BOX:
    return __gab_pppushbox(self, val), false;
  case kGAB_NATIVE:
    return __gab_pppushntv(self, val), false;
  case kGAB_BINARY:
    return __gab_pppushbin(self, val), false;
  case kGAB_RECORD:
    return (gab_recisl(val) ? __gab_pppushlst(self, val)
                            : __gab_pppushrec(self, val)),
           true;
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE:
    return __gab_pppushfib(self, val), false;
  case kGAB_CHANNEL:
  case kGAB_CHANNELCLOSED:
    return __gab_pppushchn(self, val), false;
  default:
    return __gab_pppushv(self, val), false;
  }
}

GAB_INTERNAL const char *__gab_kndcolor(gab_value v) {
  switch (gab_valkind(v)) {
  case kGAB_STRING:
    return GAB_GREEN;
  case kGAB_NUMBER:
    return GAB_YELLOW;
  case kGAB_MESSAGE:
    return GAB_CYAN;
  case kGAB_PRIMITIVE:
    return GAB_RED;
  case kGAB_CHANNEL:
  case kGAB_CHANNELCLOSED:
    return GAB_MAGENTA;
  default:
    return "";
  }
}

GAB_INTERNAL int64_t __gab_spprint_through(char **dest, uint64_t *n,
                                           struct gab_pprint t) {
  switch (t.k) {
  case kPPRINT_SPACE:
    return __gab_snprintf_through(dest, n, " ");
  case kPPRINT_COMMA:
    return __gab_snprintf_through(dest, n, ",");
  case kPPRINT_INDENT:
    return __gab_snprintf_through(dest, n, GAB_YELLOW "%c" GAB_RESET, t.as.c);
  case kPPRINT_DEDENT:
    return __gab_snprintf_through(dest, n, GAB_YELLOW "%c" GAB_RESET, t.as.c);
  case kPPRINT_BREAK:
    return __gab_snprintf_through(dest, n, "\n");
  case kPPRINT_ADDRESS:
    return __gab_snprintf_through(dest, n, GAB_MAGENTA "%p" GAB_RESET,
                                  t.as.addr);
  case kPPRINT_STRING:
    return __gab_snprintf_through(dest, n, GAB_YELLOW "%.*s" GAB_RESET, t.width,
                                  t.as.s);
  case kPPRINT_BINARY: {
    const char *s = gab_strdata(&t.as.val);
    int64_t len = t.width;

    if (__gab_snprintf_through(dest, n, GAB_YELLOW) < 0)
      return -1;

    if (len < cGAB_BINARY_LEN_CUTOFF) {
      while (len--) {
        if (__gab_snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;
      }
    } else {
      uint64_t preview = cGAB_BINARY_LEN_CUTOFF;
      while (preview--)
        if (__gab_snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;

      if (__gab_snprintf_through(dest, n, "...") < 0)
        return -1;
    }

    return __gab_snprintf_through(dest, n, GAB_RESET);
  }
  case kPPRINT_VALUE:
    // Depth should be irrelevant bc these should be
    // primitives only
    const char *color = __gab_kndcolor(t.as.val);
    if (__gab_snprintf_through(dest, n, "%s", color) < 0)
      return -1;

    if (__gab_svalinspect(dest, n, t.as.val, 1) < 0)
      return -1;

    return __gab_snprintf_through(dest, n, GAB_RESET);
  }
}

/*
 * LAYOUT ALGORITHM:
 * - We are given a list of tokens.
 * - Given a configured width (40 columns),
 *   convert some SPACE tokens to BREAK tokens
 *   such that the string fits in the given width.
 *
 *          { a: 'hi', b: 'world', c: [1, 2] }
 *  Spaces   ^  ^     ^  ^        ^  ^   ^  ^
 * Indents  ^                         ^
 * Dedents                                 ^ ^
 *    Seps           ^           ^      ^
 *
 *    => {
 *          a: 'hi',
 *          b: 'world',
 *          c: [1, 2],
 *       }
 *
 *  Track a stack of indents.
 *  Try to layout indent on one line.
 *  if width < 40
 *    all done
 *  else
 *    backtrack to INDENT token.
 *    Increment indent count.
 *    Layout until DEDENT is found
 *    by converting SPACE to BREAK
 */

struct layout {
  int32_t t, w;
};

GAB_INTERNAL struct layout __gab_layout(v_gab_pprint *self, int32_t t,
                                        int32_t indent);

// Compute if the whole indentation can be laid out on one line.
GAB_INTERNAL struct layout __gab_layline(v_gab_pprint *self, int32_t t,
                                         int32_t width) {

  // Todo @cgab @qol: Configurable indentation
  gab_assert(v_gab_pprint_val_at(self, t).k == kPPRINT_INDENT,
             "Shall begin with an indent");

  struct layout l = {.t = t, .w = width};

  for (struct gab_pprint p = v_gab_pprint_val_at(self, ++l.t);
       p.k != kPPRINT_DEDENT; p = v_gab_pprint_val_at(self, ++l.t)) {

    gab_assert(
        l.t < self->len,
        "layout token shall encounter dedent before exceeding vector len");

    l.w += p.width;

    if (p.k == kPPRINT_INDENT)
      l = __gab_layline(self, l.t, l.w);

    if (l.t < 0 || l.w > 40)
      return (struct layout){-1};
  }

  return l;
};

GAB_INTERNAL struct layout __gab_laymulti(v_gab_pprint *self, int32_t t,
                                          int32_t indent) {
  gab_precondition(v_gab_pprint_val_at(self, t).k == kPPRINT_INDENT,
                   "Shall begin with an indent");

  for (struct gab_pprint *p = v_gab_pprint_ref_at(self, ++t);
       p->k != kPPRINT_DEDENT; p = v_gab_pprint_ref_at(self, ++t)) {
    gab_assert(
        t < self->len,
        "layout token shall encounter dedent before exceeding vector len");

    if (p->k == kPPRINT_SPACE)
      p->k = kPPRINT_BREAK;

    if (p->k == kPPRINT_INDENT)
      t = __gab_layout(self, t, indent).t;
  }

  return (struct layout){t};
}

GAB_INTERNAL struct layout __gab_layout(v_gab_pprint *self, int32_t t,
                                        int32_t indent) {
  struct layout l = __gab_layline(self, t, indent * 2);

  if (l.t < 0)
    return __gab_laymulti(self, t, indent + 1);

  return l;
}

GAB_INTERNAL int64_t __gab_spprinttokens(char **dest, uint64_t *n,
                                         v_gab_pprint *self,
                                         const char *prefix) {
  int32_t indent = 0;

  if (__gab_snprintf_through(dest, n, "%s", prefix) < 0)
    return -1;

  for (uint64_t i = 0; i < self->len; i++) {
    struct gab_pprint t = v_gab_pprint_val_at(self, i);

    if (t.k == kPPRINT_INDENT)
      indent++;

    // Dedent needs to be applied *before* we
    // draw the dedent token
    if (i + 1 < self->len)
      if (v_gab_pprint_val_at(self, i + 1).k == kPPRINT_DEDENT)
        indent--;

    if (__gab_spprint_through(dest, n, t) < 0)
      return -1;

    if (t.k == kPPRINT_BREAK) {
      if (__gab_snprintf_through(dest, n, "%s", prefix) < 0)
        return -1;

      for (int32_t i = 0; i < indent; i++)
        if (__gab_snprintf_through(dest, n, "  ") < 0)
          return -1;
    };
  }

  return 0;
}

GAB_API int64_t gab_psvalinspect(char **dest, uint64_t *n, gab_value value,
                                 const char *prefix, int depth) {
  v_gab_pprint tokens = {};

  if (__gab_pptokify(&tokens, value))
    __gab_layout(&tokens, 0, 0);

  if (__gab_spprinttokens(dest, n, &tokens, prefix) < 0)
    return v_gab_pprint_destroy(&tokens), -1;

  return v_gab_pprint_destroy(&tokens), 0;
}

#undef CREATE_GAB_FLEX_OBJ
#undef CREATE_GAB_OBJ

/* ----------------------------------------
 *
 *    GAB PARSER
 *
 *  This section contains the code for producing an AST from a stream of tokens.
 * ----------------------------------------
 */

#define FMT_MALFORMED_EXPRESSION                                               \
  "Expressions start with one of the following values:\n\n"                    \
  "  " GAB_YELLOW "-1.23" GAB_MAGENTA "\t\t\t# A number \n" GAB_RESET          \
  "  " GAB_GREEN "'hello, Joe!'" GAB_MAGENTA "\t\t# A string \n" GAB_RESET     \
  "  " GAB_RED "greet:" GAB_MAGENTA "\t\t# A message\n" GAB_RESET              \
  "  " GAB_BLUE "x :: x + 1" GAB_MAGENTA "\t\t# A block \n" GAB_RESET          \
  "  " GAB_CYAN "{ key: value }" GAB_MAGENTA "\t# A record\n" GAB_RESET "  "   \
  "(" GAB_YELLOW "0x22" GAB_RESET ", " GAB_GREEN "true:" GAB_RESET             \
  ")" GAB_MAGENTA "\t\t# A tuple\n" GAB_RESET "  "                             \
  "a_variable" GAB_MAGENTA "\t\t# Or a variable!" GAB_RESET

#define FMT_ID_NOT_FOUND                                                       \
  "Symbol @ is not yet bound in this scope, nor in parent scopes.\n\n"         \
  "Assignment expressions bind values to symbols.\n\n"                         \
  "  a := " GAB_CYAN "true:" GAB_RESET "\n\n"                                  \
  "Symbols within local scope may be rebound at any time.\n\n"                 \
  "  name := " GAB_GREEN "\"Bob\"" GAB_RESET "\n\n"                            \
  "  name := " GAB_GREEN "\"Uncle Bob\"" GAB_RESET "\n\n"                      \
  "Symbols captured from parent scopes may not be rebound.\n\n"                \
  "  name := " GAB_GREEN "\"Uncle Bob\"" GAB_RESET "\n\n"                      \
  "  () :: name := " GAB_GREEN "\"Old Bob\"" GAB_MAGENTA " # Not allowed"

#define FMT_MALFORMED_BINDING                                                  \
  "This binding is malformed - a valid binding looks like:\n\n"                \
  "  a := " GAB_YELLOW "1" GAB_MAGENTA                                         \
  "\t\t# A single variable and expression\n" GAB_RESET " " GAB_BLACK           \
  "=> a := 1\n" GAB_RESET "  (a, b) := (" GAB_YELLOW "1" GAB_RESET             \
  ", " GAB_RED "bark:" GAB_RESET ")" GAB_MAGENTA                               \
  "\t# A tuple of variables and expressions\n" GAB_RESET " " GAB_BLACK         \
  "=> a := 1, b := bark:\n" GAB_RESET "  (a*, b) := (" GAB_YELLOW              \
  "1" GAB_RESET ", " GAB_YELLOW "2" GAB_RESET ", " GAB_YELLOW "3" GAB_RESET    \
  ")" GAB_MAGENTA                                                              \
  "\t# Specify one variable to collect extra values with '*'\n" GAB_RESET      \
  " " GAB_BLACK "=> a := [1, 2], b := 3\n" GAB_RESET "  (a**) := (" GAB_RED    \
  "num:" GAB_RESET ", " GAB_YELLOW "2" GAB_RESET ")" GAB_MAGENTA               \
  "\t# Specify one variable to zip extra values with '**'\n" GAB_RESET         \
  " " GAB_BLACK "=> a := { num: 2 }\n" GAB_RESET                               \
  "\nThis applies to block parameter bindings as well."

#define FMT_MALFORMED_BINDING_NOTE "\nHint: "

#define FMT_GAB_MALFORMED_STRING                                               \
  "\nSingle quoted strings can contain escape "                                \
  "sequences.\n"                                                               \
  "\n   " GAB_GREEN "'a newline -> " GAB_MAGENTA "\\n" GAB_GREEN               \
  ", or a forward slash -> " GAB_MAGENTA "\\\\" GAB_GREEN "'" GAB_RESET        \
  "\n   " GAB_GREEN "'a valid unicode codepoint: " GAB_MAGENTA                 \
  "\\u[" GAB_YELLOW "2502" GAB_MAGENTA "]" GAB_GREEN "'" GAB_RESET

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
  uint64_t offset;
  gab_value err;
};

struct bc {
  v_uint8_t bc;
  v_uint64_t bc_toks;
  v_gab_value *ks;

  struct gab_src *src;

  uint8_t prev_op, pprev_op;
  uint64_t prev_op_at;

  gab_value err;
};

enum prec_k { kNONE, kEXP, kBINARY_SEND, kSEND, kBUILTIN, kPRIMARY };

typedef gab_value (*parse_f)(struct gab_triple gab, struct parser *,
                             gab_value lhs);

struct parse_rule {
  parse_f prefix;
  parse_f infix;
  enum prec_k prec;
};

GAB_INTERNAL struct parse_rule __gab_prsrule(gab_token k);

/*static uint64_t prev_line(struct parser *parser) {*/
/*  return v_uint64_t_val_at(&parser->src->token_lines, parser->offset - 1);*/
/*}*/

GAB_INTERNAL gab_token __gab_prscurrtok(struct parser *parser) {
  return v_gab_token_val_at(&parser->src->tokens, parser->offset);
}

GAB_INTERNAL bool __gab_prscurrprefix(struct parser *parser) {
  return __gab_prsrule(__gab_prscurrtok(parser)).prefix != nullptr;
}

GAB_INTERNAL gab_token __gab_prsprevtok(struct parser *parser) {
  return v_gab_token_val_at(&parser->src->tokens, parser->offset - 1);
}

GAB_INTERNAL s_char __gab_prsprevsrc(struct parser *parser) {
  return v_s_char_val_at(&parser->src->token_srcs, parser->offset - 1);
}

GAB_INTERNAL gab_value __gab_prsprevid(struct gab_triple gab,
                                       struct parser *parser) {
  s_char s = __gab_prsprevsrc(parser);

  return gab_nstring(gab, s.len, s.data);
}

GAB_INTERNAL bool __gab_prsisbuiltin(struct gab_triple gab, gab_value msg) {
  if (gab_valkind(msg) != kGAB_BINARY)
    return false;

  if (msg == gab_binary(gab, (uint8_t *)mGAB_ASSIGN))
    return true;

  if (msg == gab_binary(gab, (uint8_t *)mGAB_BLOCK))
    return true;

  return false;
}

/* Encode a unicode codepoint */
GAB_INTERNAL int64_t __gab_cpencode(char *out, int64_t utf) {
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
  }

  return 0;
}

/* Handle escape codes in strings */
GAB_INTERNAL a_char *__gab_prsstrraw(struct parser *parser, s_char raw_str) {
  // The parsed string will be at most as long as the raw string.
  // (\n -> one char)
  char buffer[raw_str.len + 1];
  uint64_t buf_end = 0;

  // Skip the first and last bytes of the string.
  // These are the opening/closing quotes, doublequotes, or brackets (For
  // interpolation).
  for (uint64_t i = 1; i < raw_str.len - 1; i++) {
    int8_t c = raw_str.data[i];

    if (c == '\\') {
      i++;

      switch (raw_str.data[i]) {
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
        i++;

        if (raw_str.data[i] != '[')
          return nullptr;

        i++;

        uint8_t cpl = 0;
        char codepoint[8] = {0};

        while (raw_str.data[i] != ']') {

          if (cpl == 7)
            return nullptr;

          if (i >= raw_str.len)
            return nullptr;

          codepoint[cpl++] = raw_str.data[i++];
        }

        char *endptr = nullptr;
        long cp = strtol(codepoint, &endptr, 16);

        if (*codepoint == '\0' || *endptr != '\0')
          return nullptr;

        int result = __gab_cpencode(buffer + buf_end, cp);

        if (!result)
          return nullptr;

        buf_end += result;

        break;
      default:
        return nullptr;
      }
    } else {
      buffer[buf_end++] = c;
    }
  }

  buffer[buf_end] = '\0';

  uint64_t count;
  gab_assert(!__gab_utf8_codepoints((uint8_t *)buffer, &count),
             "The buffer %.*s should be valid utf8-encoded", (int)buf_end,
             (char *)buffer);

  return a_char_create(buffer, buf_end);
};

/* A trim-front version of previd */
GAB_INTERNAL gab_value __gab_tfprsprevid(struct gab_triple gab,
                                         struct parser *parser) {
  s_char s = __gab_prsprevsrc(parser);

  s.data++;
  s.len--;

  return gab_nstring(gab, s.len, s.data);
}

/* A trim-back version of previd */
GAB_INTERNAL gab_value __gab_tbprsprevid(struct gab_triple gab,
                                         struct parser *parser) {
  s_char s = __gab_prsprevsrc(parser);

  s.len--;

  return gab_nstring(gab, s.len, s.data);
}

/* A trim-front-and-back version of previd */
GAB_INTERNAL gab_value __gab_tprsprevid(struct gab_triple gab,
                                        struct parser *parser) {
  s_char s = __gab_prsprevsrc(parser);

  s.data++;
  s.len -= 2;

  // These can cause collections during compilation.
  return gab_nstring(gab, s.len, s.data);
}

GAB_INTERNAL bool __gab_prstokmatch(struct parser *parser, gab_token tok) {
  return v_gab_token_val_at(&parser->src->tokens, parser->offset) == tok;
}

GAB_INTERNAL int64_t __gab_vprserror(struct gab_triple gab,
                                     struct parser *parser, enum gab_status e,
                                     const char *fmt, va_list args) {
  parser->err = gab_vspanicf(gab, args,
                             (struct gab_err_argt){
                                 .src = parser->src,
                                 .status = e,
                                 .tok = parser->offset ? parser->offset - 1 : 0,
                                 .note_fmt = fmt,
                             });

  va_end(args);

  return 0;
}

GAB_INTERNAL int64_t __gab_prserror(struct gab_triple gab,
                                    struct parser *parser, enum gab_status e,
                                    const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  return __gab_vprserror(gab, parser, e, fmt, args);
}

GAB_INTERNAL int64_t __gab_prstokeat(struct gab_triple gab,
                                     struct parser *parser) {
  if (__gab_prstokmatch(parser, TOKEN_EOF))
    return __gab_prserror(gab, parser, GAB_UNEXPECTED_EOF,
                          "Unexpectedly reached the end of input.");

  parser->offset++;

  if (__gab_prstokmatch(parser, TOKEN_ERROR)) {
    __gab_prstokeat(gab, parser);
    return __gab_prserror(gab, parser, GAB_MALFORMED_TOKEN,
                          "This token is malformed or unrecognized.");
  }

  return 1;
}

/* Match a token against a list of tokens and eat if matched */
GAB_INTERNAL int64_t __gab_nprstokmatcheat(struct gab_triple gab,
                                           struct parser *parser, uint64_t len,
                                           gab_token tok[len]) {
  for (uint64_t i = 0; i < len; i++)
    if (__gab_prstokmatch(parser, tok[i]))
      return (tok[i] == TOKEN_EOF) ? 1 : __gab_prstokeat(gab, parser);

  return 0;
}

#define __gab_prstokmatcheat(gab, parser, ...)                                 \
  ({                                                                           \
    gab_token toks[] = {__VA_ARGS__};                                          \
    __gab_nprstokmatcheat(gab, parser, sizeof(toks) / sizeof(gab_token),       \
                          toks);                                               \
  })

GAB_INTERNAL gab_value __gab_prsexp(struct gab_triple gab,
                                    struct parser *parser, enum prec_k prec);

GAB_INTERNAL void __gab_prsnewlines(struct gab_triple gab,
                                    struct parser *parser) {
  while (__gab_prstokmatcheat(gab, parser, TOKEN_NEWLINE))
    ;
}

GAB_INTERNAL uint64_t node_getinfo_begin(struct gab_src *src, gab_value node) {
  return d_uint64_t_read(&src->node_begin_toks, node);
}

GAB_INTERNAL gab_value __gab_nodeinfoput(struct gab_src *src, gab_value node,
                                         uint64_t begin, uint64_t end) {
  d_uint64_t_insert(&src->node_begin_toks, node, begin);
  d_uint64_t_insert(&src->node_end_toks, node, end);
  return node;
}

GAB_INTERNAL gab_value __gab_nodeinfosteal(struct gab_src *src, gab_value from,
                                           gab_value to) {
  uint64_t begin = d_uint64_t_read(&src->node_begin_toks, from);
  uint64_t end = d_uint64_t_read(&src->node_end_toks, from);
  return __gab_nodeinfoput(src, to, begin, end);
}

GAB_INTERNAL gab_value __gab_nodeval(struct gab_triple gab, gab_value node) {
  return gab_listof(gab, node);
}

GAB_INTERNAL gab_value __gab_nodeempty(struct gab_triple gab,
                                       struct parser *parser) {
  gab_value empty = gab_listof(gab);
  __gab_nodeinfoput(parser->src, empty, parser->offset, parser->offset);
  return empty;
  ;
}

GAB_INTERNAL bool __gab_nodeisempty(gab_value node) {
  return gab_valkind(node) == kGAB_RECORD && gab_reclen(node) == 0;
}

GAB_INTERNAL bool __gab_nodeismulti(struct gab_triple gab, gab_value node) {
  if (gab_valkind(node) != kGAB_RECORD)
    return false;

  switch (gab_valkind(gab_recshp(node))) {
  case kGAB_SHAPE:
    return !__gab_prsisbuiltin(gab,
                               gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG));
  case kGAB_SHAPELIST: {
    uint64_t len = gab_reclen(node);

    if (len == 0)
      return false;

    for (uint64_t i = 0; i < len; i++) {
      gab_value child_node = gab_uvrecat(node, i);
      if (__gab_nodeismulti(gab, child_node))
        return true;
    }

    return false;
  }
  default:
    gab_unreachable("Impossible ast node kind");
    return false;
  }
}

GAB_INTERNAL uint64_t __gab_nodelen(struct gab_triple gab, gab_value node);

GAB_INTERNAL gab_value __gab_nodetuplast(gab_value node) {
  gab_precondition(gab_valkind(node) == kGAB_RECORD, "Invalid tuple node kind");
  gab_precondition(gab_valkind(gab_recshp(node)) == kGAB_SHAPELIST,
                   "Invalid tuple node kind");

  uint64_t len = gab_reclen(node);

  gab_precondition(len > 0, "Tuple shall have elements");

  return gab_uvrecat(node, len - 1);
}

GAB_INTERNAL uint64_t __gab_nodevallen(struct gab_triple gab, gab_value node) {
  // If this value node is a block, get the
  // last tuple in the block and return that
  // tuple's len
  if (gab_valkind(node) == kGAB_RECORD)
    if (gab_valkind(gab_recshp(node)) == kGAB_SHAPELIST)
      if (gab_reclen(node) > 0)
        return __gab_nodelen(gab, __gab_nodetuplast(node));

  // Otherwise, values are 1 long
  return 1;
}

GAB_INTERNAL uint64_t __gab_nodelen(struct gab_triple gab, gab_value node) {
  if (gab_valkind(node) != kGAB_RECORD)
    return 0;

  gab_assert(gab_valkind(gab_recshp(node)) == kGAB_SHAPELIST,
             "Shall not get length of non-list record node.");

  uint64_t len = gab_reclen(node);
  uint64_t total_len = 0;

  // Tuple's length are the sum of their children
  // If the tuple as a whole is multi, subtract one
  // for that send
  for (uint64_t i = 0; i < len; i++)
    total_len += __gab_nodevallen(gab, gab_uvrecat(node, i));

  return total_len;
}

GAB_INTERNAL gab_value __gab_nodesend(struct gab_triple gab, gab_value lhs,
                                      gab_value msg, gab_value rhs) {
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

  return __gab_nodeval(gab, gab_mrecord(gab, 1, 3, keys, vals));
}

/* Parses a list of tuples into a single expression value */
GAB_INTERNAL gab_value __gab_prsexpbody(struct gab_triple gab,
                                        struct parser *parser,
                                        enum gab_token t) {
  uint64_t begin = parser->offset;

  gab_value result = __gab_nodeempty(gab, parser);

  __gab_prsnewlines(gab, parser);

  while (!__gab_prstokmatcheat(gab, parser, t)) {
    __gab_prsnewlines(gab, parser);

    gab_value exp = __gab_prsexp(gab, parser, kEXP);

    if (exp == gab_cinvalid)
      return gab_cinvalid;

    gab_value tup = __gab_nodeval(gab, exp);
    __gab_nodeinfosteal(parser->src, exp, tup);

    result = gab_lstcat(gab, result, tup);

    if (result == gab_cinvalid)
      return gab_cinvalid;

    __gab_prsnewlines(gab, parser);
  }

  uint64_t end = parser->offset;

  gab_value res = __gab_nodeval(gab, result);

  __gab_nodeinfoput(parser->src, res, begin, end);

  return res;
}

GAB_INTERNAL gab_value __gab_prsexpuntil(struct gab_triple gab,
                                         struct parser *parser,
                                         enum gab_token t) {
  uint64_t begin = parser->offset;

  gab_value result = __gab_nodeempty(gab, parser);

  __gab_prsnewlines(gab, parser);

  while (!__gab_prstokmatcheat(gab, parser, t)) {
    __gab_prsnewlines(gab, parser);

    gab_value exp = __gab_prsexp(gab, parser, kEXP);

    if (exp == gab_cinvalid)
      return gab_cinvalid;

    result = gab_lstcat(gab, result, exp);

    if (result == gab_cinvalid)
      return gab_cinvalid;

    __gab_prsnewlines(gab, parser);
  }

  uint64_t end = parser->offset;

  __gab_nodeinfoput(parser->src, result, begin, end);

  return result;
}

GAB_INTERNAL gab_value __gab_prsexp(struct gab_triple gab,
                                    struct parser *parser, enum prec_k prec) {
  if (!__gab_prstokeat(gab, parser))
    return gab_cinvalid;

  uint64_t tok = __gab_prsprevtok(parser);

  struct parse_rule rule = __gab_prsrule(tok);

  if (rule.prefix == nullptr)
    return __gab_prserror(gab, parser, GAB_MALFORMED_EXPRESSION,
                          FMT_MALFORMED_EXPRESSION),
           gab_cinvalid;

  uint64_t begin = parser->offset;

  gab_value node = rule.prefix(gab, parser, gab_cinvalid);

  uint64_t end = parser->offset;
  uint64_t latest_valid_offset = parser->offset;

  __gab_nodeinfoput(parser->src, node, begin, end);

  __gab_prsnewlines(gab, parser);

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
  while (prec <= __gab_prsrule(__gab_prscurrtok(parser)).prec) {

    if (node == gab_cinvalid)
      return gab_cinvalid;

    if (!__gab_prstokeat(gab, parser))
      return gab_cinvalid;

    rule = __gab_prsrule(__gab_prsprevtok(parser));

    if (rule.infix != nullptr)
      node = rule.infix(gab, parser, node);

    latest_valid_offset = parser->offset;

    __gab_prsnewlines(gab, parser);
  }

  parser->offset = latest_valid_offset;

  end = parser->offset;

  __gab_nodeinfoput(parser->src, node, begin, end);

  return node;
}

/* OPTIONALLY parse an expression with a given precedence */
GAB_INTERNAL gab_value __gab_oprsexpprec(struct gab_triple gab,
                                         struct parser *parser,
                                         enum prec_k prec) {
  if (!__gab_prscurrprefix(parser)) {
    gab_value empty = __gab_nodeempty(gab, parser);
    return empty;
  }

  return __gab_prsexp(gab, parser, prec);
}

GAB_INTERNAL gab_value __gab_prsnum(struct gab_triple gab,
                                    struct parser *parser, gab_value lhs) {
  double num = strtod((char *)__gab_prsprevsrc(parser).data, nullptr);
  return __gab_nodeval(gab, gab_number(num));
}

GAB_INTERNAL gab_value __gab_prsmsg(struct gab_triple gab,
                                    struct parser *parser, gab_value lhs) {
  gab_value id = __gab_tbprsprevid(gab, parser);

  return __gab_nodeval(gab, gab_strtomsg(id));
}

GAB_INTERNAL gab_value __gab_prssym(struct gab_triple gab,
                                    struct parser *parser, gab_value lhs) {
  gab_value id = __gab_prsprevid(gab, parser);

  return __gab_nodeval(gab, gab_strtobin(id));
}

GAB_INTERNAL gab_value __gab_prsdstr(struct gab_triple gab,
                                     struct parser *parser, gab_value lhs) {
  return __gab_nodeval(gab, __gab_tprsprevid(gab, parser));
}

GAB_INTERNAL gab_value __gab_prsstr(struct gab_triple gab,
                                    struct parser *parser, gab_value lhs) {
  a_char *parsed = __gab_prsstrraw(parser, __gab_prsprevsrc(parser));

  if (parsed == nullptr)
    return __gab_prserror(gab, parser, GAB_MALFORMED_STRING,
                          FMT_GAB_MALFORMED_STRING),
           gab_cinvalid;

  gab_value str = gab_nstring(gab, parsed->len, parsed->data);

  a_char_destroy(parsed);

  return __gab_nodeval(gab, str);
}

GAB_INTERNAL gab_value __gab_prsrec(struct gab_triple gab,
                                    struct parser *parser, gab_value lhs) {
  uint64_t begin = parser->offset;

  gab_value result = __gab_prsexpuntil(gab, parser, TOKEN_RBRACK);

  if (result == gab_cinvalid)
    return gab_cinvalid;

  gab_value lhs_node = __gab_nodeval(gab, gab_message(gab, tGAB_RECORD));
  gab_value msg_node = gab_message(gab, mGAB_MAKE);

  gab_value node = __gab_nodesend(gab, lhs_node, msg_node, result);

  uint64_t end = parser->offset;

  __gab_nodeinfoput(parser->src, result, begin, end);
  __gab_nodeinfoput(parser->src, node, begin, end);
  __gab_nodeinfoput(parser->src, lhs_node, begin, end);
  __gab_nodeinfoput(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

GAB_INTERNAL gab_value __gab_prslst(struct gab_triple gab,
                                    struct parser *parser, gab_value lhs) {
  uint64_t begin = parser->offset;

  gab_value result = __gab_prsexpuntil(gab, parser, TOKEN_RBRACE);

  if (result == gab_cinvalid)
    return gab_cinvalid;

  gab_value lhs_node = __gab_nodeval(gab, gab_message(gab, tGAB_LIST));
  gab_value msg_node = gab_message(gab, mGAB_MAKE);

  gab_value node = __gab_nodesend(gab, lhs_node, msg_node, result);

  uint64_t end = parser->offset;

  __gab_nodeinfoput(parser->src, result, begin, end);
  __gab_nodeinfoput(parser->src, node, begin, end);
  __gab_nodeinfoput(parser->src, lhs_node, begin, end);
  __gab_nodeinfoput(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

GAB_INTERNAL gab_value __gab_prsshp(struct gab_triple gab,
                                    struct parser *parser, gab_value lhs) {
  uint64_t begin = parser->offset;

  gab_value result = __gab_prsexpuntil(gab, parser, TOKEN_RBRACK);

  if (result == gab_cinvalid)
    return gab_cinvalid;

  gab_value lhs_node = __gab_nodeval(gab, gab_message(gab, tGAB_SHAPE));
  gab_value msg_node = gab_message(gab, mGAB_MAKE);

  gab_value node = __gab_nodesend(gab, lhs_node, msg_node, result);

  uint64_t end = parser->offset;

  __gab_nodeinfoput(parser->src, result, begin, end);
  __gab_nodeinfoput(parser->src, node, begin, end);
  __gab_nodeinfoput(parser->src, lhs_node, begin, end);
  __gab_nodeinfoput(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

GAB_INTERNAL gab_value __gab_prstup(struct gab_triple gab,
                                    struct parser *parser, gab_value lhs) {
  return __gab_prsexpuntil(gab, parser, TOKEN_RPAREN);
}

GAB_INTERNAL gab_value __gab_prsblk(struct gab_triple gab,
                                    struct parser *parser, gab_value lhs) {
  gab_value res = __gab_prsexpbody(gab, parser, TOKEN_END);

  if (res == gab_cinvalid)
    return gab_cinvalid;

  return res;
}

GAB_INTERNAL gab_value __gab_prssend(struct gab_triple gab,
                                     struct parser *parser, gab_value lhs) {
  uint64_t begin = parser->offset;

  gab_value msg = __gab_tfprsprevid(gab, parser);

  gab_value rhs = __gab_oprsexpprec(gab, parser, kSEND + 1);

  if (rhs == gab_cinvalid)
    return gab_cinvalid;

  gab_value node = __gab_nodesend(gab, lhs, gab_strtomsg(msg), rhs);

  uint64_t end = parser->offset;

  __gab_nodeinfoput(parser->src, node, begin, end);
  __gab_nodeinfoput(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

GAB_INTERNAL gab_value __gab_prssendop(struct gab_triple gab,
                                       struct parser *parser, gab_value lhs) {
  uint64_t begin = parser->offset;

  gab_value msg = __gab_prsprevid(gab, parser);

  gab_value rhs = __gab_oprsexpprec(gab, parser, kBINARY_SEND + 1);

  if (rhs == gab_cinvalid)
    return gab_cinvalid;

  gab_value node = __gab_nodesend(gab, lhs, gab_strtomsg(msg), rhs);

  uint64_t end = parser->offset;

  __gab_nodeinfoput(parser->src, node, begin, end);
  __gab_nodeinfoput(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

/*
 * TODO @feat: What would macros look like?
 *
 * I want to implement '=' and '=>' in userspace somehow if I can.
 *
 * I also want to get rid of OP_BLOCK for the purity of it all - but thats hard.
 * I suppose a macro would sort of solve it, as it could rewrite the () => do
 * ... end Into a Blocks.make(proto) call. This would involve first compiling
 * the rhs do with the lhs binding.
 *
 * The Blocks.make(proto) message would need safeguards somehow.
 * Is there a way it could check that its at the correct spot in the bytecode?
 *
 * Should we expand macros at this point?
 *
 * It would kind of make sense otherwise the only difference between a macro
 * and a send is the precedence.
 *
 * :> and := each affect *environments*. They're not very good candidates for
 * macros either. (For example, clojure's let and fn are special forms, not
 * macros or functions)
 *
 * Maybe this is a reason to have more of a :let kind of struscture?
 *
 * (a b) :fn  do
 * end
 *
 * (a 1 c 2) :in do
 * end
 *
 * This would allow macro impl to not include env.
 *
 * a := 2
 *
 * How does this make it any easier?
 *
 * The :fn or :> macro would be able to compile the rhs with the lhs bindings,
 * and then emit a Blocks.make(proto).
 *
 * But the := or :in *can't* be just macros, because they *compile* differently.
 * They have to emit a bunch of store-local instructions.
 *
 * This happens completely with the `unpack_binding_into_env` function.
 *
 * The notion of ENV is completely at *compile* time, not *parse* time. Macros
 * should be between *parse* and *compile* - so should not intersect.
 *
 * Lisps and Schemes have special forms `let` and `fn`, they do not handle this
 * in userspace. And I don't see a way to do so reasonably.
 *
 * This leaves the options of either creating special syntax for let and fn, or
 * using special sends (as is currently, with = and =>)
 *
 * a := 2
 *
 * (a, z*, b) :> do
 *    z*
 * end
 *
 * I think I am deciding against adding macros. I don't mind leaving some
 * infrastructure for them, but I think they are mostly a bad/unnecessary idea.
 *
 * I need to decide on a specific syntax for blocks though, I don't love :>
 *
 */
GAB_INTERNAL gab_value __gab_prsbuiltin(struct gab_triple gab,
                                        struct parser *parser, gab_value lhs) {
  uint64_t begin = parser->offset;

  gab_value msg = __gab_prsprevid(gab, parser);

  gab_value rhs = __gab_prsexp(gab, parser, kEXP);

  if (rhs == gab_cinvalid)
    return gab_cinvalid;

  gab_value node = __gab_nodesend(gab, lhs, gab_strtobin(msg), rhs);

  uint64_t end = parser->offset;

  __gab_nodeinfoput(parser->src, node, begin, end);
  __gab_nodeinfoput(parser->src, gab_uvrecat(node, 0), begin, end);

  return node;
}

const struct parse_rule parse_rules[] = {
    {__gab_prsblk, nullptr, kNONE},           // DO
    {nullptr, nullptr, kNONE},                // END
    {nullptr, __gab_prsbuiltin, kBUILTIN},    // LAMBDA
    {nullptr, __gab_prsbuiltin, kBUILTIN},    // IN
    {__gab_prslst, nullptr, kNONE},           // LBRACE
    {nullptr, nullptr, kNONE},                // RBRACE
    {__gab_prsrec, nullptr, kNONE},           // LBRACK
    {nullptr, nullptr, kNONE},                // RBRACK
    {__gab_prstup, nullptr, kNONE},           // LPAREN
    {nullptr, nullptr, kNONE},                // RPAREN
    {nullptr, __gab_prssend, kSEND},          // SEND
    {nullptr, __gab_prssendop, kBINARY_SEND}, // OPERATOR
    {__gab_prssym, nullptr, kNONE},           // SYMBOL
    {__gab_prsmsg, nullptr, kNONE},           // MESSAGE
    {__gab_prsstr, nullptr, kNONE},           // STRING
    {__gab_prsdstr, nullptr, kNONE},          // STRING
    {__gab_prsnum, nullptr, kNONE},           // NUMBER
    {__gab_prsshp, nullptr, kNONE},           // SLASH
    {nullptr, nullptr, kNONE},                // NEWLINE
    {nullptr, nullptr, kNONE},                // EOF
    {nullptr, nullptr, kNONE},                // ERROR
};

GAB_INTERNAL struct parse_rule __gab_prsrule(gab_token k) {
  return parse_rules[k];
}

GAB_INTERNAL gab_value __gab_parse(struct gab_triple gab,
                                   struct parser *parser) {
  uint64_t begin = parser->offset;

  if (__gab_prscurrtok(parser) == TOKEN_EOF)
    return __gab_prstokeat(gab, parser),
           __gab_prserror(gab, parser, GAB_UNEXPECTED_EOF, ""), gab_cinvalid;

  if (__gab_prscurrtok(parser) == TOKEN_ERROR)
    return __gab_prstokeat(gab, parser),
           __gab_prserror(gab, parser, GAB_MALFORMED_TOKEN,
                          "This token is malformed or unrecognized."),
           gab_cinvalid;

  gab_value ast = __gab_prsexpbody(gab, parser, TOKEN_EOF);

  if (ast == gab_cinvalid)
    return gab_cinvalid;

  if (gab.flags & fGAB_AST_DUMP)
    gab_fprintf(stdout, "$\n", gab_pvalintos(gab, ast, ""));

  gab_iref(gab, ast);
  gab_egkeep(gab.eg, ast);

  uint64_t end = parser->offset;

  __gab_nodeinfoput(parser->src, ast, begin, end);

  return ast;
}

GAB_API union gab_value_pair gab_parse(struct gab_triple gab,
                                       struct gab_parse_argt args) {
  gab.flags |= args.flags;

  args.name = args.name ? args.name : "__main__";

  gab_gclock(gab);

  gab_value name = gab_string(gab, args.name);

  struct gab_src *src =
      __gab_source(gab, name, (char *)args.source,
                   args.source_len ? args.source_len : strlen(args.source) + 1);

  struct parser parser = {.src = src, .err = gab_cundefined};

  gab_value ast = __gab_parse(gab, &parser);

  gab_gcunlock(gab);

  gab_assert(ast != gab_cinvalid || parser.err != gab_cundefined,
             "Shall either have an ast or an error");

  if (ast == gab_cinvalid)
    return (union gab_value_pair){.status = gab_cinvalid,
                                  .vresult = parser.err};
  else
    return (union gab_value_pair){.status = gab_cvalid, .vresult = ast};
}

/* ----------------------------------------
 *
 *    GAB BYTECODE COMIPLER
 *
 *  This section contains the code for emitting bytecode from an AST.
 * ----------------------------------------
 */

GAB_INTERNAL void __gab_bcdestroy(struct bc *bc) {
  v_uint8_t_destroy(&bc->bc);
  v_uint64_t_destroy(&bc->bc_toks);
}

GAB_INTERNAL int64_t __gab_vbcerror(struct gab_triple gab, struct bc *bc,
                                    gab_value node, enum gab_status e,
                                    const char *fmt, va_list args) {
  uint64_t tok = d_uint64_t_read(&bc->src->node_begin_toks, node);

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

GAB_INTERNAL int64_t __gab_bcerror(struct gab_triple gab, struct bc *bc,
                                   gab_value node, enum gab_status e,
                                   const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  return __gab_vbcerror(gab, bc, node, e, fmt, args);
}

GAB_INTERNAL void __gab_obcpush(struct bc *bc, uint8_t op, gab_value node) {
  bc->pprev_op = bc->prev_op;
  bc->prev_op = op;
  bc->prev_op_at = v_uint8_t_push(&bc->bc, op);

  uint64_t offset = node_getinfo_begin(bc->src, node);

  v_uint64_t_push(&bc->bc_toks, offset - (offset != 0));
}

GAB_INTERNAL void __gab_bbcpush(struct bc *bc, uint8_t data, gab_value node) {
  uint64_t offset = node_getinfo_begin(bc->src, node);

  v_uint8_t_push(&bc->bc, data);
  v_uint64_t_push(&bc->bc_toks, offset - (offset != 0));
}

GAB_INTERNAL void __gab_sbcpush(struct bc *bc, uint16_t data, gab_value node) {
  __gab_bbcpush(bc, (data >> 8) & 0xff, node);
  __gab_bbcpush(bc, data & 0xff, node);
}

GAB_INTERNAL uint16_t __gab_bcaddk(struct gab_triple gab, struct bc *bc,
                                   gab_value value) {
  gab_egkeep(gab.eg, gab_iref(gab, value));

  gab_assert(bc->ks->len < UINT16_MAX, "Too many constants in module");

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

GAB_INTERNAL void __gab_bcbyte_arg_make_multi(struct bc *bc,
                                              struct inst_arg arg,
                                              struct super_instruction si,
                                              gab_value node,
                                              int multiarg_offset) {
  // Transition a single-byte-arg instruction to a multi-byte-arg
  // instruction.
  uint64_t multi_arg = bc->prev_op_at + multiarg_offset;
  uint8_t prev_multi = v_uint8_t_val_at(&bc->bc, multi_arg);

  // Change the previous byte arg to now correspond to the number of bytes
  // to follow (2).
  v_uint8_t_set(&bc->bc, multi_arg, 2);

  // Push the previous byte value, and the new one.
  __gab_bbcpush(bc, prev_multi, node);
  __gab_bbcpush(bc, arg.as.byte_arg, node);

  // Update the old instruction to the new, and the previous op.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

GAB_INTERNAL void __gab_bcmulti_byte_arg_append(struct bc *bc,
                                                struct inst_arg arg,
                                                struct super_instruction si,
                                                gab_value node,
                                                int multiarg_offset) {
  // Append a new byte arg to a multi-byte instruction.
  uint64_t multi_arg = bc->prev_op_at + multiarg_offset;
  uint8_t multi = v_uint8_t_val_at(&bc->bc, multi_arg);

  // Increment the multi-arg count.
  v_uint8_t_set(&bc->bc, multi_arg, multi + 1);

  // Push the additional byte argument.
  __gab_bbcpush(bc, arg.as.byte_arg, node);

  if (si.from == si.to)
    return;

  // Update the old instruction to the new, and the previous op.
  // We can skip this if the super instruction's from and to ops are the
  // same.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

GAB_INTERNAL void __gab_bcshort_arg_make_multi(struct bc *bc,
                                               struct inst_arg arg,
                                               struct super_instruction si,
                                               gab_value node,
                                               int multiarg_offset) {
  uint64_t multi_arg = bc->prev_op_at + multiarg_offset;

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
  __gab_bbcpush(bc, 2, node);

  // Push the original short argument, and the new second one.
  __gab_sbcpush(bc, prev_arg, node);
  __gab_sbcpush(bc, arg.as.short_arg, node);

  // Update the old instruction to the new, and the previous op.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

GAB_INTERNAL void __gab_bcmulti_short_arg_append(struct bc *bc,
                                                 struct inst_arg arg,
                                                 struct super_instruction si,
                                                 gab_value node,
                                                 int multiarg_offset) {
  // Append a new short arg to a multi-byte instruction.
  uint64_t multi_arg = bc->prev_op_at + multiarg_offset;
  uint8_t multi = v_uint8_t_val_at(&bc->bc, multi_arg);

  // Increment the multi-arg count.
  v_uint8_t_set(&bc->bc, multi_arg, multi + 1);

  // Push the additional short.
  __gab_sbcpush(bc, arg.as.short_arg, node);

  if (si.from == si.to)
    return;

  // Update the old instruction to the new, and the previous op.
  // We can skip this if the super instruction's from and to ops are the
  // same.
  v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
  bc->prev_op = si.to;
}

GAB_INTERNAL void __gab_ibcpush(struct bc *bc, struct inst_arg arg,
                                gab_value node) {
#if cGAB_SUPERINSTRUCTIONS
  for (int i = 0; i < nsuper_instructions; i++) {
    struct super_instruction si = super_instructions[i];

    if (si.from == bc->prev_op && si.via == arg.op) {

      switch (si.k) {
      default:
        gab_unreachable("Invalid superinstruction kind");
        break;
      case kSI_REPLACE: {
        // Push the arg for this new instruction
        switch (arg.k) {
        case kINST_ARG_NONE:
          break;
        case kINST_ARG_BYTE:
          __gab_bbcpush(bc, arg.as.byte_arg, node);
          break;
        case kINST_ARG_SHORT:
          __gab_sbcpush(bc, arg.as.short_arg, node);
          break;
        }

        // Update the old instruction to the new, and the previous op.
        v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
        bc->prev_op = si.to;
        break;
      }
      case kSI_MAKE_MULTI: {
        // Push a 2, to include previous op and this repetition.
        __gab_bbcpush(bc, 2, node);

        // Update the instruction to the repeatable target.
        v_uint8_t_set(&bc->bc, bc->prev_op_at, si.to);
        bc->prev_op = si.to;

        break;
      }
      case kSI_MULTI_APPEND: {
        uint64_t multi_arg = bc->prev_op_at + 1;
        uint8_t multi = v_uint8_t_val_at(&bc->bc, multi_arg);

        // Increment the multi-arg count.
        v_uint8_t_set(&bc->bc, multi_arg, multi + 1);

        // Leave the instruction, as we are just adding a repetition
        break;
      }
      case kSI_BYTE_ARG_MAKE_MULTI:
        __gab_bcbyte_arg_make_multi(bc, arg, si, node, 1);
        break;
      case kSI_BYTE_ARG_MAKE_MULTI2:
        __gab_bcbyte_arg_make_multi(bc, arg, si, node, 2);
        break;
      case kSI_MULTI_BYTE_ARG_APPEND:
        __gab_bcmulti_byte_arg_append(bc, arg, si, node, 1);
        break;
      case kSI_MULTI2_BYTE_ARG_APPEND:
        __gab_bcmulti_byte_arg_append(bc, arg, si, node, 2);
        break;
      case kSI_SHORT_ARG_MAKE_MULTI:
        __gab_bcshort_arg_make_multi(bc, arg, si, node, 1);
        break;
      case kSI_SHORT_ARG_MAKE_MULTI2:
        __gab_bcshort_arg_make_multi(bc, arg, si, node, 2);
        break;
      case kSI_MULTI_SHORT_ARG_APPEND:
        __gab_bcmulti_short_arg_append(bc, arg, si, node, 1);
        break;
      case kSI_MULTI2_SHORT_ARG_APPEND:
        __gab_bcmulti_short_arg_append(bc, arg, si, node, 2);
        break;
      };
      return;
    }
  }
#endif

  __gab_obcpush(bc, arg.op, node);

  switch (arg.k) {
  case kINST_ARG_NONE:
    break;
  case kINST_ARG_BYTE:
    __gab_bbcpush(bc, arg.as.byte_arg, node);
    break;
  case kINST_ARG_SHORT:
    __gab_sbcpush(bc, arg.as.short_arg, node);
    break;
  }
};

GAB_INTERNAL void __gab_kbcpush(struct bc *bc, uint16_t k, gab_value node) {
  __gab_ibcpush(bc,
                (struct inst_arg){
                    .op = OP_CONSTANT,
                    .k = kINST_ARG_SHORT,
                    .as.short_arg = k,
                },
                node);
}

GAB_INTERNAL void __gab_bcloadi(struct bc *bc, gab_value i, gab_value node) {
  gab_precondition(i == gab_cinvalid || i == gab_true || i == gab_false ||
                       i == gab_nil,
                   "Invalid immediate");

  switch (i) {
  case gab_nil:
    __gab_kbcpush(bc, 0, node);
    break;
  case gab_false:
    __gab_kbcpush(bc, 1, node);
    break;
  case gab_true:
    __gab_kbcpush(bc, 2, node);
    break;
  case gab_ok:
    __gab_kbcpush(bc, 3, node);
    break;
  case gab_err:
    __gab_kbcpush(bc, 4, node);
    break;
  case gab_none:
    __gab_kbcpush(bc, 5, node);
    break;
  default:
    gab_unreachable("Impossible constant kind");
    break;
  }
};

GAB_INTERNAL void __gab_bcloadni(struct bc *bc, gab_value v, int n,
                                 gab_value node) {
  for (int i = 0; i < n; i++)
    __gab_bcloadi(bc, v, node);
}

GAB_INTERNAL void __gab_bcloadk(struct gab_triple gab, struct bc *bc,
                                gab_value k, gab_value node) {
  __gab_kbcpush(bc, __gab_bcaddk(gab, bc, k), node);
}

GAB_INTERNAL void __gab_bcloadl(struct bc *bc, uint8_t local, gab_value node) {
  __gab_ibcpush(bc,
                (struct inst_arg){
                    .k = kINST_ARG_BYTE,
                    .op = OP_LOAD_LOCAL,
                    .as.byte_arg = local,
                },
                node);
  return;
}

GAB_INTERNAL void __gab_bcstorel(struct bc *bc, uint32_t local,
                                 gab_value node) {
  gab_assert(local < GAB_LOCAL_MAX, "Local %i must be less than %i", local,
             GAB_LOCAL_MAX);

  __gab_ibcpush(bc,
                (struct inst_arg){
                    .k = kINST_ARG_BYTE,
                    .op = OP_STORE_LOCAL,
                    .as.byte_arg = local,
                },
                node);
}

GAB_INTERNAL void __gab_bcloadu(struct bc *bc, uint32_t upv, gab_value node) {
  gab_assert(upv < GAB_UPVALUE_MAX, "Upvalue %i must be less than %i", upv,
             GAB_LOCAL_MAX);

  __gab_ibcpush(bc,
                (struct inst_arg){
                    .k = kINST_ARG_BYTE,
                    .op = OP_LOAD_UPVALUE,
                    .as.byte_arg = upv,
                },
                node);
}

GAB_INTERNAL void __gab_bcsend(struct gab_triple gab, struct bc *bc,
                               gab_value m, gab_value node) {
  if (gab_valkind(m) == kGAB_STRING)
    m = gab_strtomsg(m);

  gab_precondition(gab_valkind(m) == kGAB_MESSAGE,
                   "Invalid kind for message send");

  uint16_t ks = __gab_bcaddk(gab, bc, m);
  __gab_bcaddk(gab, bc, gab_cinvalid);

  for (int i = 0; i < cGAB_SEND_CACHE_LEN * GAB_SEND_CACHE_SIZE; i++)
    __gab_bcaddk(gab, bc, gab_cinvalid);

  __gab_obcpush(bc, OP_SEND, node);
  __gab_sbcpush(bc, ks, node);
}

GAB_INTERNAL void __gab_bcpop(struct bc *bc, uint8_t n, gab_value node) {
  if (n > 1) {
    __gab_obcpush(bc, OP_POP_N, node);
    __gab_bbcpush(bc, n, node);
    return;
  }

  __gab_ibcpush(bc,
                (struct inst_arg){
                    .k = kINST_ARG_NONE,
                    .op = OP_POP,
                },
                node);
}

// fix this to work with sends in the middle of tuples, doesn't trim properly
// now
GAB_INTERNAL bool __gab_bctrimnode(struct gab_triple gab, struct bc *bc,
                                   uint8_t want, gab_value values,
                                   gab_value node) {
  if (bc->prev_op == OP_TRIM) {
    v_uint8_t_set(&bc->bc, bc->prev_op_at + 1, want);
    return true;
  }

  if (values == gab_cinvalid) {
    __gab_obcpush(bc, OP_TRIM, node);
    __gab_bbcpush(bc, want, node);
    return true;
  }

  uint64_t len = __gab_nodelen(gab, values);

  if (__gab_nodeismulti(gab, values)) {
    if (want == 0) {
      __gab_obcpush(bc, OP_TRIM, node);
      __gab_bbcpush(bc, 0, node);
      return true;
    }

    __gab_obcpush(bc, OP_TRIM, node);
    __gab_bbcpush(bc, want, node);
    return true;
  }

  if (len > want) {
    __gab_bcpop(bc, len - want, values);
    return true;
  }

  if (len < want) {
    __gab_bcloadni(bc, gab_nil, want - len, values);
    return true;
  }

  // Nothing needs to be done
  return true;
}

GAB_INTERNAL void __gab_bclistpack(struct gab_triple gab, struct bc *bc,
                                   uint8_t below, uint8_t above,
                                   gab_value node) {
  __gab_obcpush(bc, OP_PACK_LIST, node);
  __gab_bbcpush(bc, below, node);
  __gab_bbcpush(bc, above, node);
}

GAB_INTERNAL void __gab_bcdictpack(struct gab_triple gab, struct bc *bc,
                                   uint8_t below, uint8_t above,
                                   gab_value node) {
  __gab_obcpush(bc, OP_PACK_DICT, node);
  __gab_bbcpush(bc, below, node);
  __gab_bbcpush(bc, above, node);
}

GAB_INTERNAL void __gab_bcret(struct gab_triple gab, struct bc *bc,
                              gab_value tup, gab_value node) {
  gab_assert(__gab_nodelen(gab, tup) < 16, "Node length shall not exceed 15");

  bool is_multi = __gab_nodeismulti(gab, tup);
  uint64_t len = __gab_nodelen(gab, tup);

  if (len && is_multi)
    len--;

#if cGAB_TAILCALL
  if (len == 0) {
    switch (bc->prev_op) {
    case OP_SEND: {
      uint8_t first_short_byte = v_uint8_t_val_at(&bc->bc, bc->bc.len - 2);
      gab_assert(!(first_short_byte & fHAVE_TAIL),
                 "fHAVE_TAIL shall not already be set on byte");

      v_uint8_t_set(&bc->bc, bc->bc.len - 2, first_short_byte | fHAVE_TAIL);
      __gab_obcpush(bc, OP_RETURN, node);

      return;
    }
    case OP_TRIM: {
      if (bc->pprev_op != OP_SEND)
        break;

      uint8_t first_short_byte = v_uint8_t_val_at(&bc->bc, bc->bc.len - 4);
      gab_assert(!(first_short_byte & fHAVE_TAIL),
                 "fHAVE_TAIL shall not already be set on byte");

      v_uint8_t_set(&bc->bc, bc->bc.len - 4, first_short_byte | fHAVE_TAIL);
      bc->prev_op = bc->pprev_op;
      bc->bc.len -= 2;
      bc->bc_toks.len -= 2;
      __gab_obcpush(bc, OP_RETURN, node);

      return;
    }
    }
  }
#endif

  __gab_obcpush(bc, OP_RETURN, node);
  return;
}

GAB_INTERNAL void __gab_bcpatchinit(struct bc *bc, uint8_t nlocals) {
  if (v_uint8_t_val_at(&bc->bc, 0) == OP_TRIM)
    v_uint8_t_set(&bc->bc, 1, nlocals);
  else if (v_uint8_t_val_at(&bc->bc, 3) == OP_TRIM)
    v_uint8_t_set(&bc->bc, 4, nlocals);
  else
    gab_unreachable("Imposible init patch");
}

GAB_INTERNAL uint64_t __gab_envlocals(gab_value env) {
  uint64_t n = 0, len = gab_reclen(env);

  for (uint64_t i = 0; i < len; i++) {
    gab_value num_or_nil = gab_uvrecat(env, i);

    if (num_or_nil == gab_nil)
      n++;
  }

  return n;
}

GAB_INTERNAL uint64_t __gab_envupvalues(gab_value env) {
  uint64_t n = 0, len = gab_reclen(env);

  for (uint64_t i = 0; i < len; i++) {
    gab_value num_or_nil = gab_uvrecat(env, i);

    if (gab_valkind(num_or_nil) == kGAB_NUMBER)
      n++;
  }

  return n;
}

GAB_INTERNAL gab_value __gab_envpeek(gab_value env, int depth) {
  uint64_t nenv = gab_reclen(env);

  if (depth + 1 > nenv)
    return gab_cundefined;

  return gab_uvrecat(env, nenv - depth - 1);
}

GAB_INTERNAL gab_value __gab_envput(struct gab_triple gab, gab_value env,
                                    int depth, gab_value new_ctx) {
  uint64_t nenv = gab_reclen(env);

  gab_precondition(depth + 1 <= nenv, "Depth %i out of range %lu", depth, nenv);
  return gab_urecput(gab, env, nenv - depth - 1, new_ctx);
}

struct lookup_res {
  gab_value env;

  enum {
    kLOOKUP_NONE,
    kLOOKUP_UPV,
    kLOOKUP_LOC,
    kLOOKUP_LOC_TOOMANY,
    kLOOKUP_UPV_TOOMANY,
  } k;

  int32_t idx;
};

GAB_INTERNAL struct lookup_res
__gab_envputloc(struct gab_triple gab, gab_value env, gab_value binding) {
  uint32_t nlocals = __gab_envlocals(env);

  if (nlocals >= GAB_LOCAL_MAX)
    return (struct lookup_res){env, kLOOKUP_LOC_TOOMANY};

  env = gab_recput(gab, env, binding, gab_nil);
  return (struct lookup_res){env, kLOOKUP_LOC, nlocals};
}

/*
 * Modify the given environment to capture the message 'id'.
 * If already captured, the environment is unchanged.
 */
GAB_INTERNAL struct lookup_res
__gab_envputupv(struct gab_triple gab, gab_value env, gab_value id, int depth) {
  gab_value ctx = __gab_envpeek(env, depth);

  if (ctx == gab_cundefined)
    return (struct lookup_res){env, kLOOKUP_NONE};

  // Don't pull redundant upvalues
  gab_value current_upv_idx = gab_recat(ctx, id);

  if (current_upv_idx != gab_cundefined)
    return (struct lookup_res){env, kLOOKUP_UPV, gab_valtoi(current_upv_idx)};

  uint16_t count = __gab_envupvalues(ctx);

  if (count >= GAB_UPVALUE_MAX)
    return (struct lookup_res){env, kLOOKUP_UPV_TOOMANY};

  gab_verify(!gab_rechas(ctx, id),
             "The environment shall not already have the id");

  ctx = gab_recput(gab, ctx, id, gab_number(count));
  env = __gab_envput(gab, env, depth, ctx);

  return (struct lookup_res){env, kLOOKUP_UPV, count};
}

GAB_INTERNAL int64_t __gab_envupvat(gab_value ctx, gab_value id) {
  gab_precondition(gab_valkind(gab_recat(ctx, id)) == kGAB_NUMBER,
                   "Upvalue shall exist");
  return gab_valtoi(gab_recat(ctx, id));
}

GAB_INTERNAL int64_t __gab_envlocat(gab_value ctx, gab_value id) {
  uint64_t idx = 0, len = gab_reclen(ctx);

  for (uint64_t i = 0; i < len; i++) {
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
GAB_INTERNAL int64_t __gab_envlocal(struct gab_triple gab, gab_value env,
                                    gab_value id, uint8_t depth) {
  gab_value ctx = __gab_envpeek(env, depth);

  if (ctx == gab_cundefined)
    return -1;

  return __gab_envlocat(ctx, id);
}

GAB_INTERNAL struct lookup_res __gab_envupvalue(struct gab_triple gab,
                                                gab_value env, gab_value name,
                                                uint8_t depth) {
  uint64_t nenvs = gab_reclen(env);

  if (depth >= nenvs)
    return (struct lookup_res){env, kLOOKUP_NONE};

  int local = __gab_envlocal(gab, env, name, depth + 1);

  if (local >= 0)
    return __gab_envputupv(gab, env, name, depth);

  struct lookup_res res = __gab_envupvalue(gab, env, name, depth + 1);

  if (res.k) // This means we found either a local, or an upvalue
    return __gab_envputupv(gab, res.env, name, depth);

  return (struct lookup_res){env, kLOOKUP_NONE};
}

/* Returns COMP_ERR if an error is encountered,
 * COMP_ID_NOT_FOUND if no matching local or upvalue is found,
 * COMP_RESOLVED_TO_LOCAL if the id was a local, and
 * COMP_RESOLVED_TO_UPVALUE if the id was an upvalue.
 */
GAB_INTERNAL struct lookup_res __gab_envresolve(struct gab_triple gab,
                                                struct bc *bc, gab_value env,
                                                gab_value id) {
  int64_t idx = __gab_envlocal(gab, env, id, 0);

  if (idx == -1)
    return __gab_envupvalue(gab, env, id, 0);
  else if (idx >= GAB_LOCAL_MAX)
    return (struct lookup_res){env, kLOOKUP_LOC_TOOMANY};
  else
    return (struct lookup_res){env, kLOOKUP_LOC, idx};
}

GAB_INTERNAL gab_value __gab_bcsym(struct gab_triple gab, struct bc *bc,
                                   gab_value tuple, gab_value id,
                                   gab_value env) {
  struct lookup_res res = __gab_envresolve(gab, bc, env, id);

  switch (res.k) {
  case kLOOKUP_LOC:
    __gab_bcloadl(bc, res.idx, tuple);
    return res.env;
  case kLOOKUP_UPV:
    __gab_bcloadu(bc, res.idx, tuple);
    return res.env;
  case kLOOKUP_LOC_TOOMANY:
    __gab_bcerror(gab, bc, tuple, GAB_TOO_MANY_LOCALS, "");
    return gab_cinvalid;
  case kLOOKUP_UPV_TOOMANY:
    __gab_bcerror(gab, bc, tuple, GAB_TOO_MANY_UPVALUES, "");
    return gab_cinvalid;
  default:
    __gab_bcerror(gab, bc, tuple, GAB_UNBOUND_SYMBOL, FMT_ID_NOT_FOUND,
                  gab_bintostr(id));
    return gab_cinvalid;
  }
};

GAB_INTERNAL gab_value __gab_bctup(struct gab_triple gab, struct bc *bc,
                                   gab_value node, gab_value env);

GAB_INTERNAL gab_value __gab_bcrec(struct gab_triple gab, struct bc *bc,
                                   gab_value tuple, gab_value node,
                                   gab_value env);

GAB_INTERNAL gab_value __gab_bcval(struct gab_triple gab, struct bc *bc,
                                   gab_value tuple, uint64_t n, gab_value env) {
  gab_value node = gab_uvrecat(tuple, n);

  switch (gab_valkind(node)) {
  case kGAB_NUMBER:
  case kGAB_STRING:
  case kGAB_MESSAGE:
    __gab_bcloadk(gab, bc, node, tuple);
    return env;

  case kGAB_BINARY:
    return __gab_bcsym(gab, bc, tuple, node, env);

  case kGAB_RECORD:
    return __gab_bcrec(gab, bc, tuple, node, env);

  default:
    gab_unreachable("Impossible uncompilable value");
    return gab_cinvalid;
  }
}

// TODO @feat: Destructuring
// It would be quite useful to automagically do some de-structuring here.
gab_value __gab_bcunpack(struct gab_triple gab, struct bc *bc,
                         gab_value bindings, uint64_t i, gab_value ctx,
                         v_gab_value *targets, int *listpack_at_n,
                         int *recpack_at_n) {
  gab_value binding = gab_uvrecat(bindings, i);

  switch (gab_valkind(binding)) {

  case kGAB_BINARY:
    if (gab_valkind(gab_recat(ctx, binding)) == kGAB_NUMBER) {
      return __gab_bcerror(gab, bc, bindings, GAB_MALFORMED_BINDING,
                           FMT_MALFORMED_BINDING FMT_MALFORMED_BINDING_NOTE
                           "Cannot assign to the captured binding: $.",
                           gab_bintostr(binding)),
             gab_cinvalid;
    }

    struct lookup_res res = __gab_envputloc(gab, ctx, binding);

    if (res.k == kLOOKUP_LOC_TOOMANY)
      return __gab_bcerror(gab, bc, binding, GAB_TOO_MANY_LOCALS, ""),
             gab_cinvalid;

    v_gab_value_push(targets, binding);
    return res.env;

  case kGAB_RECORD: {
    if (gab_valkind(gab_recshp(binding)) == kGAB_SHAPE) {
      // Assume this is a send
      gab_value lhs = gab_mrecat(gab, binding, mGAB_AST_NODE_SEND_LHS);
      gab_value rhs = gab_mrecat(gab, binding, mGAB_AST_NODE_SEND_RHS);
      gab_value m = gab_mrecat(gab, binding, mGAB_AST_NODE_SEND_MSG);

      gab_value rec = gab_uvrecat(lhs, 0);

      /* The binding must not exist as an upvalue */
      if (gab_valkind(gab_recat(ctx, rec)) == kGAB_NUMBER) {
        return __gab_bcerror(gab, bc, bindings, GAB_MALFORMED_BINDING,
                             FMT_MALFORMED_BINDING FMT_MALFORMED_BINDING_NOTE
                             "Cannot assign to the captured binding: $.",
                             gab_bintostr(rec)),
               gab_cinvalid;
      }

      /*
       * Compiling a PACK member.
       */
      if (m == gab_message(gab, mGAB_SPLATLIST)) {
        if (gab_valkind(rec) != kGAB_BINARY)
          goto err;

        if (!__gab_nodeisempty(rhs))
          goto err;

        /* We must not already have a list/dict binding */
        if (*listpack_at_n >= 0 || *recpack_at_n >= 0)
          return __gab_bcerror(
                     gab, bc, binding, GAB_MALFORMED_BINDING,
                     FMT_MALFORMED_BINDING FMT_MALFORMED_BINDING_NOTE
                     "There may only be one binding target with '*' or "
                     "'**'"),
                 gab_cinvalid;

        struct lookup_res res = __gab_envputloc(gab, ctx, rec);

        if (res.k == kLOOKUP_LOC_TOOMANY)
          return __gab_bcerror(gab, bc, binding, GAB_TOO_MANY_LOCALS, ""),
                 gab_cinvalid;

        *listpack_at_n = i;
        v_gab_value_push(targets, rec);
        return res.env;
      }

      /*
       * Compiling a DICT member
       */
      if (m == gab_message(gab, mGAB_SPLATDICT)) {
        if (gab_valkind(rec) != kGAB_BINARY)
          goto err;

        if (!__gab_nodeisempty(rhs))
          goto err;

        if (*listpack_at_n >= 0 || *recpack_at_n >= 0)
          return __gab_bcerror(
                     gab, bc, binding, GAB_MALFORMED_BINDING,
                     FMT_MALFORMED_BINDING FMT_MALFORMED_BINDING_NOTE
                     "There may only be one binding target with '*' or "
                     "'**'"),
                 gab_cinvalid;

        struct lookup_res res = __gab_envputloc(gab, ctx, rec);

        if (res.k == kLOOKUP_LOC_TOOMANY)
          return __gab_bcerror(gab, bc, binding, GAB_TOO_MANY_LOCALS, ""),
                 gab_cinvalid;

        *recpack_at_n = i;
        v_gab_value_push(targets, rec);
        return res.env;
      }

    err:
      return __gab_bcerror(gab, bc, binding, GAB_MALFORMED_BINDING,
                           FMT_MALFORMED_BINDING),
             gab_cinvalid;
    }
  }

  default:
    return __gab_bcerror(gab, bc, binding, GAB_MALFORMED_BINDING,
                         FMT_MALFORMED_BINDING),
           gab_cinvalid;
  }
}

GAB_INTERNAL gab_value __gab_bcenvunpack(struct gab_triple gab, struct bc *bc,
                                         gab_value bindings, gab_value env,
                                         gab_value values) {
  if (!gab_reclen(env))
    return env;

  uint64_t local_ctx = gab_reclen(env) - 1;
  gab_value ctx = gab_uvrecat(env, local_ctx);

  int listpack_at_n = -1, recpack_at_n = -1;

  uint64_t len = gab_reclen(bindings);

  if (!len)
    return env;

  v_gab_value targets = {};

  for (uint64_t i = 0; i < len; i++) {
    ctx = __gab_bcunpack(gab, bc, bindings, i, ctx, &targets, &listpack_at_n,
                         &recpack_at_n);

    if (ctx == gab_cinvalid)
      return v_gab_value_destroy(&targets), ctx;
  }

  uint64_t actual_targets = targets.len;

  if (listpack_at_n >= 0) {
    __gab_bclistpack(gab, bc, listpack_at_n, actual_targets - listpack_at_n - 1,
                     bindings);
  } else if (recpack_at_n >= 0) {
    __gab_bcdictpack(gab, bc, recpack_at_n, actual_targets - recpack_at_n - 1,
                     bindings);
  } else if (!__gab_bctrimnode(gab, bc, actual_targets, values, bindings)) {
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

  for (uint64_t i = 0; i < actual_targets; i++) {
    gab_value target = targets.data[actual_targets - i - 1];

    switch (gab_valkind(target)) {
    case kGAB_BINARY: {
      struct lookup_res res = __gab_envresolve(gab, bc, env, target);

      switch (res.k) {
      case kLOOKUP_LOC:
        __gab_bcstorel(bc, res.idx, bindings);

        if (i + 1 != actual_targets)
          __gab_bcpop(bc, 1, bindings);

        break;
      case kLOOKUP_LOC_TOOMANY:
        return v_gab_value_destroy(&targets),
               __gab_bcerror(gab, bc, bindings, GAB_TOO_MANY_LOCALS, ""),
               gab_cinvalid;
      default:
        gab_unreachable("Impossible environment resolution target");
        break;
      }

      break;
    }
    default:
      return v_gab_value_destroy(&targets),
             __gab_bcerror(gab, bc, bindings, GAB_MALFORMED_BINDING,
                           FMT_MALFORMED_BINDING),
             gab_cinvalid;
    }
  }

  return v_gab_value_destroy(&targets), env;
}

GAB_INTERNAL gab_value __gab_bclmb(struct gab_triple gab, struct bc *bc,
                                   gab_value node, gab_value env) {
  gab_value LHS = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_LHS);
  gab_value RHS = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_RHS);

  gab_value lst = gab_listof(gab, gab_binary(gab, (uint8_t *)"self"));

  env = gab_lstpush(gab, env, gab_erecord(gab));

  gab_value bindings = gab_lstcat(gab, lst, LHS);
  __gab_nodeinfosteal(bc->src, LHS, bindings);

  union gab_value_pair pair = gab_compile(gab, (struct gab_compile_argt){
                                                   .ast = RHS,
                                                   .env = env,
                                                   .bindings = bindings,
                                                   .mod = bc->src->name,
                                               });

  if (pair.status == gab_cinvalid)
    return bc->err = pair.vresult, gab_cinvalid;

  gab_value prt = pair.vresult;
  gab_assert(gab_valkind(prt) == kGAB_PROTOTYPE, "Invalid kind");

  env = gab_recpop(gab, gab_prtenv(prt), nullptr, nullptr);

  __gab_obcpush(bc, OP_BLOCK, RHS);
  __gab_sbcpush(bc, __gab_bcaddk(gab, bc, prt), RHS);

  return env;
}

GAB_INTERNAL gab_value __gab_bcasn(struct gab_triple gab, struct bc *bc,
                                   gab_value node, gab_value env) {
  gab_value lhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_LHS);
  gab_value rhs_node = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_RHS);

  env = __gab_bctup(gab, bc, rhs_node, env);

  if (env == gab_cinvalid)
    return gab_cinvalid;

  env = __gab_bcenvunpack(gab, bc, lhs_node, env, rhs_node);

  if (env == gab_cinvalid)
    return gab_cinvalid;

  return env;
}

GAB_INTERNAL gab_value __gab_bcspc(struct gab_triple gab, struct bc *bc,
                                   gab_value tuple, gab_value node,
                                   gab_value env) {
  gab_value msg = gab_mrecat(gab, node, mGAB_AST_NODE_SEND_MSG);

  if (msg == gab_binary(gab, (uint8_t *)mGAB_ASSIGN))
    return __gab_bcasn(gab, bc, node, env);

  if (msg == gab_binary(gab, (uint8_t *)mGAB_BLOCK))
    return __gab_bclmb(gab, bc, node, env);

  gab_unreachable("Impossible special form");
  return gab_cinvalid;
};

GAB_INTERNAL gab_value __gab_bcrec(struct gab_triple gab, struct bc *bc,
                                   gab_value tuple, gab_value node,
                                   gab_value env) {
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

    gab_assert(lhs_node != gab_cundefined, "Invalid node kind");

    if (__gab_prsisbuiltin(gab, msg))
      return __gab_bcspc(gab, bc, tuple, node, env);

    __gab_ibcpush(bc, (struct inst_arg){OP_TUPLE}, node);

    env = __gab_bctup(gab, bc, lhs_node, env);

    if (env == gab_cinvalid)
      return gab_cinvalid;

    env = __gab_bctup(gab, bc, rhs_node, env);

    if (env == gab_cinvalid)
      return gab_cinvalid;

    __gab_bcsend(gab, bc, msg, node);

    break;
  }
  case kGAB_SHAPELIST: {
    uint64_t len = gab_reclen(node);
    uint64_t last_node = len - 1;

    for (uint64_t i = 0; i < len; i++) {
      gab_value child_node = gab_uvrecat(node, i);

      env = __gab_bctup(gab, bc, child_node, env);

      if (env == gab_cinvalid)
        return gab_cinvalid;

      if (i != last_node)
        if (!__gab_bctrimnode(gab, bc, 0, child_node, child_node))
          return gab_cinvalid;
    }
    break;
  }
  default:
    gab_unreachable("Impossible node kind");
  }

  return env;
}

// Explicit tuple passed in here? Because of sends, where the lhs and rhs are
// each compiled as tuples, but really they are part of one tuple (which may or
// may not need to be cons'd)
GAB_INTERNAL gab_value __gab_bctup(struct gab_triple gab, struct bc *bc,
                                   gab_value node, gab_value env) {
  uint64_t len = gab_reclen(node);

  for (uint64_t i = 0; i < len; i++) {
    env = __gab_bcval(gab, bc, node, i, env);

    if (env == gab_cinvalid)
      return gab_cinvalid;
  }

  return env;
}

GAB_INTERNAL void __gab_envupvdata(gab_value env, uint8_t len, char *data) {
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
  uint64_t nenvs = gab_reclen(env);

  gab_assert(nenvs >= 2, "Shall have at least two envs");

  gab_value ctx = gab_uvrecat(env, nenvs - 1);
  gab_value parent = gab_uvrecat(env, nenvs - 2);

  bool has_grandparent = nenvs >= 3;

  uint64_t nbindings = gab_reclen(ctx);

  uint64_t nlocals = __gab_envlocals(parent);
  uint64_t nupvalues = __gab_envupvalues(parent);

  for (uint64_t i = 0; i < nbindings; i++) {
    gab_value k = gab_ukrecat(ctx, i);
    gab_value v = gab_uvrecat(ctx, i);

    if (v == gab_nil)
      continue;

    bool is_local = has_grandparent ? (gab_recat(parent, k) == gab_nil) : true;

    uint64_t idx =
        is_local ? __gab_envlocat(parent, k) : __gab_envupvat(parent, k);

    gab_assert((is_local && idx < nlocals) || (!is_local && idx < nupvalues),
               "Should be a local or upvalue within respective range");

    uint64_t nth_upvalue = gab_valtou(v);
    gab_assert(nth_upvalue < len, "Should have space for this upvalue");

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
GAB_API union gab_value_pair gab_compile(struct gab_triple gab,
                                         struct gab_compile_argt args) {
  gab_precondition(gab_valkind(args.ast) == kGAB_RECORD,
                   "AST shall be a record");
  gab_precondition(gab_valkind(args.env) == kGAB_RECORD,
                   "ENV shall be a record");
  gab.flags |= args.flags;

  struct gab_src *src = d_gab_src_read(&gab.eg->sources, args.mod);

  if (src == nullptr)
    return (union gab_value_pair){{gab_cinvalid, gab_cinvalid}};

  struct bc bc = {.ks = &src->constants, .src = src, .err = gab_cinvalid};

  args.env = __gab_bcenvunpack(gab, &bc, args.bindings, args.env, gab_cinvalid);

  if (args.env == gab_cinvalid)
    return gab_assert(bc.err != gab_cinvalid,
                      "Shall have valid err in error path"),
           (union gab_value_pair){{gab_cinvalid, bc.err}};

  uint64_t nenvs = gab_reclen(args.env);
  gab_assert(nenvs > 0, "The number of environments must be greater than zero");

  uint64_t nargs = gab_reclen(gab_uvrecat(args.env, nenvs - 1));

  if (!__gab_bctrimnode(gab, &bc, nargs, gab_cinvalid, args.bindings))
    return gab_assert(bc.err != gab_cinvalid,
                      "Shall have valid err in error path"),
           (union gab_value_pair){{gab_cinvalid, bc.err}};

  gab_assert(bc.bc.len == bc.bc_toks.len,
             "Each bytecode instruction should have a corresponding token");

  /*
   * The first tuple in a block is its *arguments*. We don't want to return
   * this tuple from the block when the block returns.
   * We push another, empty tuple here. This will be returned by the block.
   **/
  __gab_ibcpush(&bc, (struct inst_arg){OP_TUPLE}, args.ast);

  args.env = __gab_bctup(gab, &bc, args.ast, args.env);

  gab_assert(bc.bc.len == bc.bc_toks.len,
             "Each bytecode instruction should have a corresponding token");

  if (args.env == gab_cinvalid)
    return gab_assert(bc.err != gab_cinvalid, "Shall have err in error path"),
           __gab_bcdestroy(&bc), (union gab_value_pair){{gab_cinvalid, bc.err}};

  gab_assert(gab_reclen(args.env) == nenvs,
             "Number of environments shall match");

  gab_value local_env = gab_uvrecat(args.env, nenvs - 1);

  gab_assert(bc.bc.len == bc.bc_toks.len,
             "Each bytecode instruction should have a corresponding token");

  __gab_bcret(gab, &bc, args.ast, args.ast);

  uint64_t nlocals = __gab_envlocals(local_env);
  gab_assert(nlocals <= GAB_LOCAL_MAX,
             "Shall not exceed maximum number of locals");

  uint64_t nupvalues = __gab_envupvalues(local_env);
  gab_assert(nupvalues <= GAB_UPVALUE_MAX,
             "Shall not exceed maximum number of upvalues");

  gab_assert(bc.bc.len == bc.bc_toks.len,
             "Each bytecode instruction should have a corresponding token");

  __gab_bcpatchinit(&bc, nlocals);

  uint64_t len = bc.bc.len;
  uint64_t end = __gab_srcappend(src, len, bc.bc.data, bc.bc_toks.data);

  __gab_bcdestroy(&bc);

  uint64_t begin = end - len;

  /*
   * Some blocks may have 0 upvalues.
   * To prevent undefined behavior, just allocate an extra
   * byte that is always unused.
   */
  char data[nupvalues + 1];
  __gab_envupvdata(args.env, nupvalues, data);

  uint64_t bco = d_uint64_t_read(&bc.src->node_begin_toks, args.ast);
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

  gab_assert(bc.err == gab_cinvalid, "Shall not have error in valid path");

  return (union gab_value_pair){
      .status = gab_cvalid,
      .vresult = proto,
  };
}

GAB_API union gab_value_pair gab_build(struct gab_triple gab,
                                       struct gab_parse_argt args) {
  gab.flags |= args.flags;

  args.name = args.name ? args.name : "__main__";

  gab_gclock(gab);

  gab_value mod = gab_string(gab, args.name);

  union gab_value_pair ast = gab_parse(gab, args);

  gab_assert(ast.vresult != gab_cundefined, "Shall have vresult in all cases");

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
      vargs[i] = gab_binary(gab, (uint8_t *)args.argv[i]);

    bindings = gab_list(gab, 1, args.len, vargs);
  }

  __gab_nodeinfoput(src, bindings, 0, 0);

  gab_value env = gab_listof(
      gab, gab_recordof(gab, gab_binary(gab, (uint8_t *)"self"), gab_nil));

  if (ast.status == gab_cinvalid)
    return gab_gcunlock(gab), ast;

  union gab_value_pair res = gab_compile(gab, (struct gab_compile_argt){
                                                  .ast = ast.vresult,
                                                  .env = env,
                                                  .mod = mod,
                                                  .bindings = bindings,
                                              });

  gab_assert(res.vresult != gab_cundefined, "Shall have vresult in all cases");

  if (res.status == gab_cinvalid)
    return gab_gcunlock(gab), res;

  __gab_srccomplete(gab, src);

  gab_value main = gab_block(gab, res.vresult);
  gab_assert(main != gab_cundefined, "Shall have vresult in all cases");

  gab_iref(gab, main);
  gab_iref(gab, res.vresult);
  gab_egkeep(gab.eg, main);
  gab_egkeep(gab.eg, res.vresult);

  return gab_gcunlock(gab),
         (union gab_value_pair){.status = gab_cvalid, .vresult = main};
}

/* ----------------------------------------
 *
 *    GAB GARBAGE COLLECTION
 *
 *  This section contains the code for managing lifetimes of gab objects.
 * ----------------------------------------
 */

GAB_INTERNAL int32_t __gab_gcepoch(struct gab_triple gab) {
  return gab.eg->jobs[gab.wkid].epoch % GAB_GCNEPOCHS;
}

GAB_INTERNAL int32_t __gab_gcepochlast(struct gab_triple gab) {
  return (gab.eg->jobs[gab.wkid].epoch - 1) % GAB_GCNEPOCHS;
}

GAB_INTERNAL void __gab_gcepochinc(struct gab_triple gab) {
#if cGAB_LOG_GC
  fprintf(stderr, "(%i) EPOCHINC\t%i\n", gab.wkid, __gab_gcepoch(gab));
#endif
  gab.eg->jobs[gab.wkid].epoch++;
}

GAB_INTERNAL struct gab_obj **__gab_gcbufdata(struct gab_triple gab, uint8_t b,
                                              uint8_t wkid, uint8_t epoch) {
  gab_assert(epoch < GAB_GCNEPOCHS, "epoch shall not exceed maximum");
  gab_assert(b < kGAB_NBUF, "buffer shall not exceed maximum");
  gab_assert(wkid < gab.eg->len, "wkid shall not exceed maximum");

  return gab.eg->jobs[wkid].buffers[b][epoch].data;
}

GAB_INTERNAL uint64_t __gab_gcbuflen(struct gab_triple gab, uint8_t b,
                                     uint8_t wkid, uint8_t epoch) {
  gab_assert(epoch < GAB_GCNEPOCHS, "epoch shall not exceed maximum");
  gab_assert(b < kGAB_NBUF, "buffer shall not exceed maximum");
  gab_assert(wkid < gab.eg->len, "wkid shall not exceed maximum");

  return gab.eg->jobs[wkid].buffers[b][epoch].len;
}

GAB_INTERNAL bool __gab_gcisdone(struct gab_triple gab) {
  for (int i = 0; i < gab.eg->len; i++) {
    for (int j = 0; j < kGAB_NBUF; j++) {
      for (int k = 0; k < GAB_GCNEPOCHS; k++) {
        if (__gab_gcbuflen(gab, j, i, k) != 0)
          return false;
      }
    }
  }

  return true;
}

GAB_INTERNAL void __gab_gcbufpush(struct gab_triple gab, uint8_t b,
                                  uint8_t wkid, uint8_t epoch,
                                  struct gab_obj *o) {
  gab_assert(epoch < GAB_GCNEPOCHS, "epoch shall not exceed maximum");
  gab_assert(b < kGAB_NBUF, "buffer shall not exceed maximum");
  gab_assert(wkid < gab.eg->len, "wkid shall not exceed maximum");

  uint64_t len = __gab_gcbuflen(gab, b, wkid, epoch);
  gab_assert(len < GAB_GC_MOD_BUFF_MAX, "shall not exceed modbuff max");

  struct gab_obj **buf = __gab_gcbufdata(gab, b, wkid, epoch);
  buf[len] = o;
  gab.eg->jobs[wkid].buffers[b][epoch].len = len + 1;
}

GAB_INTERNAL void __gab_gcbufclear(struct gab_triple gab, uint8_t b,
                                   uint8_t wkid, uint8_t epoch) {
  gab_assert(epoch < GAB_GCNEPOCHS, "epoch shall not exceed maximum");
  gab_assert(b < kGAB_NBUF, "buffer shall not exceed maximum");
  gab_assert(wkid < gab.eg->len, "wkid shall not exceed maximum");

  gab.eg->jobs[wkid].buffers[b][epoch].len = 0;
}

GAB_INTERNAL void __gab_gcobjinc(struct gab_gc *gc, struct gab_obj *obj) {
  if (__gab_unlikely(obj->references == UINT8_MAX)) {
    // The default value returned when the object doesn't exist is UINT8_MAX
    // So we can always use and increment the rc value here.
    uint64_t rc = d_gab_obj_read(&gc->overflow_rc, obj);

    d_gab_obj_insert(&gc->overflow_rc, obj, rc + 1);

    return;
  }

  obj->references++;
}

GAB_INTERNAL void __gab_gcobjdec(struct gab_gc *gc, struct gab_obj *obj) {
  if (__gab_unlikely(obj->references == UINT8_MAX)) {
    uint64_t rc = d_gab_obj_read(&gc->overflow_rc, obj);

    if (__gab_unlikely(rc == UINT8_MAX)) {
      d_gab_obj_remove(&gc->overflow_rc, obj);
      obj->references--;
      return;
    }

    d_gab_obj_insert(&gc->overflow_rc, obj, rc - 1);
    return;
  }

  gab_assert(obj->references != 0,
             "Shall not underflow reference count of object with kind %d.",
             obj->kind);

  obj->references--;
  return;
}

#if cGAB_LOG_GC
#define __gab_gcqdec(gab, obj) (___gab_gcqdec(gab, obj, __FUNCTION__, __LINE__))

GAB_INTERNAL void ___gab_gcqdec(struct gab_triple gab, struct gab_obj *obj,
                                const char *func, int line) {
#else
GAB_INTERNAL void __gab_gcqdec(struct gab_triple gab, struct gab_obj *obj) {
#endif
  int32_t e = __gab_gcepoch(gab);

  while (__gab_gcbuflen(gab, kGAB_BUF_DEC, gab.wkid, e) >=
         GAB_GC_MOD_BUFF_MAX) {
    // Try to signal a collection
    gab_asigcoll(gab);

    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      // In the case where a decrement is missed to this value
      // because we must handle the terminate signal:
      // Simply store this value in the engine's scratch buffer.
      // It will be decremented as the engine is cleaned up.
      gab_egkeep(gab.eg, __gab_obj(obj));
      return;
    default:
      break;
    }

    e = __gab_gcepoch(gab);
  }

  __gab_gcbufpush(gab, kGAB_BUF_DEC, gab.wkid, e, obj);

#if cGAB_LOG_GC
  fprintf(stderr, "(%i) QDEC\t%i\t%p\t%i\t%s:%i\n", gab.wkid,
          __gab_gcepoch(gab), obj, obj->references, func, line);
#endif
}

GAB_INTERNAL void __gab_gcqinc(struct gab_triple gab, struct gab_obj *obj) {
  int32_t e = __gab_gcepoch(gab);

  while (__gab_gcbuflen(gab, kGAB_BUF_INC, gab.wkid, e) >=
         GAB_GC_MOD_BUFF_MAX) {
    // Try to signal a collection
    gab_asigcoll(gab);

    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      // In the case where an increment is missed to this value
      // because we must handle the terminate signal:
      // Immediately perform an increment. This is safe as it can't result
      // in destroying the object.
      // Give the object to the scratch buffer for resolving later.
      __gab_gcobjinc(&gab.eg->gc, obj);
      gab_egkeep(gab.eg, __gab_obj(obj));
      return;
    default:
      break;
    }

    e = __gab_gcepoch(gab);
  }

  __gab_gcbufpush(gab, kGAB_BUF_INC, gab.wkid, e, obj);

#if cGAB_LOG_GC
  fprintf(stderr, "(%i) QINC\t%i\t%p\t%d\n", gab.wkid, __gab_gcepoch(gab), obj,
          obj->references);
#endif
}

GAB_INTERNAL void __gab_gcqdestroy(struct gab_triple gab, struct gab_obj *obj) {
  if (GAB_OBJ_IS_BUFFERED(obj))
    return;

  GAB_OBJ_BUFFERED(obj);

  v_gab_obj_push(&gab.eg->gc.dead[__gab_gcepoch(gab)], obj);

  gab_assert(obj->references == 0, "Shall only qdestroy objects with 0 refs");

#if cGAB_LOG_GC
  fprintf(stderr, "(%i) QDEAD\t%i\t%p\t%d\n", gab.wkid, __gab_gcepoch(gab), obj,
          obj->references);
#endif
}

GAB_INTERNAL void __gab_gcbufeachdo(uint8_t b, uint8_t wkid, uint8_t epoch,
                                    void (*fnc)(struct gab_triple gab,
                                                struct gab_obj *obj),
                                    struct gab_triple gab) {
  struct gab_obj **buf = __gab_gcbufdata(gab, b, wkid, epoch);
  uint64_t len = __gab_gcbuflen(gab, b, wkid, epoch);
  gab_assert(len <= GAB_GC_MOD_BUFF_MAX, "Shall not exceed modbuff len");

#if cGAB_LOG_GC
  fprintf(stderr, "(%i) FORDO\t%i\t(%lu / %i)\n", wkid, epoch, len,
          GAB_GC_MOD_BUFF_MAX);
#endif

  for (uint64_t i = 0; i < len; i++) {
    struct gab_obj *obj = buf[i];

#if cGAB_LOG_GC
    if (GAB_OBJ_IS_FREED(obj)) {
      fprintf(stderr, "UAF\t%p\n", obj);
      exit(1);
    }
#endif

    fnc(gab, obj);
  }

  // Sanity check that buffer hasn't been modified while operating over buffer
#if cGAB_LOG_GC
  if (len != __gab_gcbuflen(gab, b, wkid, epoch)) {
    fprintf(stderr, "INVALID BUFMOD: %d, %i, %i, %lu vs %li\n", b, wkid, epoch,
            len, __gab_gcbuflen(gab, b, wkid, epoch));
    exit(1);
  }
#endif
  gab_assert(len == __gab_gcbuflen(gab, b, wkid, epoch),
             "Buffer length should remain constant while working");
}

GAB_INTERNAL void __gab_gcobjeachdo(struct gab_obj *obj,
                                    void (*fnc)(struct gab_triple gab,
                                                struct gab_obj *obj),
                                    struct gab_triple gab) {
#if cGAB_LOG_GC
  fprintf(stderr, "RECURSE\t%i\t%p\t%i\n", __gab_gcepoch(gab), obj,
          obj->references);
#endif
  switch (obj->kind) {
  default:
    break;

  case kGAB_NATIVE: {
    struct gab_onative *ntv = (struct gab_onative *)obj;

    if (gab_valiso(ntv->name))
      fnc(gab, gab_valtoo(ntv->name));

    break;
  }

  case kGAB_PROTOTYPE: {
    struct gab_oprototype *prt = (struct gab_oprototype *)obj;

    gab_assert(gab_valiso(prt->env), "Assumed to be an object");
    fnc(gab, gab_valtoo(prt->env));

    break;
  }

  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE:
  case kGAB_FIBER: {
    struct gab_ofiber *fib = (struct gab_ofiber *)obj;

    for (uint64_t i = 0; i < fib->len; i++) {
      gab_value o = fib->data[i];

      if (gab_valiso(o))
        fnc(gab, gab_valtoo(o));
    }

    break;
  }

  case kGAB_BOX: {
    struct gab_obox *box = (struct gab_obox *)obj;

    if (gab_valiso(box->type))
      fnc(gab, gab_valtoo(box->type));

    break;
  }

  case (kGAB_BLOCK): {
    struct gab_oblock *b = (struct gab_oblock *)obj;

    for (int i = 0; i < b->nupvalues; i++) {
      if (gab_valiso(b->upvalues[i]))
        fnc(gab, gab_valtoo(b->upvalues[i]));
    }

    break;
  }

  case kGAB_SHAPE:
  case kGAB_SHAPENODE:
  case kGAB_SHAPELIST: {
    struct gab_oshape *s = (struct gab_oshape *)obj;

    for (uint64_t i = 0; i < s->datalen; i++) {
      gab_value v = s->data[i];

      if (gab_valiso(v))
        fnc(gab, gab_valtoo(v));
    }

    break;
  }

  case kGAB_RECORD: {
    struct gab_orec *rec = (struct gab_orec *)obj;
    uint64_t len = (rec->len);

    fnc(gab, gab_valtoo(rec->shape));

    for (uint64_t i = 0; i < len; i++)
      if (gab_valiso(rec->data[i]))
        fnc(gab, gab_valtoo(rec->data[i]));

    break;
  }

  case kGAB_RECORDNODE: {
    struct gab_orecnode *rec = (struct gab_orecnode *)obj;
    uint64_t len = rec->len;

    for (uint64_t i = 0; i < len; i++)
      if (gab_valiso(rec->data[i]))
        fnc(gab, gab_valtoo(rec->data[i]));

    break;
  }
  }
}

GAB_INTERNAL void __gab_gcobjdecref(struct gab_triple gab, struct gab_obj *obj);

#if cGAB_LOG_GC
#define __gab_gcdestroy(gab, obj)                                              \
  ___gab_gcdestroy(gab, obj, __FUNCTION__, __LINE__)
GAB_INTERNAL void ___gab_gcdestroy(struct gab_triple gab, struct gab_obj *obj,
                                   const char *func, int line) {
#else
GAB_INTERNAL void __gab_gcdestroy(struct gab_triple gab, struct gab_obj *obj) {
#endif

  // TODO @cgab @doc: Test/Fix resurrection
  if (obj->references > 0)
    return; // resurrected

  gab_assert(obj->references == 0,
             "Shall only destroy objects with 0 references, not %i on kind %d",
             obj->references, obj->kind);

#if cGAB_LOG_GC
  gab_assert(!GAB_OBJ_IS_FREED(obj), "(%i) DFREE\t%p\t%s:%i\n", gab.wkid, obj,
             func, line);

  fprintf(stderr, "(%i) FREE\t%i\t%p\t%i\t%s:%d\n", gab.wkid,
          __gab_gcepoch(gab), obj, obj->references, func, line);
  __gab_objdestroy(gab, obj);
  GAB_OBJ_FREED(obj);
#else
  __gab_objdestroy(gab, obj);
  gab_egalloc(gab, obj, 0);
#endif
}

GAB_INTERNAL void __gab_gcobjdecref(struct gab_triple gab,
                                    struct gab_obj *obj) {
#if cGAB_LOG_GC
  fprintf(stderr, "(%i) DEC\t%i\t%p\t%d\n", gab.wkid, __gab_gcepoch(gab), obj,
          obj->references - 1);
#endif

  __gab_gcobjdec(&gab.eg->gc, obj);

  if (obj->references == 0) {
    if (!GAB_OBJ_IS_NEW(obj))
      __gab_gcobjeachdo(obj, __gab_gcobjdecref, gab);

    __gab_gcqdestroy(gab, obj);
  }
}

GAB_INTERNAL void __gab_gcobjincref(struct gab_triple gab,
                                    struct gab_obj *obj) {
#if cGAB_LOG_GC
  fprintf(stderr, "INC\t%i\t%p\t%d\n", __gab_gcepoch(gab), obj,
          obj->references + 1);
#endif

  __gab_gcobjinc(&gab.eg->gc, obj);

  if (GAB_OBJ_IS_NEW(obj)) {
#if cGAB_LOG_GC
    fprintf(stderr, "NEW\t%i\t%p\n", __gab_gcepoch(gab), obj);
#endif
    GAB_OBJ_NOT_NEW(obj);
    __gab_gcobjeachdo(obj, __gab_gcobjincref, gab);
  }
}

#if cGAB_LOG_GC
GAB_API void __gab_niref(struct gab_triple gab, uint64_t stride, uint64_t len,
                         gab_value *values, const char *func, int line) {
#else
GAB_API void gab_niref(struct gab_triple gab, uint64_t stride, uint64_t len,
                       gab_value *values) {
#endif
  for (uint64_t i = 0; i < len; i++) {
    gab_value value = values[i * stride];

#if cGAB_LOG_GC
    __gab_iref(gab, value, func, line);
#else
    gab_iref(gab, value);
#endif
  }
}

#if cGAB_LOG_GC
GAB_API void __gab_ndref(struct gab_triple gab, uint64_t stride, uint64_t len,
                         gab_value *values, const char *func, int line) {
#else
GAB_API void gab_ndref(struct gab_triple gab, uint64_t stride, uint64_t len,
                       gab_value *values) {
#endif

  for (uint64_t i = 0; i < len; i++) {
    gab_value value = values[i * stride];

#if cGAB_LOG_GC
    __gab_dref(gab, value, func, line);
#else
    gab_dref(gab, value);
#endif
  }
}

#if cGAB_LOG_GC
GAB_API gab_value __gab_iref(struct gab_triple gab, gab_value value,
                             const char *func, int32_t line) {
#else
GAB_API gab_value gab_iref(struct gab_triple gab, gab_value value) {
#endif
  /*
   * If the value is not a heap object, then do nothing
   */
  if (!gab_valiso(value))
    return value;

  struct gab_obj *obj = gab_valtoo(value);

#if cGAB_LOG_GC
  fprintf(stderr, "(%i) IREF\t%i\t%p\t%d\t%s:%i\n", gab.wkid,
          __gab_gcepoch(gab), obj, obj->references, func, line);
#endif

  __gab_gcqinc(gab, obj);

#if cGAB_DEBUG_GC
  gab_sigcoll(gab);
#endif

  return value;
}

#if cGAB_LOG_GC
GAB_API gab_value __gab_dref(struct gab_triple gab, gab_value value,
                             const char *func, int32_t line) {
#else
GAB_API gab_value gab_dref(struct gab_triple gab, gab_value value) {
#endif
  /*
   * If the value is not a heap object, then do nothing
   */
  if (!gab_valiso(value))
    return value;

  struct gab_obj *obj = gab_valtoo(value);

#if cGAB_DEBUG_GC
  gab_sigcoll(gab);
#endif

#if cGAB_LOG_GC
  if (GAB_OBJ_IS_NEW(obj)) {
    fprintf(stderr, "(%i) NEWDREF\t%i\t%p\t%d\t%s:%i\n", gab.wkid,
            __gab_gcepoch(gab), obj, obj->references, func, line);
  } else {
    fprintf(stderr, "(%i) DREF\t%i\t%p\t%d\t%s:%i\n", gab.wkid,
            __gab_gcepoch(gab), obj, obj->references, func, line);
  }
#endif

#if cGAB_LOG_GC
  ___gab_gcqdec(gab, obj, func, line);
#else
  __gab_gcqdec(gab, obj);
#endif

  return value;
}

GAB_API void gab_gccreate(struct gab_triple gab) {
  d_gab_obj_create(&gab.eg->gc.overflow_rc, 8);
  for (int e = 0; e < GAB_GCNEPOCHS; e++) {
    v_gab_obj_create(&gab.eg->gc.dead[e], 8);
  }

  for (int i = 0; i < gab.eg->len; i++) {
    for (int b = 0; b < kGAB_NBUF; b++) {
      for (int e = 0; e < GAB_GCNEPOCHS; e++) {
        __gab_gcbufclear(gab, b, i, e);
      }
    }
  }
};

GAB_API void gab_gcdestroy(struct gab_triple gab) {
  d_gab_obj_destroy(&gab.eg->gc.overflow_rc);
  for (int e = 0; e < GAB_GCNEPOCHS; e++) {
    v_gab_obj_destroy(&gab.eg->gc.dead[e]);
  }
  __gab_jbunalive(gab, 0);
}

GAB_INTERNAL void __gab_gcdeadreap(struct gab_triple gab) {
  // TODO @cgab @bug: In theory, collect from *last* epoch.
  while (gab.eg->gc.dead[__gab_gcepoch(gab)].len)
    __gab_gcdestroy(gab, v_gab_obj_pop(&gab.eg->gc.dead[__gab_gcepoch(gab)]));
}

GAB_API void gab_gclock(struct gab_triple gab) {
  struct gab_job *wk = gab.eg->jobs + gab.wkid;
  gab_assert(wk->locked < UINT32_MAX, "Shall not exceed maximum");
  wk->locked += 1;
}

/*
 * There was a bug where objects were beging freed *while they were in the
 * locke queue*. This was resolved by marking locked objects as *buffered*
 * until they are unlocked.
 */
GAB_API void gab_gcunlock(struct gab_triple gab) {
  struct gab_job *wk = gab.eg->jobs + gab.wkid;
  gab_assert(wk->locked > 0, "Shall not underflow");
  wk->locked -= 1;

  if (!wk->locked) {
    for (uint64_t i = 0; i < wk->lock_keep.len; i++)
      GAB_OBJ_NOT_BUFFERED(gab_valtoo(v_gab_value_val_at(&wk->lock_keep, i)));

    gab_ndref(gab, 1, wk->lock_keep.len, wk->lock_keep.data);

    wk->lock_keep.len = 0;
  }
}

GAB_INTERNAL void __gab_gcdoincrements(struct gab_triple gab, int32_t epoch) {
#if cGAB_LOG_GC
  fprintf(stderr, "IEPOCH\t%i\n", epoch);
#endif

  for (uint8_t wkid = 0; wkid < gab.eg->len; wkid++) {
    // For the stack and increment buffers, increment the object
    __gab_gcbufeachdo(kGAB_BUF_STK, wkid, epoch, __gab_gcobjincref, gab);
    __gab_gcbufeachdo(kGAB_BUF_INC, wkid, epoch, __gab_gcobjincref, gab);
    // Reset the length of the inc buffer for this worker
    // Leave the stack buffer to be cleared in next epoch by decrement.
    __gab_gcbufclear(gab, kGAB_BUF_INC, wkid, epoch);
  }
#if cGAB_LOG_GC
  fprintf(stderr, "IEPOCH!\t%i\n", epoch);
#endif
}

GAB_INTERNAL void __gab_gcdodecrements(struct gab_triple gab, int32_t epoch) {
#if cGAB_LOG_GC
  fprintf(stderr, "DEPOCH\t%i\n", epoch);
#endif

  for (uint8_t wkid = 0; wkid < gab.eg->len; wkid++) {
    // For the stack and increment buffers, decrement the object
    __gab_gcbufeachdo(kGAB_BUF_STK, wkid, epoch, __gab_gcobjdecref, gab);
    __gab_gcbufeachdo(kGAB_BUF_DEC, wkid, epoch, __gab_gcobjdecref, gab);
    // Reset the length of the dec buffer for this worker
    __gab_gcbufclear(gab, kGAB_BUF_STK, wkid, epoch);
    __gab_gcbufclear(gab, kGAB_BUF_DEC, wkid, epoch);
  }

#if cGAB_LOG_GC
  fprintf(stderr, "DEPOCH!\t%i\n", epoch);
#endif
}

GAB_INTERNAL void __gab_gcdoepoch(struct gab_triple gab, int32_t e) {
  struct gab_job *wk = &gab.eg->jobs[gab.wkid];

#if cGAB_LOG_GC
  fprintf(stderr, "(%i) PEPOCH\t%i\n", gab.wkid, e);
#endif

  if (q_gab_value_is_empty(&wk->working_queue))
    goto fin;

#if cGAB_LOG_GC
  fprintf(stderr, "QUEUENOTEMPTY\t%u\t%u\t%u\n", wk->queue.head, wk->queue.tail,
          wk->queue.head - wk->queue.tail);
#endif

  for (uint64_t idx = wk->working_queue.head; idx != wk->working_queue.tail;
       idx++) {
    gab_value fiber = wk->working_queue.data[idx & (wk->working_queue.cap - 1)];

#if cGAB_LOG_GC
    fprintf(stderr, "PFIBER\t%i\t%i\t%lu\n", e, gab.wkid, idx);
#endif

    gab_assert(gab_valkind(fiber) == kGAB_FIBER ||
                   gab_valkind(fiber) == kGAB_FIBERRUNNING,
               "Invalid kind");

    struct gab_ofiber *fb = GAB_VAL_TO_FIBER(fiber);

    struct gab_vm *vm = &fb->vm;

    gab_assert(vm->sp >= vm->sb, "By design, the stack pointer should always "
                                 "be above the stack base.");
    uint64_t stack_size = vm->sp - vm->sb;

    gab_assert(stack_size < cGAB_STACK_MAX,
               "The stack size is requred to be less than %lu", cGAB_STACK_MAX);

    gab_assert(stack_size + wk->lock_keep.len + 2 < GAB_GC_MOD_BUFF_MAX,
               "The total amount of queued modifications cannot exceed %lu",
               GAB_GC_MOD_BUFF_MAX);

    __gab_gcbufpush(gab, kGAB_BUF_STK, gab.wkid, e, gab_valtoo(fiber));

    for (uint64_t i = 0; i < stack_size; i++) {
      if (gab_valiso(vm->sb[i])) {
        struct gab_obj *o = gab_valtoo(vm->sb[i]);
#if cGAB_LOG_GC
        fprintf(stderr, "SAVESTK\t%i\t%p\t%d\n", __gab_gcepoch(gab), (void *)o,
                o->kind);
#endif
        __gab_gcbufpush(gab, kGAB_BUF_STK, gab.wkid, e, o);
      }
    }
  }

fin:
  __gab_gcepochinc(gab);
#if cGAB_LOG_GC
  fprintf(stderr, "(%i) PEPOCH!\t%i\n", gab.wkid, __gab_gcepoch(gab));
#endif
}

GAB_INTERNAL void __gab_gcassert_workers_have_epoch(struct gab_triple gab,
                                                    int32_t e) {
  for (uint64_t i = 1; i < gab.eg->len; i++) {
    int32_t this_e = __gab_gcepoch((struct gab_triple){gab.eg, .wkid = i});
    gab_assert(this_e == e, "Expected worker %i to have epoch %i. Saw: %i.\n",
               i, e, this_e);
  }
}

#if cGAB_LOG_GC
GAB_API void __gab_gcepochnext(struct gab_triple gab, const char *func,
                               int line) {

  fprintf(stderr, "EPOCH\t%i\t%i\t%s:%i\n", __gab_gcepoch(gab), gab.wkid, func,
          line);
#else
GAB_API void gab_gcepochnext(struct gab_triple gab) {
#endif
  if (gab.wkid > 0)
    __gab_gcdoepoch(gab, __gab_gcepoch(gab));
}

GAB_API void gab_gcdocollect(struct gab_triple gab) {
  gab_assert(gab.wkid == 0, "Shall only be run on gc thread");

  int32_t epoch = __gab_gcepoch(gab);
  int32_t last = __gab_gcepochlast(gab);

  gab_assert(epoch != last, "current and previous epoch must differ");

  __gab_gcdoepoch(gab, epoch);

  /**
   * Get this once. As collection is asynchronous,
   * the engine messages records is liable to change
   * as we're collecting. Just save the snapshot
   * of it now.
   */
  gab.eg->gc.msg[epoch] = atomic_load(&gab.eg->messages);

  gab_value messages = gab.eg->gc.msg[epoch];

  gab_value last_messages = gab.eg->gc.msg[last];

#if cGAB_LOG_GC
  fprintf(stderr, "CEPOCH %i (last: %i, raw: %i)\n", epoch, last,
          gab.eg->jobs[gab.wkid].epoch);
#endif

  int32_t expected_e = (gab.eg->jobs[gab.wkid].epoch) % 3;
  __gab_gcassert_workers_have_epoch(gab, expected_e);

  if (gab_valiso(messages))
    __gab_gcobjincref(gab, gab_valtoo(messages));

  __gab_gcdoincrements(gab, epoch);

  if (gab_valiso(last_messages))
    __gab_gcobjdecref(gab, gab_valtoo(last_messages));

  __gab_gcdodecrements(gab, last);

  __gab_gcdeadreap(gab);

#if cGAB_LOG_GC
  fprintf(stderr, "CEPOCH! %i\n", epoch);
#endif

  expected_e = (gab.eg->jobs[gab.wkid].epoch) % 3;
  __gab_gcassert_workers_have_epoch(gab, expected_e);
}

/* ----------------------------------------
 *
 *    GAB VIRTUAL MACHINE
 *
 *  This section contains the code for executing gab bytecode.
 * ----------------------------------------
 */

/**
 * @file
 * @brief This file contains the interpreter for Gab's bytecode.
 *
 * This interpreter is stack-based, which is quite conventional. There are
 * however some unconventional pieces to it.
 *
 * --< INTERPRETER DISPATCH >--
 *
 * Most implementations of a bytecode interpreter
 * include a switch statement in a big loop, or sometimes a more optimized
 * goto-loop. In Gab, we use a tail-calling interpreter. Each opcode is
 * implemented as a function adhering to the same interface (defined here with
 * macros, @see handler and @see OP_HANDLER_ARGS).
 *
 * When these opcodes finish their work, they tail-call into the next opcode
 * (@see DISPATCH). We annotate each of these return statements with
 * [[clang::musttail]], to ensure that the compiler can and does emit a tail
 * call at each of these call sites.
 *
 * - PROS -
 * Each instruction is its own function. This is easier to reason about when
 * implementing op codes, as there is no mutable state global to the
 * interpreting function. On top of that, it is more likely that the compiler
 * keeps crucial VM variables like the stack pointer and instruction pointer in
 * REGISTERS, because they are confined to registers by calling convention. (It
 * is for this reason that the stack pointer, constant pointer, and instruction
 * pointer are unboxed and passed as arguments, as opposed to just updating the
 * values in the gab_vm struct).
 *
 * Since each of our op-codes are individual functions, debugging tools like
 * callgrind can be used to create web-like visualizations, which show what
 * opcodes often follow others, and how much time is spent in each.
 *
 * - CONS -
 * Implementation requires more understanding of how c function calls work - and
 * there are limitations on these functions that are unclear. (Such as, no
 * dynamic stack allocations, like using int a[n], where n is not known at
 * compile time.)
 *
 * Tail-calling also makes for somewhat confusing stack traces, and can confuse
 * some debugging / performance tools. (Most do a good enough job though).
 *
 * --< STACK BASED TUPLES >--
 *
 * Gab as a language makes heavy use of ~tuples~. (returning multiple values
 * from a function, etc). To avoid allocating these as slices in memory, Gab
 * stores these tuples on its internal VM stack. The top of the stack (pointed
 * to by SP()), stores the *number of values* in the tuple below it. When a
 * value is pushed, that number is incremented. This is how function calls and
 * returns know how many values are present. (and how something like (1, 2,
 * func.send, 5, 6) => (1, 2, 3, 4, 5, 6) is able to work.
 *
 * There is plenty of runtime overhead in tracking this. But it is made up for
 * by the amount of allocation that is *Saved* through using this system
 * instead.
 *
 * --< INVARIANTS >--
 * There are some invariants which must hold true in these opcodes. It is
 * impossible to encode them into the c-typesystem, so I try to write them down
 * here. There may be more in my head which aren't written down.
 *
 *  1. Before calling out to a gab_* api function, STORE() must be called. This
 * stores the cached variables for the stack pointer and frame pointer into the
 * VM struct. Without this call, the gab_* api function will see an out-of-date
 * version of the fiber's stack.
 *
 *  2. Opcodes which may yield (by calling VM_YIELD()), much first make a call
 * to CHECK_SIGNAL(). This is to guarantee that a signal is received and
 * processed if the interpreter resumes at that specific opcode.
 *
 */

/*
 * move-macros for shifting slices of gab values up and down the vm stack.
 * Thereis a separate macro for an ascending-move (up the stack)
 * and a descending-move (down the stack). This is because the moves
 * must account for overlapping regions, so the dst doesn't overwrite the src.
 */
#define gmoved(dst, src, n)                                                    \
  ({                                                                           \
    gab_value *D = dst;                                                        \
    gab_value *S = src;                                                        \
    uint64_t N = n;                                                            \
    if (N && D != S) {                                                         \
      gab_assert(D < S, "Tried to write ascending move. %lu", D - S);          \
      while (N--)                                                              \
        *D++ = *S++;                                                           \
    }                                                                          \
  })
// #define gmoved(dst, src, n) memmove(dst, src, n * sizeof(gab_value))

#define gmovea(dst, src, n)                                                    \
  ({                                                                           \
    gab_value *D = dst;                                                        \
    gab_value *S = src;                                                        \
    uint64_t N = n;                                                            \
    if (N && D != S) {                                                         \
      D += N - 1;                                                              \
      S += N - 1;                                                              \
      gab_assert(S < D, "Tried to write descending move. %lu", S - D);         \
      while (N--)                                                              \
        *D-- = *S--;                                                           \
    }                                                                          \
  })
// #define gmovea(dst, src, n) memmove(dst, src, n * sizeof(gab_value))

#define OP_HANDLER_ARGS                                                        \
  struct gab_triple *__gab, struct gab_vm *__vm, uint8_t *__ip,                \
      gab_value *__kb, gab_value *__fb, gab_value *__sp

#define CASE_CODE(name)                                                        \
  cGAB_VM_OPCODE_ATTRIBUTES union gab_value_pair OP_##name##_HANDLER(          \
      OP_HANDLER_ARGS)

#define DISPATCH_ARGS() __gab, __vm, __ip, __kb, __fb, __sp

cGAB_VM_OPCODE_ATTRIBUTES typedef union gab_value_pair (*handler)(
    OP_HANDLER_ARGS);

// Forward declare all our opcode handlers
#define OP_CODE(name)                                                          \
  cGAB_VM_OPCODE_ATTRIBUTES union gab_value_pair OP_##name##_HANDLER(          \
      OP_HANDLER_ARGS);
#include "bytecode.h"
#undef OP_CODE

// Plop them all in an array
static handler handlers[] = {
#define OP_CODE(name) OP_##name##_HANDLER,
#include "bytecode.h"
#undef OP_CODE
};

#define GAB() (*__gab)
#define EG() (GAB().eg)
#define FIBER() (GAB_VAL_TO_FIBER(gab_thisfiber(GAB())))
#define REENTRANT() (FIBER()->reentrant)
#define RESET_REENTRANT() (FIBER()->reentrant = 0)
#define RESET_BUMP() (FIBER()->allocator.len = 0)
#define GC() (GAB().eg->gc)
#define VM() (__vm)
#define SET_BLOCK(b) ({ FB()[-(1 + FRAME_BK)] = (uintptr_t)(b); });
#define BLOCK() ((struct gab_oblock *)(uintptr_t)FB()[-(1 + FRAME_BK)])
#define BLOCK_PROTO()                                                          \
  ({                                                                           \
    gab_assert(BLOCK(), "Null block while accessing block prototype");         \
    GAB_VAL_TO_PROTOTYPE(BLOCK()->p);                                          \
  })
#define IP() (__ip)
#define SP() (__sp)
#define SB() (VM()->sb)
#define FB() (__fb)
#define HV() (*SP())
#define BELOW_HV() (PEEK_N(HV() + 1))
#define UPV() (BLOCK()->upvalues)
#define KB() (__kb)
#define LOCAL(i) (FB()[i])
#define STORE_LOCAL(i, v) (LOCAL(i) = v)
#define UPVALUE(i) (BLOCK()->upvalues[i])

#if cGAB_LOG_VM
#define LOG(gab, op)                                                           \
  fprintf(stderr, "%p OP_%s [%lu] (%i)\n", IP() - 1, gab_opcode_names[op],     \
          HV(), GAB().wkid);                                                   \
  gab_fprintf(stderr, "$\n", gab_thisfiber(gab));
#else
#define LOG(gab, op)
#endif

#define CHECK_SIGNAL()                                                         \
  if (gab_sigwaiting(GAB()))                                                   \
    switch (gab_yield(GAB())) {                                                \
    case sGAB_COLL:                                                            \
      STORE_SP();                                                              \
      gab_gcepochnext(GAB());                                                  \
      gab_sigpropagate(GAB());                                                 \
      break;                                                                   \
    case sGAB_TERM:                                                            \
      STORE_SP();                                                              \
      VM_TERM();                                                               \
    default:                                                                   \
      break;                                                                   \
    }

#define DISPATCH(op)                                                           \
  ({                                                                           \
    uint8_t o = (op);                                                          \
                                                                               \
    LOG(GAB(), o);                                                             \
                                                                               \
    gab_assert(SP() < VM()->sb + cGAB_STACK_MAX, "Shall have stackspace");     \
                                                                               \
    [[clang::musttail]] return handlers[o](DISPATCH_ARGS());                   \
  })

#define NEXT_CHECKED()                                                         \
  ({                                                                           \
    CHECK_SIGNAL();                                                            \
    NEXT();                                                                    \
  })

#define NEXT() ({ DISPATCH(*(IP()++)); })
// #define NEXT() NEXT_CHECKED()

#define VM_PANIC(status)                                                       \
  ({                                                                           \
    STORE();                                                                   \
    SP()[1] = status;                                                          \
    [[clang::musttail]] return __gab_vmeerror(DISPATCH_ARGS());                     \
  })

#define VM_PANIC3(status, a, b, c)                                             \
  ({                                                                           \
    STORE();                                                                   \
    SP()[1] = status;                                                          \
    SP()[2] = a;                                                               \
    SP()[3] = b;                                                               \
    SP()[4] = c;                                                               \
    [[clang::musttail]] return __gab_vmeerror(DISPATCH_ARGS());                     \
  })

#define VM_PANIC5(status, a, b, c, d, e)                                       \
  ({                                                                           \
    STORE();                                                                   \
    SP()[1] = status;                                                          \
    SP()[2] = a;                                                               \
    SP()[3] = b;                                                               \
    SP()[4] = c;                                                               \
    SP()[5] = d;                                                               \
    SP()[6] = e;                                                               \
    [[clang::musttail]] return __gab_vmeerror(DISPATCH_ARGS());                     \
  })

#define VM_GIVEN(err)                                                          \
  ({                                                                           \
    STORE();                                                                   \
    return __gab_vmgivenerror(GAB(), err);                                     \
  })

#define GET_STACKSPACE(sp, sb) ((sp - sb) + 3)

#define HAS_STACKSPACE(sp, sb, space)                                          \
  (GET_STACKSPACE(sp, sb) + space < cGAB_STACK_MAX)

#define SET_HV(n) ({ *SP() = n; })

#define PUSH_FRAME(b, have)                                                    \
  ({                                                                           \
    gab_assert(have < UINT32_MAX, "Have must fit within 32 bits");             \
                                                                               \
    int64_t delta = (SP() - have) - FB();                                      \
                                                                               \
    gab_assert((SP() - have) > FB(), "Previous frame shall be below new");     \
    gab_assert(delta > 0, "Previous frame must be different from below");      \
    gab_assert(delta < UINT32_MAX, "Delta must fit within 32 bits");           \
    gab_assert(SP()[-(int64_t)(have + 1 + FRAME_IP)] == FRAME_IP,              \
               "Frame setup correctly");                                       \
    gab_assert(SP()[-(int64_t)(have + 1 + FRAME_BK)] == FRAME_BK,              \
               "Frame setup correctly");                                       \
                                                                               \
    SP()[-(int64_t)(have + 1)] |= ((uint64_t)delta << 32);                     \
    SP()[-(int64_t)(have + 1 + FRAME_IP)] = (uintptr_t)IP();                   \
    SP()[-(int64_t)(have + 1 + FRAME_BK)] = (uintptr_t)b;                      \
  })

#define PUSH_VM_PANIC_FRAME(have) ({})

#define STORE_MICRO_OP_VM_PANIC_FRAME(have)                                    \
  ({                                                                           \
    STORE();                                                                   \
    PUSH_VM_PANIC_FRAME(have);                                                 \
  })

#define RETURN_FB_DELTA() (FB()[-(1)] >> 32)
#define RETURN_FB() ((FB() - RETURN_FB_DELTA()))

#define RETURN_IP() ((uint8_t *)(void *)FB()[-(1 + FRAME_IP)])
#define RETURN_BK() ((struct gab_oblock *)(void *)FB()[-(1 + FRAME_BK)])
#define RETURN_HAVE() (FB()[-(1)] & 0xffffffff)

#define LOAD_FRAME()                                                           \
  ({                                                                           \
    IP() = RETURN_IP();                                                        \
    FB() = RETURN_FB();                                                        \
    KB() = proto_ks(GAB(), BLOCK_PROTO());                                     \
    gab_assert(GAB_VAL_TO_FIBER(gab_thisfiber(GAB()))->vm.sb[2] == 0,          \
               "Frame setup correctly");                                       \
  })

#if cGAB_LOG_VM
#define PUSH(value)                                                            \
  ({                                                                           \
    if (SP() > (FB() + BLOCK_PROTO()->nslots + 1)) {                           \
      fprintf(gab.eg->stderr,                                                  \
              "Stack exceeded frame "                                          \
              "(%d). %lu passed\n",                                            \
              BLOCK_PROTO()->nslots, SP() - FB() - BLOCK_PROTO()->nslots);     \
      gab_fvminspect(stdout, VM(), 0);                                         \
      exit(1);                                                                 \
    }                                                                          \
    *SP()++ = value;                                                           \
  })

#else
#define PUSH(value) (*SP()++ = value)
#endif
#define POP() (*(--SP()))
#define DROP() (SP()--)
#define POP_N(n) (SP() -= (n))
#define DROP_N(n) (SP() -= (n))
#define PEEK() (*(SP() - 1))
#define PEEK2() (*(SP() - 2))
#define PEEK3() (*(SP() - 3))
#define PEEK_N(n) (*(SP() - (n)))

#define WRITE_BYTE(dist, n) ({ *(IP() - dist) = (n); })

#define WRITE_INLINEBYTE(n) (*IP()++ = (n))

#define SKIP_BYTE (IP()++)
#define SKIP_SHORT (IP() += 2)

#define READ_BYTE (*IP()++)
#define READ_SHORT (IP() += 2, (((uint16_t)IP()[-2] << 8) | IP()[-1]))

#define READ_CONSTANT (KB()[READ_SHORT])

// Turn off the highest bit, as this is used to store tail-calling information.
#define READ_SENDCONSTANTS                                                     \
  ({                                                                           \
    uint16_t shrt = READ_SHORT & (~(fHAVE_TAIL << 8));                         \
    KB() + shrt;                                                               \
  })

#define READ_SENDCONSTANTS_ANDTAIL(t)                                          \
  ({                                                                           \
    uint16_t shrt = READ_SHORT;                                                \
    t = ((shrt & (fHAVE_TAIL << 8)) != 0);                                     \
    KB() + (shrt & ~(fHAVE_TAIL << 8));                                        \
  })

// The *below_have* is 64 bits of space. It already exists on stack when
// The send is *sent*. Is there a way to convert this value into
// a stack frame? Can we jam everything we need into 64 bits?
//
// Store:
// - Return frame base
// - Return ip
// - Return block
// - Return have
//
// - Return frame base can be 8 bits of delta, from new fb to old.
// - Return ip is a pointer, which is 48 bits of data.
// - Return block is also a pointer, which is 48 bits of data.
// - Return have should really be 32, but we can make it 8
//
// - A send is always followed by a trim or a pack, so storing anything
// further
// - up on the stack doesn't really work either.
// - Maybe I can store the block @ the IP somehow?
//
// - If I am returning from a frame, I am returning to an IP which should have
// - send with a block in ks[GAB_SEND_KSPEC].
// - Problem - without knowing what the block is, we can't actually know
// *where*
// - the ks are. So its a chicken and egg situation.
//
// - Also, that returning send block in ks[GAB_SEND_KSPEC] is also the frame
// that
// - *is returning*, not the frame *to return to*.
//
// It also isn't known at compile time how big a stack frame can get, so
// putting it underneath is really the only option.
//
// A FB() Delta is stored in the same machine word as the below have. This
// saves 8 bytes of space on the stack.
//
// The upper 4 bytes are the frame delta, and the lower 4 bytes are the have.

#define FRAME_SIZE 3
#define FRAME_IP 1
#define FRAME_BK 2

GAB_INTERNAL gab_value *__gab_vmframeparent(gab_value *f) {
  int64_t delta = f[-(1)] >> 32;
  return delta ? f - delta : nullptr;
}

GAB_INTERNAL struct gab_oblock *__gab_vmframeblk(gab_value *f) {
  return (void *)f[-(1 + FRAME_BK)];
}

GAB_INTERNAL uint8_t *__gab_vmframeip(gab_value *f) {
  return (void *)f[-(1 + FRAME_IP)];
}

GAB_INTERNAL uint64_t __gab_tokenfromip(struct gab_triple gab,
                                        struct gab_oblock *b, uint8_t *ip) {
  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

  gab_assert(ip >= proto_srcbegin(gab, p), "ip shall be within src range");
  uint64_t offset = ip - proto_srcbegin(gab, p);

  if (offset)
    offset--;

  uint64_t token = v_uint64_t_val_at(&p->src->bytecode_toks, offset);

  return token;
}

GAB_INTERNAL struct gab_err_argt
__gab_vmbuilderr(struct gab_triple gab, struct gab_oblock *b, uint8_t *ip,
                 enum gab_status s, const char *fmt) {
  if (b) {
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

    uint64_t tok = __gab_tokenfromip(gab, b, ip);

    return (struct gab_err_argt){
        .tok = tok,
        .src = p->src,
        .note_fmt = fmt,
        .status = s,
        .wkid = gab.wkid,
    };
  }

  return (struct gab_err_argt){
      .note_fmt = fmt,
      .status = s,
      .wkid = gab.wkid,
  };
}

GAB_INTERNAL union gab_value_pair __gab_vmyield(struct gab_triple gab,
                                                uintptr_t value) {
  gab_value f = gab_thisfiber(gab);
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);
  gab_precondition(value != 0, "Shall not yield 0");
  gab_precondition(fiber->header.kind = kGAB_FIBERRUNNING,
                   "Shall only yield running fiber");

  fiber->header.kind = kGAB_FIBER;
  fiber->reentrant = value;

  return (union gab_value_pair){{gab_ctimeout, f}};
}

GAB_INTERNAL gab_value __gab_sprintstk(struct gab_triple gab, struct gab_vm *vm,
                                       gab_value *f, uint8_t *ip, int s,
                                       const char *fmt, va_list va) {
  // TODO @cgab: Place reasonable limit on number of frames to sprint.
  // Also, skip middle ones sometimes.
  int nframes = 0;
  gab_value vframes[1024] = {0};

  struct gab_err_argt frame =
      __gab_vmbuilderr(gab, __gab_vmframeblk(f), ip, s, fmt);

  vframes[nframes] = gab_vspanicf(gab, va, frame);
  if (vframes[nframes] != gab_cinvalid)
    nframes++;

  ip = __gab_vmframeip(f);
  f = __gab_vmframeparent(f);

  while (f && __gab_vmframeparent(f) > vm->sb) {
    frame = __gab_vmbuilderr(gab, __gab_vmframeblk(f), ip, GAB_NONE, "");
    vframes[nframes] = gab_vspanicf(gab, va, frame);
    if (vframes[nframes] != gab_cinvalid)
      nframes++;

    ip = __gab_vmframeip(f);
    f = __gab_vmframeparent(f);
  }

  if (nframes)
    return gab_list(gab, 1, nframes, vframes);
  else
    return gab_cinvalid;
}

GAB_API gab_value gab_fibstacktrace(struct gab_triple gab, gab_value fiber) {
  struct gab_vm *vm = gab_fibvm(fiber);

  gab_value *f = vm->fp;
  uint8_t *ip = vm->ip;

  va_list empty;
  return __gab_sprintstk(gab, vm, f, ip, GAB_TERM, nullptr, empty);
}

GAB_INTERNAL union gab_value_pair __gab_vvmterm(struct gab_triple gab,
                                                const char *fmt, va_list va) {
  gab_value fiber = gab_thisfiber(gab);

  /*
   * It is possible that a fiber is *done* here, if gab_panic was called in a
   * native fn.
   */
  if (gab_valkind(fiber) == kGAB_FIBERDONE)
    return GAB_VAL_TO_FIBER(fiber)->res_values;

  gab_assert(gab_valkind(fiber) == kGAB_FIBERRUNNING,
             "(%i) Terminating fiber %p must be running, not: %d. Terminating.",
             gab.wkid, GAB_VAL_TO_FIBER(fiber), gab_valkind(fiber));

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_env == gab_cinvalid,
             "(%i) Terminating fiber %p res_env shall be uninitialized.",
             gab.wkid, GAB_VAL_TO_FIBER(fiber));

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_values.status == 0,
             "(%i) Terminating fiber %p res shall be uninitialized.", gab.wkid,
             GAB_VAL_TO_FIBER(fiber));

  struct gab_vm *vm = gab_thisvm(gab);

  // gab_value *f = vm->fp;
  // uint8_t *ip = vm->ip;

  // gab_value err = sprint_stacktrace(gab, vm, f, ip, GAB_TERM, fmt, va);
  //
  // gab_iref(gab, err);
  // gab_egkeep(gab.eg, err);

  union gab_value_pair res = {{gab_cinvalid, gab_cinvalid}};

  struct gab_oblock *blk = __gab_vmframeblk(vm->fp);
  gab_value env;

  if (blk) {
    gab_value p = blk->p;
    gab_value shape = gab_prtshp(p);
    env = gab_recordfrom(gab, shape, 1, gab_shplen(shape), vm->fp);
  } else {
    env = gab_recordof(gab);
  }

  gab_egkeep(gab.eg, gab_iref(gab, env));

  GAB_VAL_TO_FIBER(fiber)->res_values = res;
  GAB_VAL_TO_FIBER(fiber)->res_env = env;
  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERDONE;
#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) VMTERM finished fiber $.\n", gab_number(gab.wkid),
              __gab_obj(fiber));
#endif

  return res;
}

GAB_INTERNAL union gab_value_pair
__gab_vmgivenerror(struct gab_triple gab, union gab_value_pair given) {
  gab_value fiber = gab_thisfiber(gab);

  /*
   * It is possible that a fiber is *done* here, if gab_panic was called in a
   * native fn.
   */
  if (gab_valkind(fiber) == kGAB_FIBERDONE)
    return GAB_VAL_TO_FIBER(fiber)->res_values;

  gab_assert(gab_valkind(fiber) == kGAB_FIBERRUNNING,
             "Terminating fiber %p must be running, not: %d. Given err.",
             GAB_VAL_TO_FIBER(fiber), gab_valkind(fiber));

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_env == gab_cinvalid,
             "Terminating fiber res_env shall be uninitialized.");

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_values.status == 0,
             "Terminating fiber res shall be uninitialized.");

  struct gab_vm *vm = gab_thisvm(gab);

  GAB_VAL_TO_FIBER(fiber)->res_values = given;

  if (__gab_vmframeblk(vm->fp)) {
    gab_value p = __gab_vmframeblk(vm->fp)->p;
    gab_value shape = gab_prtshp(p);

    gab_value env = gab_recordfrom(gab, shape, 1, gab_shplen(shape), vm->fp);
    gab_egkeep(gab.eg, gab_iref(gab, env));

    GAB_VAL_TO_FIBER(fiber)->res_env = env;
  }

  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERDONE;
#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) VVMGIVEN ERR finished fiber $.\n",
              gab_number(gab.wkid), __gab_obj(fiber));
#endif

  return given;
}

GAB_INTERNAL union gab_value_pair __gab_vvmerror(struct gab_triple gab,
                                                 enum gab_status s,
                                                 const char *fmt, va_list va) {
  gab_value fiber = gab_thisfiber(gab);

  gab_assert(
      gab_valkind(fiber) == kGAB_FIBERRUNNING,
      "(%i) Terminating fiber must be running, not: %d. Error status %s.",
      gab.wkid, gab_valkind(fiber), gab_status_names[s]);

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_env == gab_cinvalid,
             "Terminating fiber res_env shall be uninitialized.");

  gab_assert(GAB_VAL_TO_FIBER(fiber)->res_values.status == 0,
             "Terminating fiber res shall be uninitialized.");

  struct gab_vm *vm = gab_thisvm(gab);

  gab_value *f = vm->fp;
  uint8_t *ip = vm->ip;

  gab_value err = __gab_sprintstk(gab, vm, f, ip, s, fmt, va);

  if (err == gab_cinvalid)
    return __gab_vvmterm(gab, "While executing $\n", va);

  gab_iref(gab, err);
  gab_egkeep(gab.eg, err);

  gab_value vals[] = {gab_err, err};
  a_gab_value *results =
      a_gab_value_create(vals, sizeof(vals) / sizeof(gab_value));

  gab_niref(gab, 1, results->len, results->data);
  gab_negkeep(gab.eg, results->len, results->data);

  union gab_value_pair res = {.status = gab_cvalid, .aresult = results};

  gab_assert(GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERRUNNING,
             "Shall only error running fiber");

  GAB_VAL_TO_FIBER(fiber)->res_values = res;
  if (__gab_vmframeblk(vm->fp)) {
    gab_value p = __gab_vmframeblk(vm->fp)->p;

    gab_value shape = gab_prtshp(p);

    gab_value env = gab_recordfrom(gab, shape, 1, gab_shplen(shape), vm->fp);

    gab_egkeep(gab.eg, gab_iref(gab, env));
    gab_assert(GAB_VAL_TO_FIBER(fiber)->res_env == gab_cinvalid,
               "res_env shall not be populated");
    GAB_VAL_TO_FIBER(fiber)->res_env = env;
  }
  GAB_VAL_TO_FIBER(fiber)->header.kind = kGAB_FIBERDONE;

#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) VVMERR finished fiber $.\n", gab_number(gab.wkid),
              __gab_obj(fiber));
#endif

  return res;
}

GAB_INTERNAL union gab_value_pair __gab_vmterm(struct gab_triple gab,
                                               const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  union gab_value_pair res = __gab_vvmterm(gab, fmt, va);

  va_end(va);

  return res;
}

GAB_INTERNAL union gab_value_pair
__gab_vmerror(struct gab_triple gab, enum gab_status s, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  union gab_value_pair res = __gab_vvmerror(gab, s, fmt, va);

  va_end(va);

  return res;
}

#define FMT_TYPEMISMATCH                                                       \
  "@ @ found a value with an unexpected type.\n\n"                             \
  "$\n\n"                                                                      \
  "which has type\n\n"                                                         \
  "$\n\n"                                                                      \
  "but expected type\n\n"                                                      \
  "$\n"

#define FMT_MISSINGIMPL                                                        \
  "Sent message @ does not specialize for this receiver.\n\n"                  \
  "$\n\n"                                                                      \
  "of type\n\n"                                                                \
  "$\n"

GAB_API union gab_value_pair gab_vpanicf(struct gab_triple gab, const char *fmt,
                                         va_list va) {
  if (gab_thisfiber(gab) == gab_cinvalid) {
    gab_value err = gab_vspanicf(gab, va,
                                 (struct gab_err_argt){
                                     .status = GAB_PANIC,
                                     .note_fmt = fmt,
                                     .wkid = gab.wkid,
                                 });

    if (err != gab_cinvalid) {
      gab_iref(gab, err);
      gab_egkeep(gab.eg, err);
    }

    gab_value res[] = {gab_err, err};
    a_gab_value *results =
        a_gab_value_create(res, sizeof(res) / sizeof(gab_value));

    return (union gab_value_pair){
        .status = gab_cvalid,
        .aresult = results,
    };
  };

  union gab_value_pair res = __gab_vvmerror(gab, GAB_PANIC, fmt, va);

  return res;
}

GAB_API union gab_value_pair gab_panicf(struct gab_triple gab, const char *fmt,
                                        ...) {
  va_list va;
  va_start(va, fmt);

  union gab_value_pair res = gab_vpanicf(gab, fmt, va);

  va_end(va);

  return res;
}

// This isn't intuitive when compared to the MISS_CACHED_SEND
// code, but operator precedence is tricky like that.
// Because the other code is ip -= 3 -1 => ip -= (3 - 1)
// whereas here ip = other_ip - 3 - 1 => ip = (other_ip - 3) - 1
//
// So we add one instead and all lines up.
GAB_API gab_value gab_vmmsg(struct gab_vm *vm) {
  uint8_t *__ip = vm->ip - GAB_SEND_CACHE_SIZE + 1;
  gab_value *__kb = vm->kb;
  gab_value *ks = READ_SENDCONSTANTS;
  return ks[GAB_SEND_KMESSAGE];
}

GAB_API gab_value gab_vmspec(struct gab_vm *vm) {
  uint8_t *__ip = vm->ip - GAB_SEND_CACHE_SIZE + 1;
  gab_value *__kb = vm->kb;
  gab_value *ks = READ_SENDCONSTANTS;
  return ks[GAB_SEND_KSPEC];
}

GAB_API union gab_value_pair
gab_ptypemismatch(struct gab_triple gab, gab_value found, gab_value texpected) {
  gab_value msg = gab_vmmsg(gab_thisvm(gab));
  gab_value spec = gab_vmspec(gab_thisvm(gab));
  gab_value tfound = gab_valtype(gab, found);
  return __gab_vmerror(gab, GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, msg, spec,
                       found, tfound, texpected);
}

// TODO @cgab @bug: Implement me
GAB_API gab_value gab_vmframe(struct gab_triple gab, uint64_t depth) {
  // uint64_t frame_count = gab_vm(gab)->fp - gab_vm(gab)->sb;
  //
  // if (depth >= frame_count)
  return gab_cinvalid;

  // struct gab_vm_frame *f = gab_vm(gab)->fp - depth;
  //
  // const char *keys[] = {
  //     "line",
  // };
  //
  // gab_value line = gab_nil;
  //
  // if (f->b) {
  //   struct gab_src *src = GAB_VAL_TO_PROTOTYPE(f->b->p)->src;
  //   uint64_t tok = compute_token_from_ip(f);
  //   line = gab_number(v_uint64_t_val_at(&src->token_lines, tok));
  // }
  //
  // gab_value values[] = {
  //     line,
  // };
  //
  // uint64_t len = sizeof(keys) / sizeof(keys[0]);
  //
  // return gab_srecord(gab, len, keys, values);
}

GAB_API void gab_fvminspect(FILE *stream, struct gab_vm *vm, int depth) {
  // uint64_t frame_count = vm->fp - vm->fb;
  //
  // if (value > frame_count)
  // return;

  // struct gab_vm_frame *f = vm->fp - value;
  //
  // struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(f->b->p);
  //
  // fprintf(stream,
  //         GAB_GREEN " %03lu" GAB_RESET " closure:" GAB_CYAN "%-20s"
  //         GAB_RESET
  //                   " %d upvalues\n",
  //         frame_count - value, gab_strdata(&p->src->name), p->nupvalues);

  gab_value *f = vm->fp;
  gab_value *t = vm->sp - 1;

  while (depth > 0) {
    if (__gab_vmframeparent(f) > vm->sb) {
      t = f - 4;
      f = __gab_vmframeparent(f);
      depth--;
    } else {
      return;
    }
  }

  gab_fvalinspect(stream, __gab_obj(__gab_vmframeblk(f)), 0);
  fprintf(stream, "\n");

  while (t >= f) {
    fprintf(stream, "%2s" GAB_YELLOW "%4" PRIu64 " " GAB_RESET,
            vm->sp == t ? "->" : "", (uint64_t)(t - vm->sb));
    gab_fvalinspect(stream, *t, 0);
    fprintf(stream, "\n");
    t--;
  }
}

GAB_API void gab_fvminspectall(FILE *stream, struct gab_vm *vm) {
  for (uint64_t i = 0; i < 64; i++) {
    gab_fvminspect(stream, vm, i);
  }
}

GAB_API gab_value gab_vmpop(struct gab_vm *vm) {
  if (__gab_unlikely(vm->sp == vm->sb))
    return gab_cundefined;

  uint64_t have = *vm->sp;
  gab_value popped = *(--vm->sp);
  *vm->sp = have - 1;
  return popped;
}

// GAB_API bool gab_vmgrow(struct gab_vm *vm) {
//   uint64_t diff = vm->sp - vm->sb;
//   uint64_t sz = diff * sizeof(gab_value);
//
//   uint64_t fpdiff = vm->fp - vm->sb;
//
//   if (vm->sb == vm->initial) {
//     vm->sb = malloc(sz * 2);
//
//     if (vm->sb)
//       memcpy(vm->sb, vm->initial, sz);
//
//   } else {
//     vm->sb = realloc(vm->sb, sz * 2);
//   }
//
//   if (!vm->sb)
//     return false;
//
//   vm->sp = vm->sb + diff;
//   vm->fp = vm->sb + fpdiff;
//   return true;
// }

GAB_API gab_value gab_vmpeek(struct gab_vm *vm, uint64_t dist) {
  if (__gab_unlikely(vm->sp - dist < vm->sb))
    return gab_cundefined;

  return vm->sp[-(int64_t)(dist + 1)];
}

GAB_API uint64_t gab_nvmpush(struct gab_vm *vm, uint64_t argc,
                             gab_value argv[argc]) {
  if (__gab_unlikely(argc == 0 || !HAS_STACKSPACE(vm->sp, vm->sb, argc))) {
    // TODO: @cgab @qol: Resize the stack here.
    // Panic here?
    // if (!gab_vmgrow(vm))
    return 0;
  }

  uint64_t have = *vm->sp;

  for (uint8_t n = 0; n < argc; n++) {
    *vm->sp++ = argv[n];
  }

  *vm->sp = have + argc;

  return argc;
}

cGAB_VM_OPCODE_ATTRIBUTES union gab_value_pair __gab_vmeerror(OP_HANDLER_ARGS) {
  enum gab_status status = SP()[1];
  switch (status) {
  case GAB_OVERFLOW:
    return __gab_vmerror(GAB(), status, "");
  case GAB_PANIC:
    return __gab_vmerror(GAB(), status, "");
  case GAB_SPECIALIZATION_MISSING:
    return __gab_vmerror(GAB(), status, FMT_MISSINGIMPL, SP()[2], SP()[3],
                         SP()[4]);
  case GAB_TYPE_MISMATCH:
    return __gab_vmerror(GAB(), status, FMT_TYPEMISMATCH, SP()[2], SP()[3],
                         SP()[4], SP()[5], SP()[6]);
  default:
    gab_unreachable("Impossible error status");
    return (union gab_value_pair){};
  }
}

cGAB_VM_OPCODE_ATTRIBUTES union gab_value_pair __gab_vmok(OP_HANDLER_ARGS) {
  uint64_t have = *VM()->sp;
  gab_value *from = VM()->sp - have;

  // TODO @bug: When gab_sending just a send constant call, we hit a seg
  // fault. This catches the issue - we overwrite the frame here without
  // updating fb I think. assert(FB() < SP()); Once the fiber has returned,
  // the local values have been overwritten with return values. This makes
  // extracting new locals impossible.

  a_gab_value *results = a_gab_value_empty(have + 1);
  results->data[0] = gab_ok;
  memcpy(results->data + 1, from, have * sizeof(gab_value));

  gab_niref(GAB(), 1, results->len, results->data);
  gab_negkeep(EG(), results->len, results->data);

  union gab_value_pair res = (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = results,
  };

  VM()->sp = VM()->sb;

  struct gab_ofiber *fiber = FIBER();

  // if (fiber->header.kind == kGAB_FIBERDONE) {
  //   gab_fprintf(stderr, "($) OK'd finished fiber $.\n",
  //   gab_number(GAB().wkid),
  //               __gab_obj(fiber));
  //   gab_fprintf(stderr, "STATUS: $\n", fiber->res_values.status);
  //   gab_fprintf(stderr, "VRESULT: $\n", fiber->res_values.vresult);
  // }
  gab_assert(fiber->header.kind == kGAB_FIBERRUNNING,
             "(%i) Terminating fiber %p must be running, not: %d. OK!",
             GAB().wkid, fiber, fiber->header.kind);

  gab_assert(fiber->res_env == gab_cinvalid,
             "(%i) Terminating fiber %p res_env shall be uninitialized.",
             GAB().wkid, fiber);

  gab_assert(fiber->res_values.status == 0,
             "(%i) Terminating fiber %p res shall be uninitialized.",
             GAB().wkid, fiber);

  fiber->res_values = res;

  // TODO @bug: Find some way to pull the env out of a fiber.
  // if (frame_block(VM()->fp)) {
  //   gab_value p = frame_block(VM()->fp)->p;
  //   gab_value shape = gab_prtshp(p);
  //
  //   gab_value env =
  //       gab_recordfrom(GAB(), shape, 1, gab_shplen(shape), VM()->fp,
  //       nullptr);
  //
  //   gab_egkeep(EG(), gab_iref(GAB(), env));
  //
  //   fiber->res_env = env;
  // }

  fiber->header.kind = kGAB_FIBERDONE;
#if cGAB_LOG_EG
  gab_fprintf(stderr, "($) VMOK finished fiber $.\n", gab_number(GAB().wkid),
              __gab_obj(fiber));
#endif

  return res;
}

GAB_INTERNAL union gab_value_pair __gab_vmexec(struct gab_triple gab,
                                               gab_value f) {
  gab_assert(gab_valkind(f) == kGAB_FIBER,
             "Only gab\\fiber shall be exec'd. Not a value of type: %d",
             gab_valkind(f));
  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  gab.flags |= fiber->flags;

  gab_assert(fiber->vm.sb[2] == 0, "Shall not have return frame");
  gab_assert(fiber->vm.kb, "Shall have constant table");
  gab_assert(fiber->vm.ip, "Shall have ip");

  uint8_t *ip = fiber->vm.ip;

  uint8_t op = *ip++;

  // We can't return *to* this frame because it has no block.
  // But we *should* return here so that the environment returned
  // to the fiber is as expected

  gab_assert(fiber->header.kind != kGAB_FIBERDONE,
             "Exec'd fiber shall not be done");
  fiber->header.kind = kGAB_FIBERRUNNING;

  return handlers[op](&gab, &fiber->vm, ip, fiber->vm.kb, fiber->vm.fp,
                      fiber->vm.sp);
}

GAB_INTERNAL bool __gab_vmtrysetuplocalmatch(struct gab_triple gab, gab_value m,
                                             gab_value *ks,
                                             struct gab_oprototype *p) {
  gab_value specs = gab_thisfibmsgrec(gab, m);

  if (specs == gab_cundefined)
    return false;

  if (gab_reclen(specs) > 4 || gab_reclen(specs) < 2)
    return false;

  uint64_t len = gab_reclen(specs);

  for (uint64_t i = 0; i < len; i++) {
    gab_value spec = gab_uvrecat(specs, i);

    if (gab_valkind(spec) != kGAB_BLOCK)
      return false;

    struct gab_oblock *b = GAB_VAL_TO_BLOCK(spec);
    struct gab_oprototype *spec_p = GAB_VAL_TO_PROTOTYPE(b->p);

    if (spec_p->src != p->src)
      return false;

    gab_value t = gab_ukrecat(specs, i);

    uint8_t idx = GAB_SEND_HASH(t) * GAB_SEND_CACHE_SIZE;

    // We have a collision - no point in messing about with this.
    if (ks[GAB_SEND_KSPEC + idx] != gab_cinvalid)
      return false;

    uint8_t *ip = proto_ip(gab, spec_p);

    ks[GAB_SEND_KTYPE + idx] = t;
    ks[GAB_SEND_KSPEC + idx] = (intptr_t)b;
    ks[GAB_SEND_KOFFSET + idx] = (intptr_t)ip;
  }

  ks[GAB_SEND_KSPECS] = atomic_load(&gab.eg->messages_epoch);
  return true;
}

/*
 * This file defines implementations for an extensive set of macros.
 *
 * These macros are used by `ops.h`, which definies the bytecode operations
 * in terms of these smaller, primitive bytecodes.
 */

/* IMPL in vm.c */
GAB_INTERNAL union gab_value_pair __gab_vmterm(struct gab_triple gab,
                                               const char *fmt, ...);

/* IMPL in vm.c */
GAB_INTERNAL union gab_value_pair __gab_vmyield(struct gab_triple gab,
                                                uintptr_t value);

extern void putl(uintptr_t arg);
extern void putp(uintptr_t arg);
extern void puti(int64_t arg);
extern void putf(double arg);
extern void putg(gab_value arg);
extern void putcs(char *arg);

#define PANIC_GUARD_STACKSPACE(space)                                          \
  if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), space)))                      \
    VM_PANIC(GAB_OVERFLOW);

#define PANIC_GUARD_STACKSPACE_SPLATDICT(r)                                    \
  ({                                                                           \
    uint64_t n = gab_shplen(r) * 2;                                            \
    if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), n)))                        \
      VM_PANIC(GAB_OVERFLOW);                                                  \
    n;                                                                         \
  })

#define PANIC_GUARD_STACKSPACE_SPLATLIST(r)                                    \
  ({                                                                           \
    uint64_t n = gab_shplen(r);                                                \
    if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), n)))                        \
      VM_PANIC(GAB_OVERFLOW);                                                  \
    n;                                                                         \
  })

#define PANIC_GUARD_STACKSPACE_SPLATSHAPE(r)                                   \
  if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), gab_shplen(r))))              \
    VM_PANIC(GAB_OVERFLOW);

#define PANIC_GUARD_SHAPE_LEN(shape, len)                                      \
  if (__gab_unlikely(gab_shplen(shape) != len))                                \
    VM_PANIC(GAB_PANIC);

#define MICRO_OP_CALL_BLOCK(blk, have)                                         \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
    KB() = proto_ks(GAB(), p);                                                 \
    FB() = SP() - have;                                                        \
    gab_assert(BLOCK()->header.kind == kGAB_BLOCK,                             \
               "Block shall be gab\\block");                                   \
    gab_assert(BLOCK_PROTO()->header.kind == kGAB_PROTOTYPE,                   \
               "Proto shall be gab\\proto");                                   \
  })

#define MICRO_OP_LOCALCALL_BLOCK(blk, have)                                    \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET];                                       \
    FB() = SP() - have;                                                        \
    gab_assert(BLOCK()->header.kind == kGAB_BLOCK,                             \
               "Block shall be gab\\block");                                   \
    gab_assert(BLOCK_PROTO()->header.kind == kGAB_PROTOTYPE,                   \
               "Proto shall be gab\\proto");                                   \
                                                                               \
    SET_HV(have);                                                              \
  })

#define MICRO_OP_TAILCALL_BLOCK(blk, have)                                     \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(p->nslots - have);                                  \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    gmoved(to, from, have);                                                    \
    SP() = to + have;                                                          \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
    KB() = proto_ks(GAB(), p);                                                 \
                                                                               \
    SET_BLOCK(blk);                                                            \
    SET_HV(have);                                                              \
  })

#define MICRO_OP_LOCALTAILCALL_BLOCK(blk, have)                                \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(p->nslots - have);                                  \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    gmoved(to, from, have);                                                    \
    SP() = to + have;                                                          \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET];                                       \
                                                                               \
    SET_BLOCK(blk);                                                            \
    SET_HV(have);                                                              \
  })

#define MICRO_OP_MATCHTAILCALL_BLOCK(idx, have)                                \
  ({                                                                           \
    struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC + idx]);         \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    gmoved(to, from, have);                                                    \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET + idx];                                 \
    SP() = to + have;                                                          \
    FB() = SP() - have;                                                        \
                                                                               \
    SET_BLOCK(b);                                                              \
    SET_HV(have);                                                              \
  })

#define MICRO_OP_MATCHCALL_BLOCK(idx, have)                                    \
  ({                                                                           \
    struct gab_oblock *blk = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC + idx]);       \
                                                                               \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET + idx];                                 \
    FB() = SP() - have;                                                        \
                                                                               \
    SET_HV(have);                                                              \
  })

/*
 * NOTE: crucially, this micro op gives the native function an option to handle
 * the signal *itself* before CHECK_SIGNAL() is encountered.
 *
 * This is because some re-entrant natives may need to see terminate/gc signals
 * in order to be correct.
 *
 * We give the native a chance to handle the signal,
 * and then CHECK_SIGNAL() ourselves before yielding.
 */
#define MICRO_OP_CALL_NATIVE(native, have, below_have, message)                \
  ({                                                                           \
    STORE();                                                                   \
                                                                               \
    gab_value *returnptr = RETURN_FB();                                        \
                                                                               \
    gab_value *to = SP() - (have + FRAME_SIZE);                                \
    gab_assert(to >= FB() - 3,                                                 \
               "Expected dest to be greater than frame base. dist: %li\n",     \
               to - FB());                                                     \
                                                                               \
    gab_value *before = SP();                                                  \
                                                                               \
    uint64_t pass = (have - !message);                                         \
                                                                               \
    union gab_value_pair res =                                                 \
        (*native->function)(GAB(), pass, SP() - pass, REENTRANT());            \
                                                                               \
    RESET_REENTRANT();                                                         \
                                                                               \
    SP() = VM()->sp;                                                           \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (__gab_unlikely(res.status == gab_ctimeout))                            \
      VM_YIELD(res.bresult);                                                   \
                                                                               \
    RESET_BUMP();                                                              \
                                                                               \
    if (__gab_unlikely(res.status == gab_cvalid))                              \
      return res;                                                              \
                                                                               \
    gab_assert(SP() >= before, "Fewer than zero values returned from native"); \
    uint64_t have = SP() - before;                                             \
                                                                               \
    if (!have)                                                                 \
      PUSH(MICRO_OP_NIL()), have++;                                            \
                                                                               \
    gmoved(to, before, have);                                                  \
    SP() = to + have;                                                          \
                                                                               \
    SET_HV(below_have + have);                                                 \
                                                                               \
    gab_assert(returnptr == RETURN_FB(),                                       \
               "Should not have overwritten return frame");                    \
  })

/*
 * These primitives need some sort of control-flow in order
 * to work cleanly with the JIT IR.
 */
#define MICRO_OP_TAKE(channel)                                                 \
  ({                                                                           \
    if (!REENTRANT()) {                                                        \
      SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                    \
      SEND_GUARD_ISCHN(c);                                                     \
    }                                                                          \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    /*                                                                         \
     * Adjust for the tuple-len value at *SP() on the stack.                   \
     * Store above it, and subract one from the stackspace to reserve it.      \
     */                                                                        \
    uint64_t stackspace = GET_STACKSPACE(SP(), SB()) - 1;                      \
                                                                               \
    gab_value v = gab_ntchntake(GAB(), c, stackspace, SP() + 1,                \
                                cGAB_VM_CHANNEL_TAKE_TRIES);                   \
                                                                               \
    RESET_REENTRANT();                                                         \
                                                                               \
    switch (v) {                                                               \
    case gab_ctimeout:                                                         \
      VM_YIELD(gab_ctimeout);                                                  \
    case gab_cinvalid:                                                         \
      VM_TERM();                                                               \
    case gab_cundefined:                                                       \
      DROP_N(have + FRAME_SIZE);                                               \
      PUSH(gab_none);                                                          \
                                                                               \
      SET_HV(below_have + 1);                                                  \
      NEXT();                                                                  \
    default:                                                                   \
      uint64_t len = gab_valtou(v);                                            \
                                                                               \
      DROP_N(have + FRAME_SIZE);                                               \
      PUSH(gab_ok);                                                            \
      /*                                                                       \
       * ntchntake returns the number of values *available*, but will only     \
       * write up to *stackspace*.                                             \
       *                                                                       \
       * If there were more available to take than we had room for on the      \
       * stack, return an overflow.                                            \
       * */                                                                    \
      if (__gab_unlikely(len > stackspace))                                    \
        VM_PANIC(GAB_OVERFLOW);                                                \
                                                                               \
      /*                                                                       \
       * We now know that we wrote *len* values to the buffer, because         \
       * it is guaranteed that len <= stackspace                               \
       * */                                                                    \
      gmoved(SP(), SP() + have + FRAME_SIZE, len);                             \
      SP() += len;                                                             \
                                                                               \
      SET_HV(below_have + len + 1);                                            \
      NEXT();                                                                  \
    }                                                                          \
  })

#define MICRO_OP_PUT(channel)                                                  \
  ({                                                                           \
    if (!REENTRANT()) {                                                        \
      SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                    \
      SEND_GUARD_ISCHN(c);                                                     \
    }                                                                          \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (REENTRANT() == c) {                                                    \
      /* If we're reentering, check that our channel                           \
       is still holding our data ptr.                                          \
       I *believe* this is sound based on the following principles:            \
        - Fibers only ever run on *one* thread, they never migrate.            \
        - Fibers don't share vm's - the address range of one stack             \
          can never overlap with another's. (If stacks become resizeable, this \
          changes)                                                             \
        - Fiber's stacks never resize                                          \
      */                                                                       \
      if (!gab_chnisclosed(c) && gab_chnmatches(c, SP() - (have - 1)))         \
        VM_YIELD(c);                                                           \
                                                                               \
      RESET_REENTRANT();                                                       \
                                                                               \
      /* If not, our put is complete and we can move on */                     \
      DROP_N(have + FRAME_SIZE);                                               \
                                                                               \
      PUSH(c);                                                                 \
                                                                               \
      SET_HV(below_have + 1);                                                  \
                                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    /* All values *but* the channel are put into the channel. */               \
    gab_value r = gab_untchnput(GAB(), c, have - 1, SP() - (have - 1),         \
                                cGAB_VM_CHANNEL_PUT_TRIES);                    \
                                                                               \
    switch (r) {                                                               \
    case gab_cinvalid:                                                         \
      VM_TERM();                                                               \
    case gab_ctimeout:                                                         \
      /* The put timed-out */                                                  \
      VM_YIELD(gab_ctimeout);                                                  \
    default:                                                                   \
      /* The put succeeded, we must yield until it completes.*/                \
      VM_YIELD(c);                                                             \
    }                                                                          \
  })

// TODO @cgab @bug: Fiber creation leak
// When a fiber is created but the put yields,
// we essentially have dangling ptr that may end up collected.
#define MICRO_OP_FIBER(block, have)                                            \
  ({                                                                           \
    STORE_SP();                                                                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    uint64_t argc = have;                                                      \
                                                                               \
    gab_value fb =                                                             \
        gab_fiber(GAB(), (struct gab_fiber_argt){                              \
                             .message = gab_message(GAB(), mGAB_CALL),         \
                             .receiver = block,                                \
                             .flags = GAB().flags,                             \
                             .argv = SP() - argc,                              \
                             .argc = argc,                                     \
                         });                                                   \
                                                                               \
    bool spawned = __gab_jbspawn(GAB(), fb);                                   \
                                                                               \
    if (!spawned)                                                              \
      __gab_egqfib(GAB(), fb);                                                 \
                                                                               \
    DROP_N(have + FRAME_SIZE);                                                 \
    PUSH(fb);                                                                  \
                                                                               \
    SET_HV(below_have + 1);                                                    \
                                                                               \
    NEXT();                                                                    \
  })

#define MICRO_OP_SEND(have)                                                    \
  ({                                                                           \
    /* Have can not be 0. We need a receiver. */                               \
    if (__gab_unlikely(!have)) {                                               \
      PUSH(MICRO_OP_NIL());                                                    \
      SET_HV(1);                                                               \
      have++;                                                                  \
    }                                                                          \
                                                                               \
    gab_value r = PEEK_N(have);                                                \
    gab_value m = ks[GAB_SEND_KMESSAGE];                                       \
                                                                               \
    if (BLOCK() && try_setup_localmatch(GAB(), m, ks, BLOCK_PROTO())) {        \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_MATCHSEND_BLOCK + adjust);            \
      IP() -= GAB_SEND_CACHE_SIZE;                                             \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    /* Do the expensive lookup */                                              \
    struct gab_impl_rest res = gab_impl(GAB(), m, r);                          \
                                                                               \
    if (__gab_unlikely(!res.status))                                           \
      VM_PANIC3(GAB_SPECIALIZATION_MISSING, m, r, gab_valtype(GAB(), r));      \
                                                                               \
    gab_value spec = res.status == kGAB_IMPL_PROPERTY                          \
                         ? gab_primitive(OP_SEND_PROPERTY)                     \
                         : res.as.spec;                                        \
                                                                               \
    ks[GAB_SEND_KSPECS] = atomic_load(&EG()->messages_epoch);                  \
    ks[GAB_SEND_KTYPE] = gab_valtype(GAB(), r);                                \
    ks[GAB_SEND_KSPEC] = res.as.spec;                                          \
                                                                               \
    switch (gab_valkind(spec)) {                                               \
    case kGAB_PRIMITIVE: {                                                     \
      uint8_t op = gab_valtop(spec);                                           \
                                                                               \
      if (op == OP_SEND_PRIMITIVE_CALL_BLOCK)                                  \
        op += adjust;                                                          \
                                                                               \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, op);                                     \
                                                                               \
      break;                                                                   \
    }                                                                          \
    case kGAB_BLOCK: {                                                         \
      struct gab_oblock *b = GAB_VAL_TO_BLOCK(spec);                           \
      struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);                   \
                                                                               \
      uint8_t local = (GAB_VAL_TO_PROTOTYPE(b->p)->src == BLOCK_PROTO()->src); \
      adjust |= (local << 1);                                                  \
                                                                               \
      if (local) {                                                             \
        ks[GAB_SEND_KOFFSET] = (intptr_t)proto_ip(GAB(), p);                   \
      }                                                                        \
                                                                               \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_BLOCK + adjust);                 \
                                                                               \
      break;                                                                   \
    }                                                                          \
    case kGAB_NATIVE: {                                                        \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_NATIVE);                         \
      break;                                                                   \
    }                                                                          \
    default:                                                                   \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_CONSTANT);                       \
      break;                                                                   \
    }                                                                          \
                                                                               \
    IP() -= GAB_SEND_CACHE_SIZE;                                               \
                                                                               \
    NEXT();                                                                    \
  })

#define MICRO_OP_TRIM(want, have)                                              \
  ({                                                                           \
    uint64_t nulls = 0;                                                        \
                                                                               \
    if (have == want && want < 10) {                                           \
      WRITE_BYTE(2, OP_TRIM_EXACTLY0 + want);                                  \
      IP() -= 2;                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    if (have > want && have - want < 10) {                                     \
      WRITE_BYTE(2, OP_TRIM_DOWN1 - 1 + (have - want));                        \
      IP() -= 2;                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    if (want > have && want - have < 10) {                                     \
      WRITE_BYTE(2, OP_TRIM_UP1 - 1 + (want - have));                          \
      IP() -= 2;                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    SP() -= have;                                                              \
                                                                               \
    if (__gab_unlikely(have != want && want != VAR_EXP)) {                     \
      if (have > want) {                                                       \
        have = want;                                                           \
      } else {                                                                 \
        nulls = want - have;                                                   \
      }                                                                        \
    }                                                                          \
                                                                               \
    SP() += have + nulls;                                                      \
                                                                               \
    while (nulls--)                                                            \
      PEEK_N(nulls + 1) = gab_nil;                                             \
                                                                               \
    SET_HV(want);                                                              \
  })

#define MICRO_OP_USE(have)                                                     \
  ({                                                                           \
    CHECK_SIGNAL();                                                            \
                                                                               \
    STORE();                                                                   \
    uintptr_t reentrant = REENTRANT();                                         \
    union gab_value_pair mod;                                                  \
                                                                               \
    if (reentrant) {                                                           \
      gab_assert(gab_valisfib(reentrant), "Reentrant shall be fiber");         \
                                                                               \
      mod = gab_tfibawait(GAB(), reentrant, 0);                                \
                                                                               \
      RESET_REENTRANT();                                                       \
    } else {                                                                   \
      /*                                                                       \
       * TODO @cgab @api: Really fix this, Its rough in a lot of ways chief.   \
       * This is a better way of pulling arguments, off the fiber itself.      \
       * I don't see why *instead* of this I couldn't just store an            \
       * environment on the fiber that I can forward instead.                  \
       */                                                                      \
      gab_value shp = gab_prtshp(BLOCK()->p);                                  \
                                                                               \
      gab_value rec =                                                          \
          gab_recordfrom(GAB(), shp, 1, FIBER()->len - 2, FIBER()->data + 2);  \
                                                                               \
      bool should_reload = have > 1 ? PEEK_N(have - 1) == gab_true : false;    \
                                                                               \
      gab_value module = have > 1 ? PEEK_N(have - 1) : 0;                      \
                                                                               \
      mod = gab_use(GAB(), (struct gab_use_argt){                              \
                               .flags = should_reload ? fGAB_USE_RELOAD : 0,   \
                               .vpackage_name = r,                             \
                               .vmodule_name = module,                         \
                               .env = rec,                                     \
                           });                                                 \
    }                                                                          \
                                                                               \
    if (mod.status == gab_ctimeout) {                                          \
      gab_assert(gab_valisfib(mod.vresult), "Reentrant shall be fiber");       \
      VM_YIELD(mod.vresult);                                                   \
    }                                                                          \
                                                                               \
    if (mod.status != gab_cvalid)                                              \
      VM_GIVEN(mod);                                                           \
                                                                               \
    if (mod.aresult->data[0] != gab_ok)                                        \
      VM_GIVEN(mod);                                                           \
                                                                               \
    DROP_N(have + FRAME_SIZE);                                                 \
                                                                               \
    for (uint64_t i = 1; i < mod.aresult->len; i++)                            \
      PUSH(mod.aresult->data[i]);                                              \
                                                                               \
    SET_HV(below_have + mod.aresult->len - 1);                                 \
  })

#define MISS_CACHED_SEND(reason)                                               \
  ({                                                                           \
    IP() -= GAB_SEND_CACHE_SIZE - 1;                                           \
    [[clang::musttail]] return OP_SEND_HANDLER(DISPATCH_ARGS());               \
  })

#define MISS_CACHED_TRIM(reason)                                               \
  ({                                                                           \
    IP()--;                                                                    \
    [[clang::musttail]] return OP_TRIM_HANDLER(DISPATCH_ARGS());               \
  })

#define MISS_CACHED_RETURN(reason)                                             \
  ({ [[clang::musttail]] return OP_RETURN_HANDLER(DISPATCH_ARGS()); })

#define VM_YIELD(value)                                                        \
  ({                                                                           \
    IP() -= GAB_SEND_CACHE_SIZE;                                               \
    STORE();                                                                   \
    return __gab_vmyield(GAB(), value);                                        \
  })

#define VM_TERM()                                                              \
  ({                                                                           \
    STORE();                                                                   \
    return __gab_vmterm(GAB(), "While executing $\n", gab_thisfiber(GAB()));   \
  })

#define STORE_SP() (VM()->sp = SP())
#define STORE_FP() (VM()->fp = FB())
#define STORE_IP() (VM()->ip = IP())
#define STORE_KB() (VM()->kb = KB())

#define STORE()                                                                \
  ({                                                                           \
    STORE_SP();                                                                \
    STORE_FP();                                                                \
    STORE_IP();                                                                \
    STORE_KB();                                                                \
  })

#define PANIC_GUARD_KIND(value, kind)                                          \
  if (__gab_unlikely(gab_valkind(value) != kind)) {                            \
    STORE_MICRO_OP_VM_PANIC_FRAME(1);                                          \
    VM_PANIC5(GAB_TYPE_MISMATCH, ks[GAB_SEND_KMESSAGE], ks[GAB_SEND_KSPEC],    \
              value, gab_valtype(GAB(), value), gab_type(GAB(), kind));        \
  }

#define PANIC_GUARD_ISB(value)                                                 \
  if (__gab_unlikely(!gab_valisb(value))) {                                    \
    STORE_MICRO_OP_VM_PANIC_FRAME(have);                                       \
    VM_PANIC5(GAB_TYPE_MISMATCH, ks[GAB_SEND_KMESSAGE], ks[GAB_SEND_KSPEC],    \
              value, gab_valtype(GAB(), value),                                \
              gab_type(GAB(), kGAB_MESSAGE));                                  \
  }

#define PANIC_GUARD_ISN(value)                                                 \
  if (__gab_unlikely(!gab_valisn(value))) {                                    \
    STORE_MICRO_OP_VM_PANIC_FRAME(have);                                       \
    VM_PANIC5(GAB_TYPE_MISMATCH, ks[GAB_SEND_KMESSAGE], ks[GAB_SEND_KSPEC],    \
              value, gab_valtype(GAB(), value), gab_type(GAB(), kGAB_NUMBER)); \
  }

#define PANIC_GUARD_ISS(value) PANIC_GUARD_KIND(value, kGAB_STRING)

#define SEND_GUARD_ISS(value) SEND_GUARD_KIND(value, kGAB_STRING)

#define SEND_GUARD(clause, reason)                                             \
  if (__gab_unlikely(!(clause)))                                               \
    MISS_CACHED_SEND(reason);

#define SEND_GUARD_KIND(r, k) SEND_GUARD(gab_valkind(r) == k, "Unexpected kind")

#define SEND_GUARD_ISN(value) SEND_GUARD(gab_valisn(value), "Not number")
#define SEND_GUARD_ISB(value) SEND_GUARD(gab_valisb(value), "Not number")

/*
 * SEND guard which checks that the
 * world is as we expect, and the receiver is a channel.
 * */
#define SEND_GUARD_ISCHN(r)                                                    \
  SEND_GUARD(gab_valkind(r) >= kGAB_CHANNEL &&                                 \
                 gab_valkind(r) <= kGAB_CHANNELCLOSED,                         \
             "Not Channel")

/*
 * SEND guard which checks that the world
 * is as we expect, and the receiver is a record.
 */
#define SEND_GUARD_ISREC(r) SEND_GUARD_KIND(r, kGAB_RECORD)

/*
 * SEND guard which checks that the world
 * is as we expect, and the receiver is a shape.
 */
#define SEND_GUARD_ISSHP(r)                                                    \
  SEND_GUARD(gab_valkind(r) == kGAB_SHAPE || gab_valkind(r) == kGAB_SHAPELIST, \
             "Not shape")

/*
 * SEND guard which compares the message record checked against last time
 * to the current rec.
 *
 * IS IT POSSIBLE THAT THE MESSAGE SPECS are *replaced* by another at the same
 * address after a collection happens, and then some sends *think* they have it
 * cached but they havent?
 */
#define SEND_GUARD_CACHED_MESSAGE_SPECS(epoch)                                 \
  SEND_GUARD(gab_valeq(atomic_load(&EG()->messages_epoch), epoch),             \
             "Global message change detected.")

#define SEND_GUARD_TYPE(r, type)                                               \
  SEND_GUARD(gab_valisa(GAB(), r, type), "Type failed")

/*
 * SEND guard which checks that the world is
 * as we expect, and that the receiver type is the
 * same as seen last time.
 */
#define SEND_GUARD_CACHED_RECEIVER_TYPE(r)                                     \
  SEND_GUARD_TYPE(r, ks[GAB_SEND_KTYPE])

#define SEND_GUARD_CACHED_MATCH_TYPE(r, ks)                                    \
  ({                                                                           \
    int64_t idx = MATCH_HASHT(gab_valtype(GAB(), r));                          \
    SEND_GUARD_TYPE(r, ks[GAB_SEND_KTYPE + idx]);                              \
  })

#define TRIM_GUARD(clause, reason)                                             \
  if (__gab_unlikely(!(clause)))                                               \
    MISS_CACHED_TRIM(reason);

#define TRIM_GUARD_EXACTLY_N(want, n)                                          \
  TRIM_GUARD(HV() == want, "Mismatched tuple length")

#define TRIM_GUARD_UP_N(want, n)                                               \
  TRIM_GUARD((HV() + n) == want, "Mismatched tuple length")

#define TRIM_GUARD_DOWN_N(want, n)                                             \
  TRIM_GUARD((HV() - n) == want, "Mismatched tuple length")

#define RETURN_GUARD(clause, reason)                                           \
  if (__gab_unlikely(!(clause)))                                               \
    MISS_CACHED_RETURN(reason);

#define RETURN_GUARD_EXACTLY_N(n)                                              \
  RETURN_GUARD(HV() == n, "Mismatched return tuple length")

#define SHORTCUT_GUARD_ARGS_LT(n)                                              \
  ({                                                                           \
    if (__gab_unlikely(have < n))                                              \
      SET_HV(below_have + 1), NEXT();                                          \
  })

#define NILPAD_GUARD_ARGS_GTE(n)                                               \
  ({                                                                           \
    if (__gab_unlikely(have < n)) {                                            \
      PANIC_GUARD_STACKSPACE(n - have);                                        \
      while (have < n)                                                         \
        PUSH(MICRO_OP_NIL()), have++;                                          \
    }                                                                          \
  })

// If the LSB is 1, the number is not divisible by 2.
#define MICRO_OP_RECORD(len)                                                   \
  ({                                                                           \
    STORE_SP();                                                                \
    uint64_t sz = len;                                                         \
    gab_value record = gab_record(GAB(), 2, sz / 2, SP() - sz, SP() + 1 - sz); \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (record == gab_cinvalid)                                                \
      VM_TERM();                                                               \
                                                                               \
    if (record == gab_ctimeout)                                                \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    record;                                                                    \
  })

#define MICRO_OP_RECORDFROM(shape, len)                                        \
  ({                                                                           \
    STORE_SP();                                                                \
    uint64_t sz = len;                                                         \
    gab_value record = gab_recordfrom(GAB(), shape, 1, sz, SP() - sz);         \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (record == gab_cinvalid)                                                \
      VM_TERM();                                                               \
                                                                               \
    if (record == gab_ctimeout)                                                \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    record;                                                                    \
  })

#define MICRO_OP_SHAPE(len)                                                    \
  ({                                                                           \
    STORE_SP();                                                                \
    uint64_t sz = len;                                                         \
                                                                               \
    gab_value shape = gab_shape(GAB(), 1, sz, SP() - sz);                      \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (shape == gab_cinvalid)                                                 \
      VM_TERM();                                                               \
                                                                               \
    if (shape == gab_ctimeout)                                                 \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    shape;                                                                     \
  })

#define MICRO_OP_LIST(n, len)                                                  \
  ({                                                                           \
    STORE_SP();                                                                \
    uint64_t sz = len;                                                         \
    gab_value list = gab_list(GAB(), 1, sz, SP() - ((n) + sz));                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (list == gab_cinvalid)                                                  \
      VM_TERM();                                                               \
                                                                               \
    if (list == gab_ctimeout)                                                  \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    list;                                                                      \
  })

#define MICRO_OP_STRING(n, len)                                                \
  ({                                                                           \
    STORE_SP();                                                                \
    uint64_t sz = len;                                                         \
    gab_value str = gab_nvstring(GAB(), sz, SP() - ((n) + sz));                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (str == gab_cinvalid)                                                   \
      VM_TERM();                                                               \
                                                                               \
    if (str == gab_ctimeout)                                                   \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    str;                                                                       \
  })

#define MICRO_OP_BINARY(n, len)                                                \
  ({                                                                           \
    STORE_SP();                                                                \
    uint64_t sz = len;                                                         \
    gab_value bin = gab_nvbinary(GAB(), sz, SP() - ((n) + sz));                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (bin == gab_cinvalid)                                                   \
      VM_TERM();                                                               \
                                                                               \
    if (bin == gab_ctimeout)                                                   \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    bin;                                                                       \
  })

#define MICRO_OP_CHANNEL()                                                     \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_channel(GAB());                                                        \
  })

#define MICRO_OP_PACK_LIST(below, above)                                       \
  ({                                                                           \
    uint64_t have = HV();                                                      \
                                                                               \
    uint64_t want = below + above;                                             \
                                                                               \
    while (have < want)                                                        \
      PUSH(MICRO_OP_NIL()), have++;                                            \
                                                                               \
    SET_HV(have);                                                              \
                                                                               \
    gab_assert(have >= want, "Shall have padded values to at least want");     \
    int64_t len = have - want;                                                 \
                                                                               \
    gab_value *ap = SP() - above;                                              \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    gab_value rec = gab_list(GAB(), 1, len, ap - len);                         \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (rec == gab_cinvalid)                                                   \
      VM_TERM();                                                               \
                                                                               \
    if (rec == gab_ctimeout)                                                   \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    DROP_N(len - 1);                                                           \
                                                                               \
    if (len)                                                                   \
      gmoved(ap - len + 1, ap, above);                                         \
    else                                                                       \
      gmovea(ap + 1, ap, above);                                               \
                                                                               \
    PEEK_N(above + 1) = rec;                                                   \
                                                                               \
    SET_HV(want + 1);                                                          \
  })

#define MICRO_OP_PACK_DICT(below, above)                                       \
  ({                                                                           \
    uint64_t have = HV();                                                      \
                                                                               \
    uint64_t want = below + above;                                             \
                                                                               \
    while (have < want)                                                        \
      PUSH(MICRO_OP_NIL()), have++;                                            \
                                                                               \
    SET_HV(have);                                                              \
                                                                               \
    gab_assert(have >= want, "Shall have padded values to at least want");     \
    int64_t len = have - want;                                                 \
                                                                               \
    gab_value *ap = SP() - above;                                              \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    gab_value rec = gab_record(GAB(), 2, len / 2, ap - len, ap - len + 1);     \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (rec == gab_cinvalid)                                                   \
      VM_TERM();                                                               \
                                                                               \
    if (rec == gab_ctimeout)                                                   \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    DROP_N(len - 1);                                                           \
                                                                               \
    if (len)                                                                   \
      gmoved(ap - len + 1, ap, above);                                         \
    else                                                                       \
      gmovea(ap + 1, ap, above);                                               \
                                                                               \
    PEEK_N(above + 1) = rec;                                                   \
                                                                               \
    SET_HV(want + 1);                                                          \
  })

#define MICRO_OP_BLOCK(p)                                                      \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_value blk = gab_block(GAB(), p);                                       \
                                                                               \
    struct gab_oblock *b = GAB_VAL_TO_BLOCK(blk);                              \
    struct gab_oprototype *proto = GAB_VAL_TO_PROTOTYPE(p);                    \
                                                                               \
    for (int i = 0; i < proto->nupvalues; i++) {                               \
      uint8_t is_local = proto->data[i] & fLOCAL_LOCAL;                        \
      uint8_t index = proto->data[i] >> 1;                                     \
                                                                               \
      if (is_local)                                                            \
        b->upvalues[i] = LOCAL(index);                                         \
      else                                                                     \
        b->upvalues[i] = UPVALUE(index);                                       \
    }                                                                          \
                                                                               \
    blk;                                                                       \
  })

#define MICRO_OP_TYPE(v) (gab_valtype(GAB(), v))

#define PUSHTUPLE(n)                                                           \
  ({                                                                           \
    SP() += 2;                                                                 \
    SP()[-1] = FRAME_IP;                                                       \
    SP()[-2] = FRAME_BK;                                                       \
    PUSH(n);                                                                   \
  })

#define MICRO_OP_RETURN(have)                                                  \
  ({                                                                           \
    uint64_t below_have = RETURN_HAVE();                                       \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB() - (FRAME_SIZE);                                       \
                                                                               \
    if (__gab_unlikely(RETURN_FB_DELTA() == 0)) {                              \
      STORE();                                                                 \
                                                                               \
      gmoved(to, from, have);                                                  \
      SP() = to + have;                                                        \
      SET_HV(have + below_have);                                               \
                                                                               \
      [[clang::musttail]] return __gab_vmok(DISPATCH_ARGS());                       \
    }                                                                          \
                                                                               \
    gab_assert(RETURN_IP() != nullptr, "Shall not return to nullptr ip");      \
                                                                               \
    LOAD_FRAME();                                                              \
                                                                               \
    gmoved(to, from, have);                                                    \
    SP() = to + have;                                                          \
    SET_HV(have + below_have);                                                 \
                                                                               \
    gab_assert(FB() >= VM()->sb + FRAME_SIZE,                                  \
               "FB shall be within vm stack range");                           \
    gab_assert(BLOCK()->header.kind == kGAB_BLOCK,                             \
               "Block shall be gab\\block");                                   \
    gab_assert(BLOCK_PROTO()->header.kind == kGAB_PROTOTYPE,                   \
               "Proto shall be gab\\proto");                                   \
  })

#define MICRO_OP_UVRECAT(r, i) (gab_uvrecat(r, i))

#define MICRO_OP_UKRECAT(r, i) (gab_ukrecat(r, i))

#define MICRO_OP_SPLATSHAPE(s)                                                 \
  ({                                                                           \
    uint64_t len = gab_shplen(s);                                              \
                                                                               \
    for (uint64_t i = 0; i < len; i++)                                         \
      PUSH(gab_ushpat(s, i));                                                  \
                                                                               \
    len;                                                                       \
  })

#define MICRO_OP_CONS_RECORD(r, arg) (gab_lstpush(GAB(), r, arg))

#define MICRO_OP_CONS(a, b) (gab_listof(GAB(), a, b))

#define MICRO_OP_SENDK() (ks[GAB_SEND_KSPEC])

#define MICRO_OP_NIL() (gab_nil)

#define MICRO_OP_SPILL(r, n) (r)

#define MICRO_OP_BINARY_ADD(a, b) (a + b)
#define MICRO_OP_BINARY_SUB(a, b) (a - b)
#define MICRO_OP_BINARY_MUL(a, b) (a * b)
#define MICRO_OP_BINARY_DIV(a, b) (a / b)
#define MICRO_OP_BINARY_LT(a, b) (a < b)
#define MICRO_OP_BINARY_LTE(a, b) (a <= b)
#define MICRO_OP_BINARY_GT(a, b) (a > b)
#define MICRO_OP_BINARY_GTE(a, b) (a >= b)
#define MICRO_OP_BINARY_BOR(a, b) (a | b)
#define MICRO_OP_BINARY_BND(a, b) (a & b)

// TODO @cgab @bug: For some reason 0.f / 0.f is causing seg fault.
// I'm not sure if its just a debug-mode issue.
#define MICRO_OP_BINARY_MOD(a, b) (__gab_unlikely(b == 0) ? (0.f) : (a % b))

#define BINARY_SHIFT(a, b, op, op_op)                                          \
  ({                                                                           \
    gab_int result = ((__gab_unlikely(b >= GAB_INTWIDTH)) ? (0)                \
                      : (__gab_unlikely(b < 0)) ? (a op_op(uint32_t)(-b))      \
                                                : (a op(uint32_t) b));         \
    result;                                                                    \
  })

#define MICRO_OP_BINARY_LSH(a, b) BINARY_SHIFT(a, b, <<, >>)
#define MICRO_OP_BINARY_RSH(a, b) BINARY_SHIFT(a, b, >>, <<)

#define MICRO_OP_BINARY_EQ(a, b) (gab_valeq(a, b))

#define MICRO_OP_BINARY_CONCAT(a, b)                                           \
  ({                                                                           \
    gab_value val_ab = gab_tstrcat(GAB(), a, b);                               \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (val_ab == gab_cinvalid)                                                \
      VM_TERM();                                                               \
                                                                               \
    if (val_ab == gab_ctimeout)                                                \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    gab_assert(gab_valkind(val_ab) == kGAB_STRING,                             \
               "concat shall return string");                                  \
                                                                               \
    val_ab;                                                                    \
  })

#define MICRO_OP_BINARY_STRLT(a, b) (strcoll(a, b) < 0)
#define MICRO_OP_BINARY_STRLTE(a, b) (strcoll(a, b) <= 0)
#define MICRO_OP_BINARY_STRGT(a, b) (strcoll(a, b) > 0)
#define MICRO_OP_BINARY_STRGTE(a, b) (strcoll(a, b) >= 0)

#define MICRO_OP_UNARY_BIN(a) (~a)
#define MICRO_OP_UNARY_LIN(a) (!a)

#define MICRO_OP_BOXN(dbl) (gab_number(dbl))
#define MICRO_OP_BOXI(i) (gab_safeinteger(i))
#define MICRO_OP_BOXB(t_or_f) (gab_bool(t_or_f))
#define MICRO_OP_BOXV(v) (v)

#define MICRO_OP_UNBOXF(v) (gab_valtof(v))
#define MICRO_OP_UNBOXI(v) (({ gab_valtoi(v); }))
#define MICRO_OP_UNBOXU(v) (({ gab_valtou(v); }))
#define MICRO_OP_UNBOXB(v) (gab_valintob(v))
#define MICRO_OP_UNBOXS(v) (gab_strdata(&v))
#define MICRO_OP_UNBOXV(v) (v)

#define MICRO_OP_UNBOXF2(v) (MICRO_OP_UNBOXF(v))
#define MICRO_OP_UNBOXI2(v) (MICRO_OP_UNBOXI(v))
#define MICRO_OP_UNBOXU2(v) (MICRO_OP_UNBOXU(v))
#define MICRO_OP_UNBOXB2(v) (MICRO_OP_UNBOXB(v))
#define MICRO_OP_UNBOXS2(v) (MICRO_OP_UNBOXS(v))
#define MICRO_OP_UNBOXV2(v) (MICRO_OP_UNBOXV(v))

#define MICRO_OP_UNBOXF_T gab_float
#define MICRO_OP_UNBOXU_T gab_uint
#define MICRO_OP_UNBOXI_T gab_int
#define MICRO_OP_UNBOXB_T bool
#define MICRO_OP_UNBOXS_T const char *
#define MICRO_OP_UNBOXV_T gab_value

#define SEND_GUARD_NOP(v) SEND_GUARD_CACHED_RECEIVER_TYPE(v)
#define PANIC_GUARD_NOP(v)

#define IMPL_SEND_UNARY(CODE, guard, boxer, operation_type, unboxer,           \
                        primitive)                                             \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_SENDCONSTANTS;                                        \
    uint64_t have = HV();                                                      \
    uint64_t below_have = BELOW_HV();                                          \
                                                                               \
    SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                      \
                                                                               \
    gab_value r = PEEK_N(have);                                                \
                                                                               \
    SEND_GUARD_##guard(r);                                                     \
                                                                               \
    operation_type val = unboxer(r);                                           \
                                                                               \
    DROP_N(have + FRAME_SIZE);                                                 \
                                                                               \
    PUSH(boxer(primitive(val)));                                               \
                                                                               \
    SET_HV(below_have + 1);                                                    \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_SEND_BINARY(CODE, guard, a_type, a_unboxer, b_type, b_unboxer,    \
                         c_type, c_boxer, primitive)                           \
  CASE_CODE(SEND_##CODE) {                                                     \
    gab_value *ks = READ_SENDCONSTANTS;                                        \
    uint64_t have = HV();                                                      \
    uint64_t below_have = BELOW_HV();                                          \
                                                                               \
    SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                      \
                                                                               \
    NILPAD_GUARD_ARGS_GTE(2);                                                  \
                                                                               \
    gab_value b = PEEK_N(have - 1);                                            \
    gab_value a = PEEK_N(have);                                                \
                                                                               \
    SEND_GUARD_##guard(a);                                                     \
    PANIC_GUARD_##guard(b);                                                    \
                                                                               \
    a_type val_a = a_unboxer(a);                                               \
    b_type val_b = b_unboxer##2(b);                                            \
                                                                               \
    c_type val_c = primitive(val_a, val_b);                                    \
                                                                               \
    gab_value c = c_boxer(val_c);                                              \
                                                                               \
    DROP_N(have + FRAME_SIZE);                                                 \
                                                                               \
    PUSH(c);                                                                   \
                                                                               \
    SET_HV(below_have + 1);                                                    \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_RETURN_N(n)                                                       \
  CASE_CODE(RETURN_##n) {                                                      \
    RETURN_GUARD_EXACTLY_N((uint64_t)n);                                                 \
                                                                               \
    MICRO_OP_RETURN((uint64_t)n);                                                        \
                                                                               \
    NEXT();                                                                    \
  }

#define IMPL_TRIM_N(n)                                                         \
  CASE_CODE(TRIM_DOWN##n) {                                                    \
    uint8_t want = READ_BYTE;                                                  \
                                                                               \
    TRIM_GUARD_DOWN_N(want, n);                                                \
                                                                               \
    DROP_N(n);                                                                 \
                                                                               \
    SET_HV(want);                                                              \
                                                                               \
    NEXT();                                                                    \
  }                                                                            \
                                                                               \
  CASE_CODE(TRIM_EXACTLY##n) {                                                 \
    uint8_t want = READ_BYTE;                                                  \
                                                                               \
    TRIM_GUARD_EXACTLY_N(want, n);                                             \
                                                                               \
    NEXT();                                                                    \
  }                                                                            \
                                                                               \
  CASE_CODE(TRIM_UP##n) {                                                      \
    uint8_t want = READ_BYTE;                                                  \
                                                                               \
    TRIM_GUARD_UP_N(want, n);                                                  \
                                                                               \
    for (int i = 0; i < n; i++)                                                \
      PUSH(MICRO_OP_NIL());                                                    \
                                                                               \
    SET_HV(want);                                                              \
                                                                               \
    NEXT();                                                                    \
  }

// TODO @cgab @vm @perf: Handle undefined and record case
CASE_CODE(MATCHTAILSEND_BLOCK) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  gab_value r = PEEK_N(have);

  // TODO @cgab @vm @perf: Handle undefined and record case
  uint8_t idx = MATCH_HASHT(gab_valtype(GAB(), r));
  SEND_GUARD_TYPE(r, ks[GAB_SEND_KTYPE + idx]);

  MICRO_OP_MATCHTAILCALL_BLOCK(idx, have);

  NEXT();
}

CASE_CODE(MATCHSEND_BLOCK) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  gab_value r = PEEK_N(have);

  uint8_t idx = MATCH_HASHT(gab_valtype(GAB(), r));
  SEND_GUARD_TYPE(r, ks[GAB_SEND_KTYPE + idx]);

  MICRO_OP_MATCHCALL_BLOCK(idx, have);

  NEXT();
}

CASE_CODE(LOAD_UPVALUE) {
  uint64_t have = HV();

  PUSH(UPVALUE(READ_BYTE));

  SET_HV(have + 1);

  NEXT();
}

CASE_CODE(NLOAD_UPVALUE) {
  uint8_t n = READ_BYTE;

  PANIC_GUARD_STACKSPACE(n);

  uint64_t have = HV();

  SP()[n] = have + n;

  while (n--)
    PUSH(UPVALUE(READ_BYTE));

  NEXT();
}

CASE_CODE(LOAD_LOCAL) {
  uint64_t have = HV();

  PUSH(LOCAL(READ_BYTE));

  SET_HV(have + 1);

  NEXT();
}

CASE_CODE(NLOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  PANIC_GUARD_STACKSPACE(n);

  uint64_t have = HV();
  uint64_t len = have + n;

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_HV(len);

  NEXT();
}

CASE_CODE(STORE_LOCAL) {
  STORE_LOCAL(READ_BYTE, PEEK());
  NEXT();
}

CASE_CODE(POPSTORE_LOCAL) {
  uint64_t have = HV();

  STORE_LOCAL(READ_BYTE, POP());

  gab_assert(have >= 1, "May not underflow have");
  SET_HV(have - 1);
  NEXT();
}

CASE_CODE(NPOPSTORE_LOCAL) {
  uint64_t have = HV();

  uint8_t n = READ_BYTE;

  gab_assert(have >= n, "May not underflow have");
  have -= n;

  while (n--)
    STORE_LOCAL(READ_BYTE, POP());

  SET_HV(have);
  NEXT();
}

CASE_CODE(NPOPSTORE_STORE_LOCAL) {
  uint64_t have = HV();

  uint8_t n = READ_BYTE;

  gab_assert(have >= n, "May not underflow have");
  have -= n;

  while (n-- > 1)
    STORE_LOCAL(READ_BYTE, POP());

  STORE_LOCAL(READ_BYTE, PEEK());

  SET_HV(have + 1);
  NEXT();
}

CASE_CODE(SEND_NATIVE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_onative *n = GAB_VAL_TO_NATIVE(ks[GAB_SEND_KSPEC]);

  MICRO_OP_CALL_NATIVE(n, have, below_have, true);

  NEXT();
}

CASE_CODE(SEND_BLOCK) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  MICRO_OP_CALL_BLOCK(b, have);

  NEXT();
}

/*
 * TODO @vm @perf: Specializer tailsends for HV().
 * Maybe specialize each of these even further based on the HV amount -
 * this would allow the gmoved in TAILCALL to be even further optimized
 * by the compiler, as the have argument would be compile-time.
 */
CASE_CODE(TAILSEND_BLOCK) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  MICRO_OP_TAILCALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(LOCALSEND_BLOCK) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  MICRO_OP_LOCALCALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(LOCALTAILSEND_BLOCK) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC]);

  MICRO_OP_LOCALTAILCALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CALL_BLOCK) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(r);

  MICRO_OP_CALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(TAILSEND_PRIMITIVE_CALL_BLOCK) {
  SKIP_SHORT;
  // gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_BLOCK);

  struct gab_oblock *b = GAB_VAL_TO_BLOCK(r);

  MICRO_OP_TAILCALL_BLOCK(b, have);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CALL_NATIVE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  PANIC_GUARD_KIND(r, kGAB_NATIVE);

  struct gab_onative *n = GAB_VAL_TO_NATIVE(r);

  MICRO_OP_CALL_NATIVE(n, have, below_have, false);

  NEXT();
}

// float + float = float
IMPL_SEND_BINARY(PRIMITIVE_ADD, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXF_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_ADD);

// float - float = float
IMPL_SEND_BINARY(PRIMITIVE_SUB, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXF_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_SUB);

// float * float = float
IMPL_SEND_BINARY(PRIMITIVE_MUL, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXF_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_MUL);

// float / float = float
IMPL_SEND_BINARY(PRIMITIVE_DIV, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXF_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_DIV);

// int % int = int
IMPL_SEND_BINARY(PRIMITIVE_MOD, ISN, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_MOD);

// float < float = bool
IMPL_SEND_BINARY(PRIMITIVE_LT, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_LT);

// float <= float = bool
IMPL_SEND_BINARY(PRIMITIVE_LTE, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_LTE);

// float >= float = bool
IMPL_SEND_BINARY(PRIMITIVE_GT, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_GT);

// float >= float = bool
IMPL_SEND_BINARY(PRIMITIVE_GTE, ISN, MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF,
                 MICRO_OP_UNBOXF_T, MICRO_OP_UNBOXF, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_GTE);

// int | int = int
IMPL_SEND_BINARY(PRIMITIVE_BOR, ISN, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_BOR);

// int & int = int
IMPL_SEND_BINARY(PRIMITIVE_BND, ISN, MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_BND);

// Implemented logical and/or for booleans with a binary &/| operation.
// bool | bool = bool
IMPL_SEND_BINARY(PRIMITIVE_LOR, ISB, MICRO_OP_UNBOXB_T, MICRO_OP_UNBOXB,
                 MICRO_OP_UNBOXB_T, MICRO_OP_UNBOXB, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_BOR);

// bool & bool = bool
IMPL_SEND_BINARY(PRIMITIVE_LND, ISB, MICRO_OP_UNBOXB_T, MICRO_OP_UNBOXB,
                 MICRO_OP_UNBOXB_T, MICRO_OP_UNBOXB, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_BND);

// str < str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_LT, ISS, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS,
                 MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_STRLT);

// str <= str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_LTE, ISS, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS,
                 MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_STRLTE);

// str > str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_GT, ISS, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS,
                 MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_STRGT);

// str >= str = bool
IMPL_SEND_BINARY(PRIMITIVE_STR_GTE, ISS, MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS,
                 MICRO_OP_UNBOXS_T, MICRO_OP_UNBOXS, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_STRGTE);
// uint << int = uint
IMPL_SEND_BINARY(PRIMITIVE_LSH, ISN, MICRO_OP_UNBOXU_T, MICRO_OP_UNBOXU,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_LSH);

// uint >> int = uint
IMPL_SEND_BINARY(PRIMITIVE_RSH, ISN, MICRO_OP_UNBOXU_T, MICRO_OP_UNBOXU,
                 MICRO_OP_UNBOXI_T, MICRO_OP_UNBOXI, MICRO_OP_UNBOXI_T,
                 MICRO_OP_BOXN, MICRO_OP_BINARY_RSH);

// str + str = str
IMPL_SEND_BINARY(PRIMITIVE_CONCAT, ISS, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV,
                 MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV, MICRO_OP_UNBOXV_T,
                 MICRO_OP_BOXV, MICRO_OP_BINARY_CONCAT);

// val == val = bool
IMPL_SEND_BINARY(PRIMITIVE_EQ, NOP, MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV,
                 MICRO_OP_UNBOXV_T, MICRO_OP_UNBOXV, MICRO_OP_UNBOXB_T,
                 MICRO_OP_BOXB, MICRO_OP_BINARY_EQ);

// !bool = bool
IMPL_SEND_UNARY(PRIMITIVE_LIN, ISB, MICRO_OP_BOXB, MICRO_OP_UNBOXB_T,
                MICRO_OP_UNBOXB, MICRO_OP_UNARY_LIN);

// ~int = int
IMPL_SEND_UNARY(PRIMITIVE_BIN, ISN, MICRO_OP_BOXN, MICRO_OP_UNBOXI_T,
                MICRO_OP_UNBOXI, MICRO_OP_UNARY_BIN);

// val? = val
IMPL_SEND_UNARY(PRIMITIVE_TYPE, NOP, MICRO_OP_BOXV, MICRO_OP_UNBOXV_T,
                MICRO_OP_UNBOXV, MICRO_OP_TYPE);

CASE_CODE(SEND_PRIMITIVE_USE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SEND_GUARD_KIND(r, kGAB_STRING);

  MICRO_OP_USE(have);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  SHORTCUT_GUARD_ARGS_LT(2);

  gab_value a = PEEK_N(have);

  gab_value b = PEEK_N(have - 1);

  STORE_SP();

  gab_gclock(GAB());

  gab_value res = MICRO_OP_CONS(a, b);

  DROP_N(have + FRAME_SIZE);

  PUSH(res);

  SET_HV(below_have + 1);

  gab_gcunlock(GAB());

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CONS_RECORD) {
  gab_value *ks = READ_SENDCONSTANTS; // Constants
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  SHORTCUT_GUARD_ARGS_LT(2);

  STORE_SP();

  gab_value arg = PEEK_N(have - 1);

  gab_gclock(GAB());

  gab_value res = MICRO_OP_CONS_RECORD(r, arg);

  DROP_N(have + FRAME_SIZE);

  PUSH(res);

  SET_HV(below_have + 1);

  gab_gcunlock(GAB());

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATSHAPE) {
  gab_value *ks = READ_SENDCONSTANTS; // Constants
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value s = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_ISSHP(s);

  DROP_N(have + FRAME_SIZE);

  PANIC_GUARD_STACKSPACE_SPLATSHAPE(s);

  uint64_t len = MICRO_OP_SPLATSHAPE(s);

  SET_HV(below_have + len);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATLIST) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  uint64_t n = gab_shplen(ks[GAB_SEND_KTYPE]);

  r = MICRO_OP_SPILL(r, n - (have + 1));

  DROP_N(have + FRAME_SIZE);

  PANIC_GUARD_STACKSPACE(n);

  for (uint64_t i = 0; i < n; i++)
    PUSH(MICRO_OP_UVRECAT(r, i));

  SET_HV(below_have + n);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SPLATDICT) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  uint64_t n = gab_shplen(ks[GAB_SEND_KTYPE]);

  DROP_N(have + FRAME_SIZE);

  PANIC_GUARD_STACKSPACE(n * 2);

  for (uint64_t i = 0; i < n; i++)
    PUSH(MICRO_OP_UKRECAT(r, i)), PUSH(MICRO_OP_UVRECAT(r, i));

  SET_HV(below_have + n);

  NEXT();
}

CASE_CODE(SEND_CONSTANT) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);
  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  gab_value spec = MICRO_OP_SENDK();

  DROP_N(have + FRAME_SIZE);

  PUSH(spec);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PROPERTY) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value r = PEEK_N(have);

  SEND_GUARD_KIND(r, kGAB_RECORD);

  SEND_GUARD_CACHED_RECEIVER_TYPE(r);

  r = MICRO_OP_SPILL(r, 0);

  DROP_N(have + FRAME_SIZE);

  PUSH(MICRO_OP_UVRECAT(r, ks[GAB_SEND_KSPEC]));

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(NOP) { NEXT(); }

CASE_CODE(CONSTANT) {
  uint64_t have = HV();

  PUSH(READ_CONSTANT);

  SET_HV(have + 1);

  NEXT();
}

CASE_CODE(NCONSTANT) {
  uint8_t n = READ_BYTE;

  PANIC_GUARD_STACKSPACE(n);

  uint64_t have = HV() + n;

  while (n--)
    PUSH(READ_CONSTANT);

  SET_HV(have);

  NEXT();
}

CASE_CODE(POP) {
  uint64_t have = HV();

  DROP();

  SET_HV(have - 1);
  NEXT();
}

CASE_CODE(POP_N) {
  uint64_t have = HV();

  uint8_t n = READ_BYTE;
  DROP_N(n);

  SET_HV(have - n);
  NEXT();
}

CASE_CODE(BLOCK) {
  gab_value p = READ_CONSTANT;
  uint64_t have = HV();

  gab_value blk = MICRO_OP_BLOCK(p);

  PUSH(blk);

  SET_HV(have + 1);

  NEXT();
}

CASE_CODE(TUPLE) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  SET_HV(0);

  NEXT();
}

CASE_CODE(NTUPLE) {
  uint8_t n = READ_BYTE;

  while (n--) {
    uint64_t have = HV();
    PUSHTUPLE(have);
    SET_HV(0);
  }

  NEXT();
}

CASE_CODE(TUPLE_CONSTANT) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  PUSH(READ_CONSTANT);

  SET_HV(1);

  NEXT();
}

CASE_CODE(TUPLE_NCONSTANT) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  uint8_t n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(READ_CONSTANT);

  SET_HV(have);

  NEXT();
}

CASE_CODE(TUPLE_LOAD_LOCAL) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  PUSH(LOCAL(READ_BYTE));

  SET_HV(1);

  NEXT();
}

CASE_CODE(TUPLE_NLOAD_LOCAL) {
  uint64_t have = HV();

  PUSHTUPLE(have);

  uint8_t n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_HV(have);

  NEXT();
}

CASE_CODE(NTUPLE_LOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  uint64_t have = HV();
  PUSHTUPLE(have);

  while (--n)
    PUSHTUPLE(0);

  PUSH(LOCAL(READ_BYTE));

  SET_HV(1);

  NEXT();
}

CASE_CODE(NTUPLE_NLOAD_LOCAL) {
  uint8_t n = READ_BYTE;

  uint64_t have = HV();
  PUSHTUPLE(have);

  while (--n)
    PUSHTUPLE(0);

  n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(LOCAL(READ_BYTE));

  SET_HV(have);

  NEXT();
}

CASE_CODE(NTUPLE_CONSTANT) {
  uint8_t n = READ_BYTE;

  uint64_t have = HV();
  PUSHTUPLE(have);

  while (--n)
    PUSHTUPLE(0);

  PUSH(READ_CONSTANT);

  SET_HV(1);

  NEXT();
}

CASE_CODE(NTUPLE_NCONSTANT) {
  uint8_t n = READ_BYTE;

  uint64_t have = HV();
  PUSHTUPLE(have);

  while (--n)
    PUSHTUPLE(0);

  n = READ_BYTE;

  have = n;

  PANIC_GUARD_STACKSPACE(n);

  while (n--)
    PUSH(READ_CONSTANT);

  SET_HV(have);

  NEXT();
}

IMPL_TRIM_N(0)
IMPL_TRIM_N(1)
IMPL_TRIM_N(2)
IMPL_TRIM_N(3)
IMPL_TRIM_N(4)
IMPL_TRIM_N(5)
IMPL_TRIM_N(6)
IMPL_TRIM_N(7)
IMPL_TRIM_N(8)
IMPL_TRIM_N(9)

CASE_CODE(TRIM) {
  uint8_t want = READ_BYTE;
  uint64_t have = HV();

  MICRO_OP_TRIM(want, have);

  NEXT();
}

IMPL_RETURN_N(1)
IMPL_RETURN_N(2)
IMPL_RETURN_N(3)
IMPL_RETURN_N(4)
IMPL_RETURN_N(5)
IMPL_RETURN_N(6)
IMPL_RETURN_N(7)
IMPL_RETURN_N(8)
IMPL_RETURN_N(9)

CASE_CODE(RETURN) {
  uint64_t have = HV();

  if (have > 0 && have < 10) {
    WRITE_BYTE(1, OP_RETURN + have);
    IP() -= 1;
    NEXT();
  }

  MICRO_OP_RETURN(have);

  NEXT();
}

CASE_CODE(PACK_DICT) {
  uint8_t below = READ_BYTE;
  uint8_t above = READ_BYTE;

  MICRO_OP_PACK_DICT(below, above);

  NEXT();
}

CASE_CODE(PACK_LIST) {
  uint8_t below = READ_BYTE;
  uint8_t above = READ_BYTE;

  MICRO_OP_PACK_LIST(below, above);

  NEXT();
}

CASE_CODE(SEND) {
  uint8_t adjust;
  gab_value *ks = READ_SENDCONSTANTS_ANDTAIL(adjust);
  uint64_t have = HV();

  /* Have can not be 0. We need a receiver. */
  if (__gab_unlikely(!have)) {
    PUSH(MICRO_OP_NIL());
    SET_HV(1);
    have++;
  }

  gab_value r = PEEK_N(have);
  gab_value m = ks[GAB_SEND_KMESSAGE];

  if (BLOCK() && __gab_vmtrysetuplocalmatch(GAB(), m, ks, BLOCK_PROTO())) {
    WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_MATCHSEND_BLOCK + adjust);
    IP() -= GAB_SEND_CACHE_SIZE;
    NEXT();
  }

  /* Do the expensive lookup */
  struct gab_impl_rest res = gab_impl(GAB(), m, r);

  if (__gab_unlikely(!res.status))
    VM_PANIC3(GAB_SPECIALIZATION_MISSING, m, r, gab_valtype(GAB(), r));

  gab_value spec = res.status == kGAB_IMPL_PROPERTY
                       ? gab_primitive(OP_SEND_PROPERTY)
                       : res.as.spec;

  ks[GAB_SEND_KSPECS] = atomic_load(&EG()->messages_epoch);
  ks[GAB_SEND_KTYPE] = gab_valtype(GAB(), r);
  ks[GAB_SEND_KSPEC] = res.as.spec;

  switch (gab_valkind(spec)) {
  case kGAB_PRIMITIVE: {
    uint8_t op = gab_valtop(spec);

    if (op == OP_SEND_PRIMITIVE_CALL_BLOCK)
      op += adjust;

    WRITE_BYTE(GAB_SEND_CACHE_SIZE, op);

    break;
  }
  case kGAB_BLOCK: {
    struct gab_oblock *b = GAB_VAL_TO_BLOCK(spec);
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);

    uint8_t local = (BLOCK() && BLOCK_PROTO()->src == p->src);
    adjust |= (local << 1);

    if (local) {
      ks[GAB_SEND_KOFFSET] = (intptr_t)proto_ip(GAB(), p);
    }

    WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_BLOCK + adjust);

    break;
  }
  case kGAB_NATIVE: {
    WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_NATIVE);
    break;
  }
  default:
    WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_CONSTANT);
    break;
  }

  IP() -= GAB_SEND_CACHE_SIZE;

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_TAKE) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value c = PEEK_N(have);

  MICRO_OP_TAKE(c);
}

CASE_CODE(SEND_PRIMITIVE_PUT) {
  gab_value *ks = READ_SENDCONSTANTS;
  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  gab_value c = PEEK_N(have);

  MICRO_OP_PUT(c);
}

CASE_CODE(SEND_PRIMITIVE_FIBER) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  // TODO @cgab @bug: Breaks when yielding.
  NILPAD_GUARD_ARGS_GTE(2);

  gab_value block = PEEK_N(have - 1);

  PANIC_GUARD_KIND(block, kGAB_BLOCK);

  MICRO_OP_FIBER(block, have - 2);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_CHANNEL) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  DROP_N(have + FRAME_SIZE);

  gab_value chan = MICRO_OP_CHANNEL();

  PUSH(chan);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_RECORD) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  if (__gab_unlikely(len & 1))
    PUSH(MICRO_OP_NIL()), len++, have++;

  gab_value record = MICRO_OP_RECORD(len);

  DROP_N(have + FRAME_SIZE);

  PUSH(record);

  SET_HV(below_have + 1);

  NEXT();
}

// This should handle large shapes.
// but eeew! I don't want to malloc here if I can avoid it.

CASE_CODE(SEND_PRIMITIVE_MAKE_SHAPE) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  gab_value shape = PEEK_N(have);

  gab_value record = MICRO_OP_RECORDFROM(shape, have - 1);

  DROP_N(have + FRAME_SIZE);

  PUSH(record);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_SHAPE) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  gab_value shape = MICRO_OP_SHAPE(len);

  DROP_N(have + FRAME_SIZE);

  PUSH(shape);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_LIST) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  gab_value rec = MICRO_OP_LIST(0, len);

  DROP_N(have + FRAME_SIZE);

  PUSH(rec);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_STRING) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  gab_value str = MICRO_OP_STRING(0, len);

  DROP_N(have + FRAME_SIZE);

  PUSH(str);

  SET_HV(below_have + 1);

  NEXT();
}

CASE_CODE(SEND_PRIMITIVE_BINARY) {
  gab_value *ks = READ_SENDCONSTANTS;

  uint64_t have = HV();
  uint64_t below_have = BELOW_HV();

  SEND_GUARD_CACHED_RECEIVER_TYPE(PEEK_N(have));

  uint64_t len = have - 1;

  gab_value str = MICRO_OP_BINARY(0, len);

  DROP_N(have + FRAME_SIZE);

  PUSH(str);

  SET_HV(below_have + 1);

  NEXT();
}
