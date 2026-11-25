// pace.cpp: pace %, the run-out projection, the flag deadband and the alert
// crossing. Inputs are chosen to be exact in float so a result can't hinge on
// rounding noise.
#include <unity.h>

#include <string.h>

#include "pace.h"

void setUp(void) {}
void tearDown(void) {}

void test_elapsed_percent() {
  TEST_ASSERT_EQUAL_INT(-1, elapsedPercentOfWindow(-1, SESSION_WINDOW_SEC));
  TEST_ASSERT_EQUAL_INT(0, elapsedPercentOfWindow(SESSION_WINDOW_SEC, SESSION_WINDOW_SEC));
  TEST_ASSERT_EQUAL_INT(50, elapsedPercentOfWindow(SESSION_WINDOW_SEC / 2, SESSION_WINDOW_SEC));
  TEST_ASSERT_EQUAL_INT(100, elapsedPercentOfWindow(0, SESSION_WINDOW_SEC));
  TEST_ASSERT_EQUAL_INT(0, elapsedPercentOfWindow(WEEK_WINDOW_SEC + 500, WEEK_WINDOW_SEC));  // stale snapshot
  TEST_ASSERT_EQUAL_INT(25, elapsedPercentOfWindow(WEEK_WINDOW_SEC * 3 / 4, WEEK_WINDOW_SEC));
}

void test_seconds_to_limit() {
  TEST_ASSERT_EQUAL_INT(100, paceSecondsToLimit(50, 100, 200));  // rate .5%/s, 50% left
  TEST_ASSERT_EQUAL_INT(300, paceSecondsToLimit(25, 100, 1000));
}

void test_seconds_to_limit_says_nothing_when_unknown_or_reset_wins() {
  TEST_ASSERT_EQUAL_INT(-1, paceSecondsToLimit(0, 100, 200));     // no usage yet
  TEST_ASSERT_EQUAL_INT(-1, paceSecondsToLimit(50, 0, 200));      // window just opened
  TEST_ASSERT_EQUAL_INT(-1, paceSecondsToLimit(50, 100, -1));     // reset unknown
  TEST_ASSERT_EQUAL_INT(-1, paceSecondsToLimit(100, 100, 200));   // already full
  TEST_ASSERT_EQUAL_INT(-1, paceSecondsToLimit(50, 100, 100));    // lands exactly at the reset
  TEST_ASSERT_EQUAL_INT(-1, paceSecondsToLimit(50, 100, 50));     // reset comes first
}

void test_pace_duration_text() {
  char b[16];
  formatPaceDurBuf(b, sizeof(b), 25, 100, 1000);           // 300 s
  TEST_ASSERT_EQUAL_STRING("5m", b);
  formatPaceDurBuf(b, sizeof(b), 50, 10, 100);             // 10 s rounds up to the 1m floor
  TEST_ASSERT_EQUAL_STRING("1m", b);
  formatPaceDurBuf(b, sizeof(b), 50, 10800, 100000);       // 3 h
  TEST_ASSERT_EQUAL_STRING("3h", b);
  formatPaceDurBuf(b, sizeof(b), 0, 100, 1000);
  TEST_ASSERT_EQUAL_STRING("", b);
}

void test_pace_ahead_deadband() {
  TEST_ASSERT_FALSE(paceAhead(-1, 50));
  TEST_ASSERT_FALSE(paceAhead(50, -1));
  TEST_ASSERT_FALSE(paceAhead(50, 50));
  TEST_ASSERT_FALSE(paceAhead(52, 50));  // inside the 2-point deadband
  TEST_ASSERT_TRUE(paceAhead(53, 50));
  TEST_ASSERT_TRUE(paceAhead(100, 0));
  TEST_ASSERT_FALSE(paceAhead(0, 0));
}

// 5 h window = 18000 s. 20% elapsed = 3600 s in, 14400 s left.
static const long W = SESSION_WINDOW_SEC;

void test_alert_waits_for_ten_percent_of_the_window() {
  LimitAlert a;
  TEST_ASSERT_FALSE(limitAlertCrossed(a, 20, W - 900, W, false));  // 5% elapsed
  TEST_ASSERT_FALSE(a.runOut);
}

void test_alert_fires_once_when_pace_runs_out_before_reset() {
  LimitAlert a;
  TEST_ASSERT_TRUE(limitAlertCrossed(a, 50, W - 3600, W, false));   // 50% used at 20% elapsed
  TEST_ASSERT_TRUE(a.runOut);
  TEST_ASSERT_FALSE(limitAlertCrossed(a, 52, W - 3700, W, false));  // already alerted
}

void test_alert_on_reaching_full_and_only_once() {
  LimitAlert a;
  TEST_ASSERT_TRUE(limitAlertCrossed(a, 100, 100, W, false));
  TEST_ASSERT_TRUE(a.full);
  TEST_ASSERT_FALSE(limitAlertCrossed(a, 100, 90, W, false));
}

void test_prime_records_state_without_alerting() {
  LimitAlert a;
  TEST_ASSERT_FALSE(limitAlertCrossed(a, 50, W - 3600, W, true));
  TEST_ASSERT_TRUE(a.runOut);
  TEST_ASSERT_FALSE(limitAlertCrossed(a, 55, W - 3700, W, false));  // already known
}

void test_window_reset_rearms() {
  LimitAlert a;
  limitAlertCrossed(a, 50, W - 3600, W, false);
  TEST_ASSERT_FALSE(limitAlertCrossed(a, 5, W - 60, W, false));     // usage fell: new window
  TEST_ASSERT_FALSE(a.runOut);
  TEST_ASSERT_TRUE(limitAlertCrossed(a, 50, W - 3600, W, false));   // alerts again
}

void test_unavailable_limit_keeps_state() {
  LimitAlert a;
  limitAlertCrossed(a, 50, W - 3600, W, false);
  TEST_ASSERT_FALSE(limitAlertCrossed(a, -1, W - 3600, W, false));
  TEST_ASSERT_FALSE(limitAlertCrossed(a, 50, -1, W, false));
  TEST_ASSERT_EQUAL_INT(50, a.lastPct);
  TEST_ASSERT_TRUE(a.runOut);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_elapsed_percent);
  RUN_TEST(test_seconds_to_limit);
  RUN_TEST(test_seconds_to_limit_says_nothing_when_unknown_or_reset_wins);
  RUN_TEST(test_pace_duration_text);
  RUN_TEST(test_pace_ahead_deadband);
  RUN_TEST(test_alert_waits_for_ten_percent_of_the_window);
  RUN_TEST(test_alert_fires_once_when_pace_runs_out_before_reset);
  RUN_TEST(test_alert_on_reaching_full_and_only_once);
  RUN_TEST(test_prime_records_state_without_alerting);
  RUN_TEST(test_window_reset_rearms);
  RUN_TEST(test_unavailable_limit_keeps_state);
  return UNITY_END();
}
