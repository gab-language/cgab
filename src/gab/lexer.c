#include "include/lexer.h"
#include "include/char.h"
#include "include/engine.h"
#include "include/types.h"
#include <stdio.h>

static void advance(gab_lexer *self) {
  self->cursor++;
  self->col++;
  self->current_token_src.len++;
  self->current_row_src.len++;
}

static void start_row(gab_lexer *self) {
  self->current_row_src.data = self->cursor;
  self->current_row_src.len = 0;
  self->col = 0;
  self->row++;
  self->previous_row = self->current_row;
}

static void start_token(gab_lexer *self) {
  self->current_token_src.data = self->cursor;
  self->current_token_src.len = 0;
}

static void finish_row(gab_lexer *self) {
  self->skip_lines++;

  self->previous_row_src = self->current_row_src;
  // Skip the newline at the end of the row.
  self->previous_row_src.len -=
      self->previous_row_src.data[self->previous_row_src.len - 1] == '\n';

  v_s_i8_push(&self->source->source_lines, self->previous_row_src);
  start_row(self);
}

void gab_lexer_create(gab_lexer *self, gab_source *src) {
  memset(self, 0, sizeof(gab_lexer));

  self->source = src;
  self->cursor = src->source->data;
  self->row_start = src->source->data;
  self->current_row = 1;

  start_row(self);
}

static inline i8 peek(gab_lexer *self) { return *self->cursor; }

static inline i8 peek_next(gab_lexer *self) { return *(self->cursor + 1); }

static inline gab_token error(gab_lexer *self, gab_status s) {
  self->status = s;
  return TOKEN_ERROR;
}

typedef struct keyword {
  const char *literal;
  gab_token token;
} keyword;

const keyword keywords[] = {
    {
        "and",
        TOKEN_AND,
    },
    {

        "do",
        TOKEN_DO,
    },
    {

        "def",
        TOKEN_DEF,
    },
    {

        "else",
        TOKEN_ELSE,
    },
    {
        "end",
        TOKEN_END,
    },
    {
        "false",
        TOKEN_FALSE,
    },
    {
        "match",
        TOKEN_MATCH,
    },
    {
        "for",
        TOKEN_FOR,
    },
    {
        "then",
        TOKEN_THEN,
    },
    {
        "in",
        TOKEN_IN,
    },
    {
        "is",
        TOKEN_IS,
    },
    {
        "not",
        TOKEN_NOT,
    },
    {
        "or",
        TOKEN_OR,
    },
    {
        "return",
        TOKEN_RETURN,
    },
    {
        "yield",
        TOKEN_YIELD,
    },
    {
        "true",
        TOKEN_TRUE,
    },
    {
        "nil",
        TOKEN_NIL,
    },
    {
        "loop",
        TOKEN_LOOP,
    },
    {
        "until",
        TOKEN_UNTIL,
    },
    {
        "impl",
        TOKEN_IMPL,
    },
};

gab_token identifier(gab_lexer *self) {

  while (is_alpha(peek(self)) || is_digit(peek(self))) {
    advance(self);
  }

  for (i32 i = 0; i < sizeof(keywords) / sizeof(keyword); i++) {
    keyword k = keywords[i];
    s_i8 lit = s_i8_create((i8 *)k.literal, strlen(k.literal));
    if (s_i8_match(self->current_token_src, lit)) {
      return k.token;
    }
  }

  return TOKEN_IDENTIFIER;
}

gab_token string(gab_lexer *self) {
  u8 start = peek(self);
  u8 stop = start == '"' ? '"' : '\'';

  do {
    advance(self);

    if (peek(self) == '\0') {
      return error(self, GAB_MALFORMED_STRING);
    }

    if (start != '"') {
      if (peek(self) == '\n') {
        return error(self, GAB_MALFORMED_STRING);
      }

      if (peek(self) == '{') {
        advance(self);
        self->nested_curly++;
        return TOKEN_INTERPOLATION;
      }
    }
  } while (peek(self) != stop);

  // Eat the end
  advance(self);

  return start == '}' ? TOKEN_INTERPOLATION_END : TOKEN_STRING;
}

