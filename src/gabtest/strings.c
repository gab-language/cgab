#include "gab.h"
#include "munit/munit.h"

extern struct gab_triple gab;

static MunitResult test_string_creation(const MunitParameter params[],
                                        void *data) {
  gab_value s_hello = gab_string(gab, "hello");
  munit_assert_uint64(gab_valkind(s_hello), ==, kGAB_STRING);
  munit_assert_uint64(gab_strlen(s_hello), ==, 5);

  gab_value s_bound = gab_nstring(gab, 4, "long_string_buffer");
  munit_assert_uint64(gab_strlen(s_bound), ==, 4);

  munit_assert_string_equal(gab_strdata(&s_hello), "hello");
  munit_assert_string_equal(gab_strdata(&s_bound), "long");

  gab_value s_hello2 = gab_string(gab, "hello");
  munit_assert_uint64(s_hello, ==, s_hello2);

  return MUNIT_OK;
}

static MunitResult test_string_multibyte(const MunitParameter params[],
                                         void *data) {
  gab_value s_ascii = gab_string(gab, "Gab");
  munit_assert_uint64(gab_strlen(s_ascii), ==, 3);
  munit_assert_uint64(gab_strmblen(s_ascii), ==, 3);

  // Emoji test: "🔥" - 1 character, 4 bytes
  gab_value s_fire = gab_string(gab, "🚀");
  munit_assert_uint64(gab_strlen(s_fire), ==, 4);
  munit_assert_uint64(gab_strmblen(s_fire), ==, 1);

  // "こんにちは" (Konnichiwa) - 5 characters, 15 bytes in UTF-8
  gab_value s_jp = gab_string(gab, "こんにちは");
  munit_assert_uint64(gab_strmblen(s_jp), !=, -1);

  munit_assert_uint64(gab_strlen(s_jp), ==, 15);
  munit_assert_uint64(gab_strmblen(s_jp), ==, 5);

  return MUNIT_OK;
}

static MunitResult test_string_concat(const MunitParameter params[],
                                      void *data) {
  gab_value a = gab_string(gab, "foo");
  gab_value b = gab_string(gab, "bar");

  gab_value c = gab_strcat(gab, a, b);
  munit_assert_string_equal(gab_strdata(&c), "foobar");
  munit_assert_uint64(gab_strlen(c), ==, 6);

  gab_value d = gab_sstrcat(gab, c, "baz");
  munit_assert_string_equal(gab_strdata(&d), "foobarbaz");
  munit_assert_uint64(gab_strlen(d), ==, 9);

  gab_value parts[] = {
      gab_string(gab, "The "), gab_string(gab, "answer "),
      gab_string(gab, "is "),
      gab_number(42) // Should automatically coerce to string via gab_nvstring's
                     // internal logic
  };

  // Array-based concatenation
  gab_value result1 = gab_nvstring(gab, 4, parts);
  munit_assert_string_equal(gab_strdata(&result1), "The answer is 42");

  // Macro-based convenience wrapper
  gab_value result2 =
      gab_stringof(gab, gab_string(gab, "Error: "), gab_number(404));
  munit_assert_string_equal(gab_strdata(&result2), "Error: 404");

  return MUNIT_OK;
}

static MunitResult test_string_timeouts(const MunitParameter params[],
                                        void *data) {
  // tnstring (tries/timeout variant)
  gab_value s = gab_tnstring(gab, 4, "safe_string");
  munit_assert_uint64(gab_strlen(s), ==, 4);
  munit_assert_string_equal(gab_strdata(&s), "safe");

  // tstrcat
  gab_value appended = gab_tstrcat(gab, s, gab_string(gab, "_mode"));
  munit_assert_string_equal(gab_strdata(&appended), "safe_mode");

  return MUNIT_OK;
}

static MunitResult test_string_grapheme(const MunitParameter params[],
                                          void *data) {
  // 👨 (4 bytes) + ZWJ (3 bytes) + 👩 (4 bytes) + ZWJ (3 bytes) + 👧 (4 bytes)
  // = 18 bytes
  const char *family_emoji = "👨‍👩‍👧";
  uint64_t expected_bytes = 18;

  gab_value str_val = gab_string(gab, family_emoji);

  munit_assert_uint32(gab_valkind(str_val), ==, kGAB_STRING);
  // There are 18 expected bytes.
  munit_assert_uint64(gab_strlen(str_val), ==, expected_bytes);
  // There are 5 codepoints.
  munit_assert_uint64(gab_strmblen(str_val), ==, 5);

  return MUNIT_OK;
}

static MunitTest string_tests[] = {
    {
        "/creation",
        test_string_creation,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/multibyte",
        test_string_multibyte,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/grapheme",
        test_string_grapheme,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/concat",
        test_string_concat,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/timeouts",
        test_string_timeouts,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        NULL,
        NULL,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
};

const MunitSuite string_suite = {"/string", string_tests, NULL, 1,
                                 MUNIT_SUITE_OPTION_NONE};
