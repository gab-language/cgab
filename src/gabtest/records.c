#include "engine.h"
#include "gab.h"
#include "munit/munit.h"

extern struct gab_triple gab;

static MunitResult test_record_creation(const MunitParameter params[],
                                        void *data) {
  gab_value key = gab_string(gab, "name");
  gab_value val = gab_string(gab, "Gab Language");

  gab_value rec = gab_recordof(gab, key, val);

  munit_assert_uint64(gab_valkind(rec), ==, kGAB_RECORD);
  munit_assert_uint64(gab_reclen(rec), ==, 1);

  return MUNIT_OK;
}

static MunitResult test_record_property_access(const MunitParameter params[],
                                               void *data) {
  gab_value k_age = gab_string(gab, "age");
  gab_value v_age = gab_number(3);

  gab_value rec = gab_recordof(gab, k_age, v_age);

  gab_value res = gab_recat(rec, k_age);
  munit_assert_uint64(res, ==, v_age);

  gab_value s_res = gab_srecat(gab, rec, "age");
  munit_assert_uint64(s_res, ==, v_age);

  gab_value missing_key = gab_string(gab, "missing");
  munit_assert_uint64(gab_recat(rec, missing_key), ==, gab_cundefined);

  return MUNIT_OK;
}

static MunitResult
test_record_immutability_and_put(const MunitParameter params[], void *data) {
  gab_value k1 = gab_string(gab, "a");
  gab_value v1 = gab_number(10);
  gab_value k2 = gab_string(gab, "b");
  gab_value v2 = gab_number(20);

  gab_value original_rec = gab_recordof(gab, k1, v1);

  gab_value updated_rec = gab_recput(gab, original_rec, k2, v2);

  munit_assert_uint64(gab_reclen(original_rec), ==, 1);
  munit_assert_uint64(gab_recat(original_rec, k2), ==, gab_cundefined);

  munit_assert_uint64(gab_reclen(updated_rec), ==, 2);
  munit_assert_uint64(gab_recat(updated_rec, k1), ==, v1);
  munit_assert_uint64(gab_recat(updated_rec, k2), ==, v2);

  return MUNIT_OK;
}

static MunitResult test_record_concatenation(const MunitParameter params[],
                                             void *data) {
  gab_value k1 = gab_string(gab, "x");
  gab_value v1 = gab_number(100);
  gab_value v2 = gab_number(200);

  gab_value rec_under = gab_recordof(gab, k1, v1);
  gab_value rec_over = gab_recordof(gab, k1, v2);

  gab_value merged = gab_reccat(gab, rec_under, rec_over);

  munit_assert_uint64(gab_reclen(merged), ==, 1);
  munit_assert_uint64(gab_recat(merged, k1), ==, v2);

  return MUNIT_OK;
}

static MunitResult test_record_hamt_boundary(const MunitParameter params[],
                                             void *data) {
  const int kTotalElements = 50;

  gab_value rec = gab_erecord(gab);

  for (int i = 0; i < kTotalElements; i++) {
    rec = gab_recput(gab, rec, gab_number(i), gab_number(i * 10));
    munit_assert_uint64(gab_valkind(rec), ==, kGAB_RECORD);
    munit_assert_uint64(gab_reclen(rec), ==, (size_t)(i + 1));
  }

  for (int i = 0; i < kTotalElements; i++) {
    gab_value val = gab_recat(rec, gab_number(i));
    munit_assert_uint64(val, ==, gab_number(i * 10));
  }

  const int kElementsToRemove = 20;
  for (int i = 0; i < kElementsToRemove; i++) {
    gab_value vout = gab_cundefined;
    rec = gab_rectake(gab, rec, gab_number(i), &vout);

    munit_assert_uint64(gab_valtou(vout), ==, i * 10);
  }

  gab_fvalinspect(stderr, rec, 100);

  munit_assert_uint64(gab_reclen(rec), ==,
                      (size_t)(kTotalElements - kElementsToRemove));

  for (int i = 0; i < kElementsToRemove; i++) {
    munit_assert_uint64(gab_recat(rec, gab_number(i)), ==, gab_cundefined);
  }

  for (int i = kElementsToRemove; i < kTotalElements; i++) {
    munit_assert_uint64(gab_valtou(gab_recat(rec, gab_number(i))), ==, i * 10);
  }

  return MUNIT_OK;
}