gab_token integer(gab_lexer *self) {
  if (!is_digit(peek(self)))
    return error(self, GAB_MALFORMED_TOKEN);

  while (is_digit(peek(self)))
    advance(self);

  return TOKEN_NUMBER;
}

gab_token floating(gab_lexer *self) {

  if (integer(self) == TOKEN_ERROR)
    return TOKEN_ERROR;

  if (peek(self) == '.' && is_digit(peek_next(self))) {
    advance(self);

    while (is_digit(peek(self))) {
      advance(self);
    }
  }

  return TOKEN_NUMBER;
}

#define CHAR_CASE(char, name)                                                  \
  case char: {                                                                 \
    advance(self);                                                             \
    return TOKEN_##name;                                                       \
  }

gab_token other(gab_lexer *self) {
  switch (peek(self)) {

    CHAR_CASE('+', PLUS)
    CHAR_CASE('*', STAR)
    CHAR_CASE('/', SLASH)
    CHAR_CASE('%', PERCENT)
    CHAR_CASE('[', LBRACE)
    CHAR_CASE(']', RBRACE)
    CHAR_CASE('(', LPAREN)
    CHAR_CASE(')', RPAREN)
    CHAR_CASE(',', COMMA)
    CHAR_CASE(';', SEMICOLON)
    CHAR_CASE('|', PIPE)
    CHAR_CASE('?', QUESTION)
    CHAR_CASE('&', AMPERSAND)
    CHAR_CASE('!', BANG)

  case '{': {
    advance(self);
    if (self->nested_curly > 0)
      self->nested_curly++;
    return TOKEN_LBRACK;
  }

  case '}': {
    advance(self);
    if (self->nested_curly > 0)
      self->nested_curly--;
    return TOKEN_RBRACK;
  }

  case '@': {
    advance(self);

    if (is_digit(peek(self))) {
      if (integer(self) == TOKEN_NUMBER)
        return TOKEN_IMPLICIT;

      return error(self, GAB_MALFORMED_TOKEN);
    }

    return TOKEN_AT;
  }

  case '$': {
    advance(self);

    if (is_alpha(peek(self))) {
      // If we didn't get a keyword, return a token message
      if (identifier(self) == TOKEN_IDENTIFIER)
        return TOKEN_SYMBOL;

      // Otherwise, we got a keyword and this was an error
      return error(self, GAB_MALFORMED_TOKEN);
    }

    return TOKEN_DOLLAR;
  }
  case '.': {
    advance(self);

    if (is_alpha(peek(self))) {
      // If we didn't get a keyword, return a token message
      if (identifier(self) == TOKEN_IDENTIFIER)
        return TOKEN_PROPERTY;

      // Otherwise, we got a keyword and this was an error

      return error(self, GAB_MALFORMED_TOKEN);
    }

    switch (peek(self)) {
      CHAR_CASE('.', DOT_DOT);
    default: {
      return TOKEN_DOT;
    }
    }
  }
  case '-': {
    advance(self);
    switch (peek(self)) {
      CHAR_CASE('>', ARROW)
    default: {
      return TOKEN_MINUS;
    }
    }
  }
  case ':': {
    advance(self);

    if (is_alpha(peek(self))) {
      // If we didn't get a keyword, return a token message
      if (identifier(self) == TOKEN_IDENTIFIER) {
        // Messages can end in ? or !
        if (peek(self) == '?' || peek(self) == '!') {
          advance(self);
        }

        return TOKEN_MESSAGE;
      }

      // Otherwise, we got a keyword and this was an error

      return error(self, GAB_MALFORMED_TOKEN);
    }

    switch (peek(self)) {
      CHAR_CASE('=', COLON_EQUAL)
    default: {
      return TOKEN_COLON;
    }
    }
  }
  case '=': {
    advance(self);
    switch (peek(self)) {
      CHAR_CASE('=', EQUAL_EQUAL)
      CHAR_CASE('>', FAT_ARROW)
    default: {
      return TOKEN_EQUAL;
    }
    }
  }
  case '<': {
    advance(self);
    switch (peek(self)) {
      CHAR_CASE('=', LESSER_EQUAL)
      CHAR_CASE('<', LESSER_LESSER)
    default: {
      return TOKEN_LESSER;
    }
    }
  }
  case '>': {
    advance(self);
    switch (peek(self)) {
      CHAR_CASE('=', GREATER_EQUAL)
      CHAR_CASE('>', GREATER_GREATER)
    default: {
      return TOKEN_GREATER;
    }
    }
  }
  default: {
    advance(self);
    return error(self, GAB_MALFORMED_TOKEN);
  }
  }
}

