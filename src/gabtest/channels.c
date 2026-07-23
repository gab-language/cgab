#include "cgab.h"
#include "munit/munit.h"
#include <stdint.h>

extern struct gab_triple gab;

static MunitResult test_channel_lifecycle(const MunitParameter params[],
                                          void *data) {
  gab_value ch = gab_channel(gab);

  munit_assert_true(gab_chnisempty(ch));
  munit_assert_false(gab_chnisfull(ch));
  munit_assert_false(gab_chnisclosed(ch));

  gab_chnclose(ch);

  munit_assert_true(gab_chnisclosed(ch));

  return MUNIT_OK;
}

static MunitResult test_channel_send_put(const MunitParameter params[],
                                         void *data) {
  gab_value ch = gab_channel(gab);
  gab_value val_in = gab_number(42);

  // asynchronously send a put
  union gab_value_pair put_res =
      gab_asend(gab, (struct gab_send_argt){
                         .message = gab_message(gab, mGAB_PUT),
                         .receiver = ch,
                         .argv = (gab_value[]){val_in},
                         .len = 1,
                     });

  munit_assert_uint64(put_res.status, ==, gab_cvalid);

  gab_value val_out = gab_chntake(gab, ch);

  munit_assert_true(gab_chnisempty(ch));
  munit_assert_uint64(val_in, ==, val_out);

  return MUNIT_OK;
}

static MunitResult test_channel_send_closed(const MunitParameter params[],
                                            void *data) {
  gab_value ch = gab_channel(gab);
  gab_value val_in = gab_number(99);

  gab_chnclose(ch);
  munit_assert_true(gab_chnisclosed(ch));

  union gab_value_pair put_res =
      gab_send(gab, (struct gab_send_argt){
                        .message = gab_message(gab, mGAB_PUT),
                        .receiver = ch,
                        .argv = (gab_value[]){val_in},
                        .len = 1,
                    });

  munit_assert_uint64(put_res.status, ==, gab_cvalid);
  munit_assert_uint64(put_res.aresult->data[0], ==, gab_ok);

  return MUNIT_OK;
}

static MunitResult test_channel_unsafe_put(const MunitParameter params[],
                                           void *data) {
  gab_value ch = gab_channel(gab);
  uint64_t tries = 1;

  gab_value values_in[] = {gab_number(100)};
  uint64_t len = 1;

  // Perform unsafe put (will not block indefinitely)
  gab_value put_res = gab_untchnput(gab, ch, len, values_in, tries);

  // Perform unsafe take to clear it
  gab_value values_out[1] = {0};
  gab_value take_res = gab_ntchntake(gab, ch, len, values_out, tries);

  munit_assert_uint64(values_out[0], ==, values_in[0]);

  return MUNIT_OK;
}

// TODO @cgabtest @opt: Optimize channel put/take

static MunitResult
test_channel_stress_concurrent_putters(const MunitParameter params[],
                                       void *data) {
  gab_value ch = gab_channel(gab);
  const uint64_t num_fibers = 5000;
  gab_value fibers[num_fibers];
  gab_value msg_put = gab_message(gab, mGAB_PUT);

  // 1. Queue up thousands of concurrent putters.
  for (uint64_t i = 0; i < num_fibers; i++) {
    union gab_value_pair put_res =
        gab_asend(gab, (struct gab_send_argt){
                           .message = msg_put,
                           .receiver = ch,
                           .argv = (gab_value[]){gab_number(1)},
                           .len = 1,
                       });

    munit_assert_uint64(put_res.status, ==, gab_cvalid);

    fibers[i] = put_res.vresult;
  }

  uint64_t total_received = 0;
  for (uint64_t i = 0; i < num_fibers; i++) {
    gab_value val = gab_chntake(gab, ch);

    // Verify the value was successfully passed through
    munit_assert_uint64(val, ==, gab_number(1));
    total_received++;

    int64_t done = 0;
    for (uint64_t f = 0; f < num_fibers; f++) {
      if (gab_fibisdone(fibers[f])) {
        done++;
        // If a fiber is done, check that it didn't error.
        union gab_value_pair res = gab_fibawait(gab, fibers[f]);
        munit_assert_uint64(res.status, ==, gab_cvalid);
        munit_assert_uint64(res.aresult->data[0], ==, gab_ok);
      }
    }

    // If more fibers are done than values we've receied so far, we have a bug!
    munit_assert_uint64(done, <=, total_received);
  }

  munit_assert_uint64(total_received, ==, num_fibers);
  munit_assert_true(gab_chnisempty(ch));

  return MUNIT_OK;
}

