#include "cgab.h"
#include "munit/munit.h"

// Reference the global gab runtime triple set up by your runner/main file
extern struct gab_triple gab;

static MunitResult test_shape_identity(const MunitParameter params[],
                                       void *data) {
  gab_value k1 = gab_string(gab, "alpha");
  gab_value k2 = gab_string(gab, "beta");
  gab_value k3 = gab_string(gab, "gamma");

  // Create two independent shapes with identical keys
  gab_value shp1 = gab_shapeof(gab, k1, k2, k3);
  gab_value shp2 = gab_shapeof(gab, k1, k2, k3);

  // Assert they are structurally identical (pointer/value equality)
  munit_assert_uint64(shp1, ==, shp2);

  // Validate core shape properties
  munit_assert_true(gab_valisshp(shp1));
  munit_assert_uint64(gab_shplen(shp1), ==, 3);

  // Create a shape using the mshapeof macro (which takes c-strings and converts
  // to messages)
  gab_value mshp1 = gab_mshapeof(gab, "alpha", "beta", "gamma");
  gab_value mshp2 = gab_mshapeof(gab, "alpha", "beta", "gamma");

  munit_assert_uint64(mshp1, ==, mshp2);

  return MUNIT_OK;
}

static MunitResult test_shape_transitions(const MunitParameter params[],
                                          void *data) {
  gab_value k_id = gab_string(gab, "id");
  gab_value k_name = gab_string(gab, "name");

  // Start with an empty shape
  gab_value root = gab_shape(gab, 0, 0, NULL);

  // Transition 1: Add 'id'
  gab_value shp_id = gab_shpwith(gab, root, k_id);
  munit_assert_uint64(shp_id, ==, gab_shapeof(gab, k_id));
  munit_assert_uint64(gab_shplen(shp_id), ==, 1);
  munit_assert_false(gab_shpisl(shp_id));

  gab_value shp_id_name = gab_shpwith(gab, shp_id, k_name);
  munit_assert_uint64(shp_id_name, ==, gab_shapeof(gab, k_id, k_name));
  munit_assert_uint64(gab_shplen(shp_id_name), ==, 2);
  munit_assert_false(gab_shpisl(shp_id_name));

  gab_value shp_id_reverted = gab_shpwithout(gab, shp_id_name, k_name);
  munit_assert_uint64(shp_id_reverted, ==, shp_id);
  munit_assert_false(gab_shpisl(shp_id_reverted));

  gab_value shp_id_zero = gab_shpwith(gab, shp_id_reverted, gab_number(0));
  munit_assert_uint64(gab_shplen(shp_id_zero), ==, 2);
  munit_assert_false(gab_shpisl(shp_id_zero));

  // Should transition to a list on removal
  gab_value shp_zero = gab_shpwithout(gab, shp_id_zero, k_id);
  munit_assert_uint64(gab_shplen(shp_zero), ==, 1);
  munit_assert_true(gab_shpisl(shp_zero));

  return MUNIT_OK;
}

static MunitResult test_shape_inspection(const MunitParameter params[],
                                         void *data) {
  gab_value k_first = gab_string(gab, "first");
  gab_value k_second = gab_string(gab, "second");
  gab_value k_missing = gab_string(gab, "missing");

  gab_value shp = gab_shapeof(gab, k_first, k_second);

  // Verify existence checks
  munit_assert_true(gab_shphas(shp, k_first));
  munit_assert_true(gab_shphas(shp, k_second));
  munit_assert_false(gab_shphas(shp, k_missing));

  // Verify index finding
  munit_assert_uint64(gab_shpfind(shp, k_first), ==, 0);
  munit_assert_uint64(gab_shpfind(shp, k_second), ==, 1);

  // gab_shpfind returns (uint64_t)-1 when a key is not found
  munit_assert_uint64(gab_shpfind(shp, k_missing), ==, (uint64_t)-1);

  // Verify key retrieval by index
  munit_assert_uint64(gab_shpat(shp, 0), ==, k_first);
  munit_assert_uint64(gab_shpat(shp, 1), ==, k_second);
  munit_assert_uint64(gab_shpat(shp, 999), ==, gab_cundefined); // Out of bounds

  return MUNIT_OK;
}

static MunitResult test_shape_concatenation(const MunitParameter params[],
                                            void *data) {
  gab_value k1 = gab_string(gab, "x");
  gab_value k2 = gab_string(gab, "y");
  gab_value k3 = gab_string(gab, "z");

  gab_value shp1 = gab_shapeof(gab, k1, k2);
  gab_value shp2 = gab_shapeof(gab, k2, k3); // Overlapping key 'y'

  gab_value merged = gab_shpcat(gab, shp1, shp2);

  // The resulting shape should combine unique keys, dropping duplicates
  munit_assert_uint64(gab_shplen(merged), ==, 3);
  munit_assert_true(gab_shphas(merged, k1));
  munit_assert_true(gab_shphas(merged, k2));
  munit_assert_true(gab_shphas(merged, k3));

  // Ensure it matches the equivalent manual construction
  munit_assert_uint64(merged, ==, gab_shapeof(gab, k1, k2, k3));

  return MUNIT_OK;
}