static inline void parse_comment(gab_lexer *self) {
  i8 *start = self->cursor;

  while (is_comment(peek(self))) {
    while (peek(self) != '\n')
      advance(self);

    advance(self);
  }

  self->previous_comment = s_i8_create(start, self->cursor - start);
}

static inline void parse_whitespace(gab_lexer *self) { advance(self); }

gab_token gab_lexer_next(gab_lexer *self) {
  self->previous_token_src = self->current_token_src;
  self->previous_token = self->current_token;
  self->previous_comment = (s_i8){0};

  while (is_whitespace(peek(self)) || is_comment(peek(self))) {
    if (is_comment(peek(self)))
      parse_comment(self);

    if (is_whitespace(peek(self)))
      parse_whitespace(self);
  }

  start_token(self);

  if (peek(self) == '\0' || peek(self) == EOF) {
    self->current_token_src = s_i8_create(self->cursor, 0);
    finish_row(self);
    return TOKEN_EOF;
  }

  if (peek(self) == '\n') {
    advance(self);
    finish_row(self);
    return TOKEN_NEWLINE;
  }

  self->current_row += self->skip_lines;
  self->skip_lines = 0;

  if (is_alpha(peek(self))) {
    return identifier(self);
  }

  if (is_digit(peek(self))) {
    return floating(self);
  }

  if (peek(self) == '"') {
    return string(self);
  }

  if (peek(self) == '\'') {
    return string(self);
  }

  if (self->nested_curly == 1 && peek(self) == '}') {
    self->nested_curly--;
    return string(self);
  }

  return other(self);
}

void gab_lexer_finish_line(gab_lexer *self) {
  while (peek(self) != '\n' && peek(self) != '\0') {
    advance(self);
  }

  advance(self);
  finish_row(self);

  self->current_row += self->skip_lines;
  self->skip_lines = 0;
};

gab_source *gab_source_create(gab_engine *gab, s_i8 source) {
  gab_source *self = NEW(gab_source);
  self->source = a_i8_create(source.data, source.len);
  v_s_i8_create(&self->source_lines, 32);

  self->next = gab->sources;
  gab->sources = self;

  return self;
}

gab_source *gab_source_copy(gab_engine *gab, gab_source *self) {
  gab_source *copy = NEW(gab_source);
  copy->source = a_i8_create(self->source->data, self->source->len);

  v_s_i8_copy(&copy->source_lines, &self->source_lines);

  // Reconcile the copied slices to point to the new source
  for (u64 i = 0; i < copy->source_lines.len; i++) {
    s_i8 *copy_src = v_s_i8_ref_at(&copy->source_lines, i);
    s_i8 *src_src = v_s_i8_ref_at(&self->source_lines, i);

    copy_src->data = copy->source->data + (src_src->data - self->source->data);
  }

  copy->next = gab->sources;
  gab->sources = copy;

  return copy;
}

void gab_source_destroy(gab_source *self) {
  v_s_i8_destroy(&self->source_lines);
  a_i8_destroy(self->source);
  DESTROY(self);
}

#undef CURSOR
#undef NEXT_CURSOR
#undef ADVANCE
