#include "../vendor/libgrapheme/grapheme.h"
#include "gab.h"
#include <ctype.h>

static inline bool instr(char c, const char *set) {
  while (*set != '\0')
    if (c == *set++)
      return true;

  return false;
}

GAB_DYNLIB_NATIVE_FN(string, seq_init) {
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

GAB_DYNLIB_NATIVE_FN(string, seq_next) {
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

GAB_DYNLIB_NATIVE_FN(string, trim) {
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

GAB_DYNLIB_NATIVE_FN(string, split) {
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

GAB_DYNLIB_NATIVE_FN(binary, len) {
  gab_value result = gab_number(gab_strlen(argv[0]));

  gab_vmpush(gab_thisvm(gab), result);
  return gab_union_cvalid(gab_nil);
};

GAB_DYNLIB_NATIVE_FN(binary, tos) {
  gab_value bin = gab_arg(0);

  gab_value str = gab_bintostr(bin);

  if (str == gab_cvalid)
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Binary is not valid UTF-8"));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, str);

  return gab_union_cvalid(gab_nil);
};

GAB_DYNLIB_NATIVE_FN(string, len) {
  if (argc != 1) {
    return gab_panicf(gab, "&:len expects 1 argument");
  }

  gab_value result = gab_number(gab_strmblen(argv[0]));

  gab_vmpush(gab_thisvm(gab), result);
  return gab_union_cvalid(gab_nil);
};

GAB_DYNLIB_NATIVE_FN(string, make) {
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

GAB_DYNLIB_NATIVE_FN(string, blank) {
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

GAB_DYNLIB_NATIVE_FN(string, ends) {
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

GAB_DYNLIB_NATIVE_FN(string, begins) {
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

GAB_DYNLIB_NATIVE_FN(binary, at) {
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


GAB_DYNLIB_NATIVE_FN(string, at){
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

GAB_DYNLIB_NATIVE_FN(string, slice){
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

GAB_DYNLIB_NATIVE_FN(string, has) {
  if (argc < 2) {
    return gab_panicf(gab, "&:has? expects one argument");
  }

  const char *str = gab_strdata(argv + 0);
  const char *pat = gab_strdata(argv + 1);

  gab_vmpush(gab_thisvm(gab), gab_bool(strstr(str, pat)));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(string, tos) {
  if (gab_arg(1) == gab_message(gab, "plain"))
    gab_vmpush(gab_thisvm(gab), gab_valintos(gab, gab_arg(0)));
  else
    gab_vmpush(gab_thisvm(gab), gab_pvalintos(gab, gab_arg(0)));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(string, tob) {
  gab_vmpush(gab_thisvm(gab), gab_strtobin(gab_arg(0)));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(message, tob) {
  gab_vmpush(gab_thisvm(gab), gab_strtobin(gab_msgtostr(gab_arg(0))));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(number, tob) {
  uint64_t f = gab_valtou(gab_arg(0));
  gab_vmpush(gab_thisvm(gab), gab_nbinary(gab, sizeof(f), (void *)&f));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(string, tom) {
  gab_vmpush(gab_thisvm(gab), gab_strtomsg(gab_arg(0)));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(string, ton) {
  const char *str = gab_strdata(argv + 0);

  gab_value res = gab_number(strtod(str, nullptr));

  gab_vmpush(gab_thisvm(gab), res);
  return gab_union_cvalid(gab_nil);
};

GAB_DYNLIB_NATIVE_FN(string, pop) {
  const char *str = gab_strdata(argv + 0);
  uint64_t len = gab_strlen(gab_arg(0));

  if (len == 0) {
    gab_vmpush(gab_thisvm(gab), gab_string(gab, ""));
    return gab_union_cvalid(gab_nil);
  }

  char ch = str[len - 1];
  gab_value strchar = gab_nstring(gab, 1, &ch);
  gab_value leftover = gab_nstring(gab, len - 1, str);

  // TODO: Fix this to respect unicode
  // ie: Can't assume that the *last byte* of the str
  // is a valid char.
  gab_vmpush(gab_thisvm(gab), leftover, strchar);
  return gab_union_cvalid(gab_nil);
};

GAB_DYNLIB_NATIVE_FN(fmt, panicf) {
  gab_value fmtstr = gab_arg(0);
  const char *fmt = gab_strdata(&fmtstr);

  char buf[10000];
  int len = gab_nsprintf(buf, sizeof(buf), fmt, argc - 1, argv + 1);

  if (len < 0)
    return gab_panicf(gab, "sprintf buffer too small", gab_number(argc - 1));

  return gab_panicf(gab, "$", gab_string(gab, buf));
}

GAB_DYNLIB_NATIVE_FN(fmt, sprintf) {
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
              gab_snative(gab, "is\\blank", gab_mod_string_blank),
          },
          {
              gab_message(gab, "split"),
              t,
              gab_snative(gab, "split", gab_mod_string_split),
          },
          {
              gab_message(gab, "has\\sub"),
              t,
              gab_snative(gab, "has\\sub", gab_mod_string_has),
          },
          {
              gab_message(gab, "has\\ending"),
              t,
              gab_snative(gab, "has\\ending", gab_mod_string_ends),
          },
          {
              gab_message(gab, "has\\beginning"),
              t,
              gab_snative(gab, "has\\beginning", gab_mod_string_begins),
          },
          {
              gab_message(gab, "seq\\init"),
              t,
              gab_snative(gab, "seq\\init", gab_mod_string_seq_init),
          },
          {
              gab_message(gab, "seq\\next"),
              t,
              gab_snative(gab, "seq\\next", gab_mod_string_seq_next),
          },
          {
              gab_message(gab, "to\\s"),
              gab_cundefined,
              gab_snative(gab, "to\\s", gab_mod_string_tos),
          },
          {
              gab_message(gab, "to\\m"),
              t,
              gab_snative(gab, "to\\m", gab_mod_string_tom),
          },
          {
              gab_message(gab, "to\\b"),
              t,
              gab_snative(gab, "to\\b", gab_mod_string_tob),
          },
          {
              gab_message(gab, "as\\n"),
              t,
              gab_snative(gab, "as\\n", gab_mod_string_ton),
          },
          {
              gab_message(gab, "as\\s"),
              gab_type(gab, kGAB_BINARY),
              gab_snative(gab, "as\\s", gab_mod_binary_tos),
          },
          {
              gab_message(gab, "len"),
              gab_type(gab, kGAB_BINARY),
              gab_snative(gab, "len", gab_mod_binary_len),
          },
          {
              gab_message(gab, "at"),
              gab_type(gab, kGAB_BINARY),
              gab_snative(gab, "at", gab_mod_binary_at),
          },
          {
              gab_message(gab, "t"),
              gab_strtomsg(gab_type(gab, kGAB_BINARY)),
              gab_type(gab, kGAB_BINARY),
          },
          {
              gab_message(gab, "len"),
              t,
              gab_snative(gab, "len", gab_mod_string_len),
          },
          {
              gab_message(gab, "at"),
              t,
              gab_snative(gab, "at", gab_mod_string_at),
          },
          {
              gab_message(gab, "slice"),
              t,
              gab_snative(gab, "slice", gab_mod_string_slice),
          },
          {
              gab_message(gab, "make"),
              gab_strtomsg(t),
              gab_snative(gab, "make", gab_mod_string_make),
          },
          {
              gab_message(gab, "sprintf"),
              t,
              gab_snative(gab, "sprintf", gab_mod_fmt_sprintf),
          },
          {
              gab_message(gab, "panicf"),
              t,
              gab_snative(gab, "panicf", gab_mod_fmt_panicf),
          },
          {
              gab_message(gab, "trim"),
              t,
              gab_snative(gab, "trim", gab_mod_string_trim),
          },
          {
              gab_message(gab, "pop"),
              t,
              gab_snative(gab, "pop", gab_mod_string_pop),
          }
          );

  gab_value res[] = {gab_ok, gab_strtomsg(t)};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
