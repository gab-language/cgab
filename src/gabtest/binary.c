#include "munit/munit.h"
#include "gab.h"

extern struct gab_triple gab;

static MunitResult test_bin_basic(const MunitParameter params[], void* data) {
    const char* raw_str = "hello, gab";
    gab_value bin_val = gab_binary(gab, raw_str);
    
    gab_value str_val = gab_bintostr(bin_val);
    
    munit_assert_uint32(gab_valkind(bin_val), ==, kGAB_BINARY);
    
    uint64_t str_len = gab_strmblen(str_val);
    uint64_t bin_len = gab_strlen(bin_val);
    munit_assert_uint64(str_len, ==, bin_len);
    
    const char* bin_data = gab_strdata(&bin_val);
    munit_assert_memory_equal(bin_len, raw_str, bin_data);
    
    return MUNIT_OK;
}

static MunitResult test_bin_empty(const MunitParameter params[], void* data) {
    gab_value bin_val = gab_binary(gab, "");
    
    munit_assert_uint32(gab_valkind(bin_val), ==, kGAB_BINARY);
    munit_assert_uint64(gab_strlen(bin_val), ==, 0);
    munit_assert_uint64(gab_strmblen(bin_val), ==, 0);
    
    return MUNIT_OK;
}

static MunitResult test_bin_unicode(const MunitParameter params[], void* data) {
    // "Hello" plus a 4-byte emoji 🚀
    const char* raw_unicode = "Hello 🚀";

    gab_value bin_val = gab_binary(gab, raw_unicode);
    
    munit_assert_uint32(gab_valkind(bin_val), ==, kGAB_BINARY);
    
    // The byte length should reflect the full UTF-8 payload size
    // 'H','e','l','l','o',' ' = 6 bytes, '🚀' = 4 bytes -> Total 10 bytes
    uint64_t expected_bytes = 10;
    munit_assert_uint64(gab_strlen(bin_val), ==, expected_bytes);
    munit_assert_uint64(gab_strmblen(bin_val), ==, 7);
    
    const char* bin_data = gab_strdata(&bin_val);
    munit_assert_memory_equal(expected_bytes, raw_unicode, bin_data);
    
    return MUNIT_OK;
}

static MunitResult test_bin_invalid_unicode(const MunitParameter params[], void* data) {
    // 0xFF is never a valid UTF-8 byte. 
    // 0x80 is a continuation byte, which is invalid without a leading byte.
    const uint8_t invalid_utf8[] = { 0xFF, 0x80 };
    uint64_t len = sizeof(invalid_utf8) / sizeof(uint8_t);
    
    gab_value bin_val = gab_nbinary(gab, len, invalid_utf8);

    munit_assert_memory_equal(len, gab_strdata(&bin_val), invalid_utf8);
    
    gab_value str_val = gab_bintostr(bin_val);
    
    munit_assert_uint64(str_val, ==, gab_cinvalid);
    
    return MUNIT_OK;
}

static MunitResult test_bin_identity(const MunitParameter params[], void* data) {
    const char* raw = "immutable";
    gab_value bin1 = gab_binary(gab, (uint8_t*)raw);
    gab_value bin2 = gab_nbinary(gab, strlen(raw), (uint8_t*)raw);
    gab_value bin3 = gab_strtobin(gab_string(gab, raw));
    
    // If your binaries are globally interned like your strings, this should pass:
    // munit_assert_uint64(bin1, ==, bin2);
    
    // Otherwise, assert value equality:
    munit_assert_uint64(gab_strlen(bin1), ==, gab_strlen(bin2));
    munit_assert_uint64(gab_strlen(bin2), ==, gab_strlen(bin3));
    munit_assert_memory_equal(gab_strlen(bin1), gab_strdata(&bin1), gab_strdata(&bin2));
    munit_assert_memory_equal(gab_strlen(bin2), gab_strdata(&bin2), gab_strdata(&bin3));
    
    return MUNIT_OK;
}

/* * Array of tests to be registered with the main munit runner 
 */
MunitTest binary_tests[] = {
    {
        "/basic",
        test_bin_basic,
        NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL
    },
    {
        "/empty",
        test_bin_empty,
        NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL
    },
    {
        "/valid_unicode",
        test_bin_unicode,
        NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL
    },
    {
        "/identity",
        test_bin_identity,
        NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL
    },
    {
        "/invalid_unicode",
        test_bin_invalid_unicode,
        NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL
    },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

const MunitSuite binary_suite = {
    "/binary",
    binary_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};