static MunitResult test_shape_fuzz_transitions(const MunitParameter params[],
                                               void *data) {
  const int kIterations = 3000;
  const int kPoolSize = 10;

  gab_value keys[kPoolSize];
  for (int i = 0; i < kPoolSize; i++) {
    char buf[16];
    snprintf(buf, sizeof(buf), "fuzz_key_%d", i);
    keys[i] = gab_string(gab, buf);

    gab_iref(gab, keys[i]);
  }

  gab_value shp_a = gab_shape(gab, 0, 0, nullptr);
  gab_iref(gab, shp_a);

  gab_value shp_b = gab_shape(gab, 0, 0, nullptr);
  gab_iref(gab, shp_b);

  munit_assert_uint64(shp_a, ==, shp_b);

  for (int i = 0; i < kIterations; i++) {
    gab_value rand_key = keys[munit_rand_int_range(0, kPoolSize - 1)];
    int action = munit_rand_int_range(0, 1);

    // Apply exactly the same transition to both shapes
    if (action == 0) {
      shp_a = gab_shpwith(gab, shp_a, rand_key);
      gab_iref(gab, shp_a);

      shp_b = gab_shpwith(gab, shp_b, rand_key);
      gab_iref(gab, shp_b);
    } else {
      shp_a = gab_shpwithout(gab, shp_a, rand_key);
      gab_iref(gab, shp_a);

      shp_b = gab_shpwithout(gab, shp_b, rand_key);
      gab_iref(gab, shp_b);
    }

    munit_assert_uint64(shp_a, ==, shp_b);

    munit_assert_uint64(gab_shplen(shp_a), <=, (size_t)kPoolSize);
  }

  return MUNIT_OK;
}

static MunitResult test_shape_deduplication(const MunitParameter params[], void* data) {
    gab_value k_alpha = gab_string(gab, "alpha");
    gab_value k_beta  = gab_string(gab, "beta");
    gab_value k_gamma = gab_string(gab, "gamma");

    // Array with explicit duplicates
    gab_value raw_keys[] = { k_alpha, k_beta, k_alpha, k_gamma, k_beta };
    size_t raw_len = sizeof(raw_keys) / sizeof(gab_value);
    
    // Create the shape from the raw duplicated array
    gab_value deduplicated_shp = gab_shape(gab, 1, raw_len, raw_keys);

    // 1. The length must be exactly 3 (the number of unique keys)
    munit_assert_uint64(gab_shplen(deduplicated_shp), ==, 3);

    // 2. The keys must exist in the order of their FIRST appearance
    munit_assert_uint64(gab_shpat(deduplicated_shp, 0), ==, k_alpha);
    munit_assert_uint64(gab_shpat(deduplicated_shp, 1), ==, k_beta);
    munit_assert_uint64(gab_shpat(deduplicated_shp, 2), ==, k_gamma);

    // 3. Structural Identity Verification
    // A shape built from an explicitly clean array MUST match our deduplicated shape
    gab_value clean_keys[] = { k_alpha, k_beta, k_gamma };
    gab_value clean_shp = gab_shape(gab, 1, 3, clean_keys);

    munit_assert_uint64(deduplicated_shp, ==, clean_shp);

    // 4. Test Macro deduplication
    // (Ensure the vararg macro gab_shapeof behaves the same way)
    gab_value macro_shp = gab_shapeof(gab, k_alpha, k_beta, k_alpha, k_gamma);
    munit_assert_uint64(macro_shp, ==, clean_shp);

    return MUNIT_OK;
}

static MunitResult test_shape_list_transitions(const MunitParameter params[], void* data) {
    gab_value list = gab_shapeof(gab, gab_number(0), gab_number(1));

    munit_assert_true(gab_shpisl(list));

    gab_value with_key = gab_shpwith(gab, list, gab_message(gab, "val"));

    munit_assert_false(gab_shpisl(with_key));

    gab_value without_key = gab_shpwithout(gab, with_key, gab_message(gab, "val"));

    munit_assert_true(gab_shpisl(without_key));

    gab_value without_zero = gab_shpwithout(gab, without_key, gab_number(0));
    
    munit_assert_false(gab_shpisl(without_zero));

    return MUNIT_OK;
}

static MunitTest shape_tests[] = {
    {
        "/identity",
        test_shape_identity,
    },
    {
        "/transitions",
        test_shape_transitions,
    },
    {
        "/inspection",
        test_shape_inspection,
    },
    {
        "/concatenation",
        test_shape_concatenation,
    },
    {
        "/fuzz_transitions",
        test_shape_fuzz_transitions,
    },
    {
        "/deduplication",
        test_shape_deduplication,
    },
    {
        "/list_transitions",
        test_shape_list_transitions,
    },
    {},
};

const MunitSuite shape_suite = {
    "/shape", shape_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE,
};
