// timing.cpp: when data counts as stale / the board as offline, and the live
// countdown. design.md 13.4.
#include <unity.h>

#include "timing.h"

void setUp(void) {}
void tearDown(void) {}

void test_stale_needs_data_and_a_poll_older_than_the_bar() {
  TEST_ASSERT_FALSE(dataStaleAt(false, 0, 20000, 1000000));   // no data: nothing to be stale
  TEST_ASSERT_TRUE(dataStaleAt(true, 0, 20000, 5));           // only the SD cache so far
  // 20 s poll: bar = max(40 s, 60 s) = 60 s
  TEST_ASSERT_FALSE(dataStaleAt(true, 100000, 20000, 160000));
  TEST_ASSERT_TRUE(dataStaleAt(true, 100000, 20000, 160001));
  // 60 s poll: bar = 120 s
  TEST_ASSERT_FALSE(dataStaleAt(true, 100000, 60000, 200000));
  TEST_ASSERT_TRUE(dataStaleAt(true, 100000, 60000, 220001));
}

void test_stale_survives_the_millis_wraparound() {
  TEST_ASSERT_FALSE(dataStaleAt(true, 0xFFFFFFF0u, 20000, 5));       // 21 ms apart
  TEST_ASSERT_TRUE(dataStaleAt(true, 0xFFFFFFF0u, 20000, 70000));    // ~70 s apart
}

void test_offline_grace_is_the_larger_of_three_minutes_and_three_polls() {
  TEST_ASSERT_EQUAL_UINT32(180000, offlineAfterMsFor(5000));
  TEST_ASSERT_EQUAL_UINT32(180000, offlineAfterMsFor(20000));
  TEST_ASSERT_EQUAL_UINT32(180000, offlineAfterMsFor(60000));  // exactly equal
  TEST_ASSERT_EQUAL_UINT32(360000, offlineAfterMsFor(120000)); // Battery Save
}

void test_offline_is_always_later_than_stale() {
  const uint32_t polls[] = {5000, 20000, 60000, 120000};
  for (uint32_t p : polls) {
    uint32_t staleAfter = p * 2 < STALE_AFTER_MIN_MS ? STALE_AFTER_MIN_MS : p * 2;
    TEST_ASSERT_TRUE(offlineAfterMsFor(p) > staleAfter);
  }
}

void test_live_countdown() {
  TEST_ASSERT_EQUAL_INT(-1, liveResetsFrom(-1, 999999, 0));
  TEST_ASSERT_EQUAL_INT(100, liveResetsFrom(100, 5000, 5000));
  TEST_ASSERT_EQUAL_INT(70, liveResetsFrom(100, 35500, 5000));   // 30.5 s -> 30 whole seconds
  TEST_ASSERT_EQUAL_INT(0, liveResetsFrom(100, 500000, 5000));   // never negative
  TEST_ASSERT_EQUAL_INT(0, liveResetsFrom(0, 5000, 5000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_stale_needs_data_and_a_poll_older_than_the_bar);
  RUN_TEST(test_stale_survives_the_millis_wraparound);
  RUN_TEST(test_offline_grace_is_the_larger_of_three_minutes_and_three_polls);
  RUN_TEST(test_offline_is_always_later_than_stale);
  RUN_TEST(test_live_countdown);
  return UNITY_END();
}