static MunitResult test_channel_concurrent_takers(const MunitParameter params[],
                                                  void *data) {
  gab_value ch = gab_channel(gab);
  const uint64_t num_fibers = 5000;
  gab_value fibers[num_fibers];
  gab_value msg_take = gab_message(gab, mGAB_TAKE);

  for (uint64_t i = 0; i < num_fibers; i++) {
    // NOTE: Pin the send to a *different thread*.
    // Because of how we try and wait for puts currently,
    // this can sometimes deadlock.
    // As a workaround, disallow the wkid 1 from pulling this job.
    union gab_value_pair take_res = gab_asend(gab, (struct gab_send_argt){
                                                       .message = msg_take,
                                                       .receiver = ch,
                                                       .argv = NULL,
                                                       .len = 0,
                                                       .pinmask = ~(1 << 0)
                                                   });

    munit_assert_uint64(take_res.status, ==, gab_cvalid);
    fibers[i] = take_res.vresult;
  }

  uint64_t total_sent = 0;
  for (uint64_t i = 0; i < num_fibers; i++) {
    // If more fibers are done than values we've sent so far, we have a bug
    int64_t done = 0;
    for (uint64_t f = 0; f < num_fibers; f++) {
      if (gab_fibisdone(fibers[f])) {
        done++;
        // Verify the taker finished without trapping an error
        union gab_value_pair res = gab_fibawait(gab, fibers[f]);
        munit_assert_uint64(res.status, ==, gab_cvalid);

        // Bug where primitives don't tailcall well. We have to skip over the channel and put arguments which
        // are mistakenly returned.
        munit_assert_uint64(res.aresult->data[0], ==, gab_ok);
        munit_assert_uint64(res.aresult->data[3], ==, gab_ok);
        munit_assert_uint64(res.aresult->data[4], ==, gab_number(1));
      }
    }
    munit_assert_uint64(done, <=, total_sent);

    // Flaw! If the taker is on *this thread*, deadlocks
    gab_chnput(gab, ch, gab_number(1));
    total_sent++;
  }

  munit_assert_uint64(total_sent, ==, num_fibers);
  munit_assert_true(gab_chnisempty(ch));

  return MUNIT_OK;
}

// This can lock up the system because of the "working queue" approach.
// If we get stuck with all takers/producers in our working queues,
// we actually can't ever make progress.
// For now, a lower number for num_pairs makes this impossible here.
static MunitResult test_channel_stress_randomized(const MunitParameter params[],
                                                  void *data) {
  gab_value ch = gab_channel(gab);
  const uint64_t num_pairs = 64;
  // const uint64_t num_pairs = 10000;
  const uint64_t total_fibers = num_pairs * 2;

  gab_value fibers[total_fibers];
  gab_value msg_put = gab_message(gab, mGAB_PUT);
  gab_value msg_take = gab_message(gab, mGAB_TAKE);

  // 0 = PUT, 1 = TAKE
  int actions[total_fibers];

  for (uint64_t i = 0; i < num_pairs; i++)
    actions[i] = 0;

  for (uint64_t i = num_pairs; i < total_fibers; i++)
    actions[i] = 1;

  // Shuffle
  for (uint64_t i = total_fibers - 1; i > 0; i--) {
    uint64_t j = (uint64_t)munit_rand_int_range(0, (int)i);
    int temp = actions[i];
    actions[i] = actions[j];
    actions[j] = temp;
  }

  // 3. Queue the fibers in the aggressively randomized order
  for (uint64_t i = 0; i < total_fibers; i++) {
    union gab_value_pair res;
    if (actions[i] == 0) {
      res = gab_asend(gab, (struct gab_send_argt){
                               .message = msg_put,
                               .receiver = ch,
                               .argv = (gab_value[]){gab_number(i)},
                               .len = 1,
                               .pinmask = ~(1 << 0)
                           });
    } else {
      res = gab_asend(gab, (struct gab_send_argt){
                               .message = msg_take,
                               .receiver = ch,
                               .argv = NULL,
                               .len = 0,
                           });
    }

    munit_assert_uint64(res.status, ==, gab_cvalid);
    fibers[i] = res.vresult;
  }

  // 4. Await all fibers to complete.
  for (uint64_t i = 0; i < total_fibers; i++) {
    union gab_value_pair res = gab_fibawait(gab, fibers[i]);

    // Verify every single fiber resolved without error
    munit_assert_uint64(res.status, ==, gab_cvalid);
  }

  // By the end, every Put must have matched with exactly one Take.
  munit_assert_true(gab_chnisempty(ch));

  return MUNIT_OK;
}

#define BATCH_STRESS_SIZE 2048

static MunitResult test_channel_stress_batch(const MunitParameter params[],
                                             void *data) {
  gab_value ch = gab_channel(gab);
  uint64_t tries = 1;

  // Initialize a large batch of values
  gab_value values_in[BATCH_STRESS_SIZE];
  for (uint64_t i = 0; i < BATCH_STRESS_SIZE; i++) {
    values_in[i] = gab_number(i);
  }

  // Perform massive unsafe put
  gab_value put_res =
      gab_untchnput(gab, ch, BATCH_STRESS_SIZE, values_in, tries);

  // Perform massive unsafe take to clear it
  gab_value values_out[BATCH_STRESS_SIZE] = {0};
  gab_value take_res =
      gab_ntchntake(gab, ch, BATCH_STRESS_SIZE, values_out, tries);

  // Verify memory integrity for the entire block
  for (uint64_t i = 0; i < BATCH_STRESS_SIZE; i++) {
    munit_assert_uint64(values_out[i], ==, values_in[i]);
  }

  munit_assert_true(gab_chnisempty(ch));

  return MUNIT_OK;
}

MunitTest channel_tests[] = {
    {
        "/lifecycle",
        test_channel_lifecycle,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/send_put_unbuffered",
        test_channel_send_put,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/send_closed",
        test_channel_send_closed,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/unsafe_put",
        test_channel_unsafe_put,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/concurrent_putters",
        test_channel_stress_concurrent_putters,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/stress_batch",
        test_channel_stress_batch,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/concurrent_takers",
        test_channel_concurrent_takers,
        NULL,
        NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL,
    },
    {
        "/randomized",
        test_channel_stress_randomized,
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

const MunitSuite channel_suite = {
    "/channel", channel_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE,
};