static MunitResult test_record_shapes(const MunitParameter params[],
                                      void *data) {
  gab_value key_a = gab_string(gab, "alpha");
  gab_value key_b = gab_string(gab, "beta");

  gab_value rec1 =
      gab_recordof(gab, key_a, gab_number(100), key_b, gab_number(200));
  gab_value rec2 =
      gab_recordof(gab, key_a, gab_number(300), key_b, gab_number(400));

  gab_value shape1 = gab_recshp(rec1);
  gab_value shape2 = gab_recshp(rec2);

  gab_fprintf(stderr, "$ vs $\n", shape1, shape2);

  munit_assert_uint64(shape1, ==, shape2);

  return MUNIT_OK;
}

static MunitResult test_record_from_array(const MunitParameter params[],
                                          void *data) {
  gab_value keys[] = {
      gab_string(gab, "x"),
      gab_string(gab, "y"),
      gab_string(gab, "z"),
  };

  gab_value values[] = {
      gab_number(1),
      gab_number(2),
      gab_number(3),
  };

  size_t count = sizeof(keys) / sizeof(gab_value);

  gab_value rec = gab_record(gab, 1, count, keys, values);

  munit_assert_uint64(gab_reclen(rec), ==, count);
  munit_assert_uint64(gab_recat(rec, keys[0]), ==, gab_number(1));
  munit_assert_uint64(gab_recat(rec, keys[1]), ==, gab_number(2));
  munit_assert_uint64(gab_recat(rec, keys[2]), ==, gab_number(3));

  return MUNIT_OK;
}

static MunitResult test_record_push_pop(const MunitParameter params[],
                                        void *data) {
  gab_value rec = gab_listof(gab, gab_number(0), gab_number(1));
  size_t initial_len = gab_reclen(rec);

  // Push elements onto the record
  gab_value rec_pushed1 = gab_lstpush(gab, rec, gab_number(11));
  gab_value rec_pushed2 = gab_lstpush(gab, rec_pushed1, gab_number(22));

  munit_assert_uint64(gab_reclen(rec_pushed2), ==, initial_len + 2);

  // Pop elements back off
  gab_value rec_popped1 = gab_recpop(gab, rec_pushed2, nullptr, nullptr);
  munit_assert_uint64(gab_reclen(rec_popped1), ==, initial_len + 1);

  gab_value rec_popped2 = gab_recpop(gab, rec_popped1, nullptr, nullptr);
  munit_assert_uint64(gab_reclen(rec_popped2), ==, initial_len);

  return MUNIT_OK;
}

static MunitResult test_record_find(const MunitParameter params[], void *data) {
  gab_value key_target = gab_string(gab, "target");
  gab_value rec = gab_recordof(gab, key_target, gab_number(999));

  // gab_recfind should safely resolve existing keys
  uint64_t found_offset = gab_recfind(rec, key_target);
  munit_assert_uint64(gab_uvrecat(rec, found_offset), ==, gab_number(999));

  // Searching for non-existent key should return gab_cundefined safely without
  // blowing up
  uint64_t missing_key = gab_string(gab, "ghost");
  munit_assert_uint64(gab_recfind(rec, missing_key), ==, -1);

  return MUNIT_OK;
}

static MunitResult test_record_tidal_wave(const MunitParameter params[],
                                          void *data) {
  const int kMaxElements = 10000;
  const int kWaves = 10;

  gab_value rec = gab_erecord(gab); // Start with empty record
  gab_iref(gab, rec);

  // Arrays to hold our keys and track our random insertion/deletion order
  gab_value keys[kMaxElements];
  int indices[kMaxElements];

  for (int i = 0; i < kMaxElements; i++) {
    keys[i] = gab_number((double)i);
    indices[i] = i;
  }

  for (int wave = 0; wave < kWaves; wave++) {
    // 1. Shuffle indices for random insertion order
    for (int i = kMaxElements - 1; i > 0; i--) {
      int j = munit_rand_int_range(0, i);
      int temp = indices[i];
      indices[i] = indices[j];
      indices[j] = temp;
    }

    // 2. Insert all elements in random order
    for (int i = 0; i < kMaxElements; i++) {
      int idx = indices[i];
      gab_value val = gab_number((double)(idx * 100));

      gab_value new_rec = gab_recput(gab, rec, keys[idx], val);

      gab_iref(gab, new_rec);
      gab_dref(gab, rec);

      rec = new_rec;

      // Validate length grows correctly
      munit_assert_uint64(gab_reclen(rec), ==, (size_t)(i + 1));
      // Validate the inserted element exists
      munit_assert_uint64(gab_recat(rec, keys[idx]), ==, val);
    }

    // 3. Reshuffle indices for random deletion order
    for (int i = kMaxElements - 1; i > 0; i--) {
      int j = munit_rand_int_range(0, i);
      int temp = indices[i];
      indices[i] = indices[j];
      indices[j] = temp;
    }

    // 4. Drain all elements in random order
    for (int i = 0; i < kMaxElements; i++) {
      int idx = indices[i];
      gab_value removed_val;

      gab_value new_rec = gab_rectake(gab, rec, keys[idx], &removed_val);

      gab_iref(gab, new_rec);
      gab_dref(gab, rec);

      rec = new_rec;

      // Validate the removed value matches what we originally put
      munit_assert_uint64(removed_val, ==, gab_number((double)(idx * 100)));
      // Validate length shrinks correctly
      munit_assert_uint64(gab_reclen(rec), ==, (size_t)(kMaxElements - i - 1));
      // Validate the element is actually gone
      munit_assert_uint64(gab_recat(rec, keys[idx]), ==, gab_cundefined);
    }

    // At the end of the wave, the record must be entirely empty
    munit_assert_uint64(gab_reclen(rec), ==, 0);
  }

  return MUNIT_OK;
}

