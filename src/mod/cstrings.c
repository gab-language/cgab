#include "../vendor/libgrapheme/grapheme.h"
#include "core.h"
#include "gab.h"
#include <ctype.h>
#include <stdint.h>

static inline bool instr(char c, const char *set) {
  while (*set != '\0')
    if (c == *set++)
      return true;

  return false;
}

union gab_value_pair gab_strlib_seqinit(struct gab_triple gab, uint64_t argc,
                                        gab_value argv[argc]) {
  gab_value str = gab_arg(0);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  uint64_t len = gab_strmblen(str);

  if (len == 0) {
    gab_vmpush(gab_thisvm(gab), gab_none);
    return gab_union_cvalid(gab_nil);
  }

  const char *data = gab_strdata(&str);

  size_t end = grapheme_next_character_break_utf8(data, SIZE_MAX);

  gab_value grapheme = gab_nstring(gab, end, data);
  gab_vmpush(gab_thisvm(gab), gab_ok, 0, grapheme, 0);

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_seqnext(struct gab_triple gab, uint64_t argc,
                                        gab_value argv[argc]) {
  gab_value str = gab_arg(0);
  gab_value old = gab_arg(1);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  if (gab_valkind(old) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, old, kGAB_NUMBER);

  uint64_t old_off = gab_valtou(old);
  uint64_t len = gab_strlen(str);

  if (len <= old_off) {
    gab_vmpush(gab_thisvm(gab), gab_none);
    return gab_union_cvalid(gab_nil);
  }

  const char *data = gab_strdata(&str);

  assert(old_off < len);
  data += old_off;

  size_t old_bytes = grapheme_next_character_break_utf8(data, SIZE_MAX);

  if (len <= old_off + old_bytes) {
    gab_vmpush(gab_thisvm(gab), gab_none);
    return gab_union_cvalid(gab_nil);
  }

  size_t new_bytes =
      grapheme_next_character_break_utf8(data + old_bytes, SIZE_MAX);

  gab_value new_off = gab_number(old_off + old_bytes);

  gab_value grapheme = gab_nstring(gab, new_bytes, data + old_bytes);

  gab_vmpush(gab_thisvm(gab), gab_ok, new_off, grapheme, new_off);

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_trim(struct gab_triple gab, uint64_t argc,
                                     gab_value argv[argc]) {
  gab_value str = gab_arg(0);
  gab_value trimset = gab_arg(1);

  const char *cstr = gab_strdata(&str);
  const char *ctrimset = nullptr;
  uint64_t cstrlen = gab_strlen(str);

  if (trimset == gab_nil)
    trimset = gab_string(gab, "\n\t ");

  if (gab_valkind(trimset) != kGAB_STRING)
    return gab_pktypemismatch(gab, trimset, kGAB_STRING);

  if (cstrlen == 0) {
    gab_vmpush(gab_thisvm(gab), str);
    return gab_union_cvalid(gab_nil);
  }

  ctrimset = gab_strdata(&trimset);

  const char *front = cstr;
  const char *back = cstr + cstrlen - 1;

  while (instr(*front, ctrimset) && front < back)
    front++;

  while (instr(*back, ctrimset) && back > front)
    back--;

  uint64_t result_len = back - front + 1;

  gab_vmpush(gab_thisvm(gab), gab_nstring(gab, result_len, front));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_split(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_value str = gab_arg(0);
  gab_value sep = gab_arg(1);

  if (gab_valkind(sep) != kGAB_STRING)
    return gab_pktypemismatch(gab, sep, kGAB_STRING);

  uint64_t cstr_len = gab_strlen(str);
  uint64_t csep_len = gab_strlen(sep);

  if (cstr_len == 0 || csep_len == 0)
    return gab_union_cvalid(gab_nil);

  const char *cstr = gab_strdata(&str);
  const char *csep = gab_strdata(&sep);
  const char sepstart = csep[0];

  uint64_t offset = 0, begin = 0;
  while (offset + csep_len <= cstr_len) {

    // Quick check to see if we should try memcmp
    if (cstr[offset] == sepstart) {
      // Memcmp to test for full sep match
      if (!memcmp(cstr + offset, csep, csep_len)) {
        // Full match found - push a string
        gab_vmpush(gab_thisvm(gab),
                   gab_nstring(gab, offset - begin, cstr + begin));
        begin = offset + csep_len;
        offset = begin;
        continue;
      }
    }

    offset++;
  }

  gab_vmpush(gab_thisvm(gab), gab_nstring(gab, cstr_len - begin, cstr + begin));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_binlib_len(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  gab_value result = gab_number(gab_strlen(argv[0]));

  gab_vmpush(gab_thisvm(gab), result);
  return gab_union_cvalid(gab_nil);
};

union gab_value_pair gab_binlib_strings_into(struct gab_triple gab,
                                             uint64_t argc,
                                             gab_value argv[argc]) {
  gab_value bin = gab_arg(0);

  gab_value str = gab_bintostr(bin);

  if (str == gab_cvalid)
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Binary is not valid UTF-8"));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, str);

  return gab_union_cvalid(gab_nil);
};

union gab_value_pair gab_strlib_len(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  if (argc != 1) {
    return gab_panicf(gab, "&:len expects 1 argument");
  }

  gab_value result = gab_number(gab_strmblen(argv[0]));

  gab_vmpush(gab_thisvm(gab), result);
  return gab_union_cvalid(gab_nil);
};

union gab_value_pair gab_strlib_make(struct gab_triple gab, uint64_t argc,
                                     gab_value argv[argc]) {
  if (argc <= 1)
    return gab_vmpush(gab_thisvm(gab), gab_string(gab, "")),
           gab_union_cvalid(gab_nil);

  gab_value str = gab_valintos(gab, gab_arg(1));

  for (size_t i = 2; i < argc; i++) {
    str = gab_strcat(gab, str, gab_valintos(gab, gab_arg(i)));
  }

  return gab_vmpush(gab_thisvm(gab), str), gab_union_cvalid(gab_nil);
}

static inline bool begins(const char *str, const char *pat, uint64_t offset) {
  uint64_t len = strlen(pat);

  if (strlen(str) < offset + len)
    return false;

  return !memcmp(str + offset, pat, len);
}

static inline bool ends(const char *str, const char *pat, uint64_t offset) {
  uint64_t len = strlen(pat);

  if (strlen(str) < offset + len)
    return false;

  return !memcmp(str + strlen(str) - offset - len, pat, len);
}

union gab_value_pair gab_strlib_blank(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_value str = gab_arg(0);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *cstr = gab_strdata(&str);

  while (*cstr) {
    if (!isspace(*cstr)) {
      gab_vmpush(gab_thisvm(gab), gab_false);
      return gab_union_cvalid(gab_nil);
    }

    cstr++;
  }

  gab_vmpush(gab_thisvm(gab), gab_true);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_ends(struct gab_triple gab, uint64_t argc,
                                     gab_value argv[argc]) {
  switch (argc) {
  case 2: {
    if (gab_valkind(argv[1]) != kGAB_STRING) {
      return gab_panicf(gab, "&:ends? expects 1 string argument");
    }

    const char *str = gab_strdata(argv + 0);
    const char *pat = gab_strdata(argv + 1);

    gab_vmpush(gab_thisvm(gab), gab_bool(ends(str, pat, 0)));
    return gab_union_cvalid(gab_nil);
  }

  case 3: {
    if (gab_valkind(argv[1]) != kGAB_STRING) {
      return gab_panicf(gab, "&:ends? expects 1 string argument");
    }

    if (gab_valkind(argv[2]) != kGAB_NUMBER) {
      return gab_panicf(gab, "&:ends? expects an optinal number argument");
    }
    const char *pat = gab_strdata(argv + 0);
    const char *str = gab_strdata(argv + 1);

    gab_vmpush(gab_thisvm(gab), gab_bool(ends(str, pat, gab_valtou(argv[2]))));
    return gab_union_cvalid(gab_nil);
  }
  }

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_begins(struct gab_triple gab, uint64_t argc,
                                       gab_value argv[argc]) {
  gab_value vstr = gab_arg(0);
  gab_value vpat = gab_arg(1);
  switch (argc) {
  case 2: {
    if (gab_valkind(argv[1]) != kGAB_STRING) {
      return gab_panicf(gab, "&:begins? expects 1 string argument");
    }

    const char *pat = gab_strdata(&vpat);
    const char *str = gab_strdata(&vstr);

    gab_vmpush(gab_thisvm(gab), gab_bool(begins(str, pat, 0)));
    return gab_union_cvalid(gab_nil);
  }
  case 3: {
    if (gab_valkind(argv[1]) != kGAB_STRING) {
      return gab_panicf(gab, "&:begins? expects 1 string argument");
    }

    if (gab_valkind(argv[2]) != kGAB_NUMBER) {
      return gab_panicf(gab, "&:begins? expects an optinal number argument");
    }
    const char *pat = gab_strdata(&vpat);
    const char *str = gab_strdata(&vstr);

    gab_vmpush(gab_thisvm(gab),
               gab_bool(begins(str, pat, gab_valtou(argv[2]))));
    return gab_union_cvalid(gab_nil);
  }
  }
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_binlib_at(struct gab_triple gab, uint64_t argc,
                                   gab_value argv[argc]) {
  gab_value bin = gab_arg(0);
  gab_value idx = gab_arg(1);
  gab_value step = gab_arg(2);

  if (gab_valkind(bin) != kGAB_BINARY)
    return gab_pktypemismatch(gab, bin, kGAB_BINARY);

  if (gab_valkind(idx) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, idx, kGAB_NUMBER);

  if (step == gab_nil)
    step = gab_number(1);

  if (gab_valkind(step) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, step, kGAB_NUMBER);

  int64_t index = gab_valtoi(idx);
  uint64_t stp = gab_valtou(step);

  if (gab_valkind(step) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, step, kGAB_NUMBER);

  if (stp > 8)
    return gab_panicf(gab, "Step size cannot exceed 8 bytes: got $", step);

  size_t len = gab_strlen(bin);

  // Go from the back
  if (index < 0)
    index += (len / stp);

  size_t offset = index * stp;

  if (offset + stp > len)
    return gab_vmpush(gab_thisvm(gab), gab_none), gab_union_cvalid(gab_nil);

  const char *begin = gab_strdata(&bin) + (index * stp);

  int64_t result = 0;

  // Cast the char to unsigned - can't shift negative values.
  for (size_t i = 0; i < stp; i++)
    result |= ((unsigned char)begin[i]) << i;

  gab_vmpush(gab_thisvm(gab), gab_ok, gab_number(result));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_at(struct gab_triple gab, uint64_t argc,
                                   gab_value argv[argc]) {
  if (argc != 2 && gab_valkind(argv[1]) != kGAB_NUMBER) {
    return gab_panicf(gab, "&:at expects 1 number argument");
  }

  int64_t index = gab_valtoi(argv[1]);

  if (index > gab_strlen(argv[0])) {
    return gab_panicf(gab, "Index out of bounds");
  }

  if (index < 0) {
    // Go from the back
    index = gab_strlen(argv[0]) + index;
  }

  char byte = gab_strdata(argv + 0)[index];

  gab_vmpush(gab_thisvm(gab), gab_nstring(gab, 1, &byte));
  return gab_union_cvalid(gab_nil);
}

s_char utf8_slice(const char *data, size_t len, size_t from, size_t to) {
  size_t graphemes = 0, offset = 0;

  while (graphemes < from) {
    graphemes++;

    assert(offset < len);
    offset += grapheme_next_character_break_utf8(data + offset, SIZE_MAX);
  }

  s_char result = s_char_create(data + offset, 0);
  while (graphemes < to) {
    graphemes++;

    assert(offset < len);
    size_t grapheme_size =
        grapheme_next_character_break_utf8(data + offset, SIZE_MAX);

    offset += grapheme_size;

    result.len += grapheme_size;
  }

  return result;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(a, b) (MAX(0, MIN(a, b)))

union gab_value_pair gab_strlib_slice(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_value str = gab_arg(0);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *data = gab_strdata(&str);

  uint64_t len = gab_strlen(argv[0]);
  if (len == 0) {
    gab_vmpush(gab_thisvm(gab), gab_string(gab, ""));
    return gab_union_cvalid(gab_nil);
  }

  uint64_t start = 0, end = len;

  switch (argc) {
  case 2: {
    if (gab_valkind(gab_arg(1)) != kGAB_NUMBER)
      return gab_pktypemismatch(gab, gab_arg(1), kGAB_NUMBER);

    int64_t a = gab_valtoi(gab_arg(1));
    if (a < 0)
      a += len;

    end = CLAMP(a, len);
    assert(end >= 0 && end < len);
    break;
  }

  default: {
    if (gab_valkind(gab_arg(1)) != kGAB_NUMBER)
      return gab_pktypemismatch(gab, gab_arg(1), kGAB_NUMBER);

    int64_t a = gab_valtoi(gab_arg(1));
    if (a < 0)
      a += len;

    start = CLAMP(a, len);
    assert(start >= 0 && start <= len);

    if (gab_valkind(gab_arg(2)) == kGAB_NUMBER) {
      int64_t b = gab_valtoi(gab_arg(2));
      if (b < 0)
        b += len;

      end = CLAMP(b, len);
      assert(end >= 0 && end <= len);
    }
    break;
  }
  }

  if (start > end)
    return gab_panicf(
        gab, "slice: expects the start to be before the end, got [$, $]", start,
        end);

  s_char result = utf8_slice(data, gab_strlen(str), start, end);

  gab_value res = gab_nstring(gab, result.len, result.data);

  gab_vmpush(gab_thisvm(gab), res);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_has(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  if (argc < 2) {
    return gab_panicf(gab, "&:has? expects one argument");
  }

  const char *str = gab_strdata(argv + 0);
  const char *pat = gab_strdata(argv + 1);

  gab_vmpush(gab_thisvm(gab), gab_bool(strstr(str, pat)));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_string_into(struct gab_triple gab,
                                            uint64_t argc,
                                            gab_value argv[argc]) {
  gab_vmpush(gab_thisvm(gab), gab_valintos(gab, gab_arg(0)));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_binary_into(struct gab_triple gab,
                                            uint64_t argc,
                                            gab_value argv[argc]) {
  gab_vmpush(gab_thisvm(gab), gab_strtobin(gab_arg(0)));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_msglib_binary_into(struct gab_triple gab,
                                            uint64_t argc,
                                            gab_value argv[argc]) {
  gab_vmpush(gab_thisvm(gab), gab_strtobin(gab_msgtostr(gab_arg(0))));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_numlib_binary_into(struct gab_triple gab,
                                            uint64_t argc,
                                            gab_value argv[argc]) {
  uint64_t f = gab_valtou(gab_arg(0));
  gab_vmpush(gab_thisvm(gab), gab_nbinary(gab, sizeof(f), (void *)&f));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_messages_into(struct gab_triple gab,
                                              uint64_t argc,
                                              gab_value argv[argc]) {
  gab_vmpush(gab_thisvm(gab), gab_strtomsg(gab_arg(0)));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_new(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  if (argc < 2) {
    gab_vmpush(gab_thisvm(gab), gab_string(gab, ""));
    return gab_union_cvalid(gab_nil);
  }

  gab_value str = gab_valintos(gab, gab_arg(1));

  if (argc == 2) {
    gab_vmpush(gab_thisvm(gab), str);
    return gab_union_cvalid(gab_nil);
  }

  gab_gclock(gab);

  for (uint8_t i = 2; i < argc; i++) {
    gab_value curr = gab_valintos(gab, gab_arg(i));
    str = gab_strcat(gab, str, curr);
  }

  gab_vmpush(gab_thisvm(gab), str);
  gab_gcunlock(gab);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_strlib_numbers_into(struct gab_triple gab,
                                             uint64_t argc,
                                             gab_value argv[argc]) {
  const char *str = gab_strdata(argv + 0);

  gab_value res = gab_number(strtod(str, nullptr));

  gab_vmpush(gab_thisvm(gab), res);
  return gab_union_cvalid(gab_nil);
};

union gab_value_pair gab_fmtlib_panicf(struct gab_triple gab, uint64_t argc,
                                       gab_value argv[argc]) {
  gab_value fmtstr = gab_arg(0);
  const char *fmt = gab_strdata(&fmtstr);

  char buf[10000];
  int len = gab_nsprintf(buf, sizeof(buf), fmt, argc - 1, argv + 1);

  if (len < 0)
    return gab_panicf(gab, "sprintf buffer too small", gab_number(argc - 1));

  return gab_panicf(gab, "$", gab_string(gab, buf));
}

union gab_value_pair gab_fmtlib_sprintf(struct gab_triple gab, uint64_t argc,
                                        gab_value argv[argc]) {
  gab_value fmtstr = gab_arg(0);

  const char *fmt = gab_strdata(&fmtstr);

  size_t n = 2048;
  for (;; n *= 2) {
    char *buf = malloc(n);
    int len = gab_nsprintf(buf, n, fmt, argc - 1, argv + 1);

    if (len < 0) {
      free(buf);
      continue;
    }

    gab_vmpush(gab_thisvm(gab), gab_string(gab, buf));
    free(buf);
    return gab_union_cvalid(gab_nil);
  }
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_STRING);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "is\\blank"),
              t,
              gab_snative(gab, "is\\blank", gab_strlib_blank),
          },
          {
              gab_message(gab, "split"),
              t,
              gab_snative(gab, "split", gab_strlib_split),
          },
          {
              gab_message(gab, "has\\sub"),
              t,
              gab_snative(gab, "has\\sub", gab_strlib_has),
          },
          {
              gab_message(gab, "has\\ending"),
              t,
              gab_snative(gab, "has\\ending", gab_strlib_ends),
          },
          {
              gab_message(gab, "has\\beginning"),
              t,
              gab_snative(gab, "has\\beginning", gab_strlib_begins),
          },
          {
              gab_message(gab, "seq\\init"),
              t,
              gab_snative(gab, "seq\\init", gab_strlib_seqinit),
          },
          {
              gab_message(gab, "seq\\next"),
              t,
              gab_snative(gab, "seq\\next", gab_strlib_seqnext),
          },
          {
              gab_message(gab, "to\\s"),
              gab_cundefined,
              gab_snative(gab, "to\\s", gab_strlib_string_into),
          },
          {
              gab_message(gab, "to\\m"),
              t,
              gab_snative(gab, "to\\m", gab_strlib_messages_into),
          },
          {
              gab_message(gab, "to\\b"),
              t,
              gab_snative(gab, "to\\b", gab_strlib_binary_into),
          },
          {
              gab_message(gab, "as\\n"),
              t,
              gab_snative(gab, "as\\n", gab_strlib_numbers_into),
          },
          {
              gab_message(gab, "as\\s"),
              gab_type(gab, kGAB_BINARY),
              gab_snative(gab, "as\\s", gab_binlib_strings_into),
          },
          {
              gab_message(gab, "len"),
              gab_type(gab, kGAB_BINARY),
              gab_snative(gab, "len", gab_binlib_len),
          },
          {
              gab_message(gab, "at"),
              gab_type(gab, kGAB_BINARY),
              gab_snative(gab, "at", gab_binlib_at),
          },
          {
              gab_message(gab, "t"),
              gab_strtomsg(gab_type(gab, kGAB_BINARY)),
              gab_type(gab, kGAB_BINARY),
          },
          {
              gab_message(gab, "len"),
              t,
              gab_snative(gab, "len", gab_strlib_len),
          },
          {
              gab_message(gab, "at"),
              t,
              gab_snative(gab, "at", gab_strlib_at),
          },
          {
              gab_message(gab, "slice"),
              t,
              gab_snative(gab, "slice", gab_strlib_slice),
          },
          {
              gab_message(gab, "make"),
              gab_strtomsg(t),
              gab_snative(gab, "make", gab_strlib_make),
          },
          {
              gab_message(gab, "sprintf"),
              t,
              gab_snative(gab, "sprintf", gab_fmtlib_sprintf),
          },
          {
              gab_message(gab, "panicf"),
              t,
              gab_snative(gab, "panicf", gab_fmtlib_panicf),
          },
          {
              gab_message(gab, "trim"),
              t,
              gab_snative(gab, "trim", gab_strlib_trim),
          });

  gab_value res[] = {gab_ok, gab_strtomsg(t)};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