static MunitResult test_record_fuzz_walk(const MunitParameter params[],
                                         void *data) {
  const int kIterations = 5000;
  const int kKeyPoolSize = 80; // Large enough to cross HAMT boundaries

  gab_value rec = gab_erecord(gab);
  gab_egkeep(gab.eg, gab_iref(gab, rec));

  // Shadow state to verify against
  bool is_present[kKeyPoolSize];
  gab_value expected_vals[kKeyPoolSize];
  size_t expected_len = 0;

  for (int i = 0; i < kKeyPoolSize; i++) {
    is_present[i] = false;
    expected_vals[i] = gab_cundefined;
  }

  for (int i = 0; i < kIterations; i++) {
    // Pick a random key from our pool
    int key_idx = munit_rand_int_range(0, kKeyPoolSize - 1);
    gab_value key = gab_number((double)key_idx);

    // 60% chance to put/update, 40% chance to take
    // This slight bias ensures the record grows past 32 but still fluctuates
    // heavily
    int action = munit_rand_int_range(1, 100);

    if (action <= 60) {
      // PUT / UPDATE
      gab_value val = gab_number((double)munit_rand_uint32());
      rec = gab_recput(gab, rec, key, val);
      gab_egkeep(gab.eg, gab_iref(gab, rec));

      if (!is_present[key_idx]) {
        is_present[key_idx] = true;
        expected_len++;
      }
      expected_vals[key_idx] = val;

    } else {
      // TAKE
      rec = gab_rectake(gab, rec, key, NULL);
      gab_egkeep(gab.eg, gab_iref(gab, rec));

      if (is_present[key_idx]) {
        is_present[key_idx] = false;
        expected_len--;
      }
      expected_vals[key_idx] = gab_cundefined;
    }

    // Randomly sample the state of the record during the walk
    if (i % 50 == 0) {
      munit_assert_uint64(gab_reclen(rec), ==, expected_len);

      // Spot check a few random keys against the shadow state
      for (int check = 0; check < 5; check++) {
        int test_idx = munit_rand_int_range(0, kKeyPoolSize - 1);
        gab_value test_key = gab_number((double)test_idx);
        munit_assert_uint64(gab_recat(rec, test_key), ==,
                            expected_vals[test_idx]);
      }
    }
  }

  // Final comprehensive verification of all keys
  munit_assert_uint64(gab_reclen(rec), ==, expected_len);
  for (int i = 0; i < kKeyPoolSize; i++) {
    gab_value key = gab_number((double)i);
    munit_assert_uint64(gab_recat(rec, key), ==, expected_vals[i]);
  }

  return MUNIT_OK;
}

// Map individual tests to an munit array
static MunitTest record_tests[] = {
    {
        "/creation",
        test_record_creation,
    },
    {
        "/creation_dynamic",
        test_record_from_array,
    },
    {
        "/property_access",
        test_record_property_access,
    },
    {
        "/immutability_put",
        test_record_immutability_and_put,
    },
    {
        "/concatenation",
        test_record_concatenation,
    },
    {
        "/find",
        test_record_find,
    },
    {
        "/hamt_boundary",
        test_record_hamt_boundary,
    },
    {
        "/creation",
        test_record_from_array,
    },
    {
        "/shapes",
        test_record_shapes,
    },
    {
        "/push-pop",
        test_record_push_pop,
    },
    {
        "/tidal_wave",
        test_record_tidal_wave,
    },
    {
        "/fuzz_walk",
        test_record_fuzz_walk,
    },
    {},
};

// Define the global suite variable for your entry file to pull in
const MunitSuite record_suite = {"/record", record_tests, NULL, 1,
                                 MUNIT_SUITE_OPTION_NONE};
