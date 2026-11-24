// format_pure.cpp: the exact text the board draws. Expected strings are worked
// out from the original String-based code; the token rows pin the ESP32
// core's dtostrf rounding, which is not printf's (see the near-ties test).
#include <unity.h>

#include <string.h>

#include "format_pure.h"

void setUp(void) {}
void tearDown(void) {}

static void tokens(int64_t t, const char* want) {
  char b[FMT_BUF_MIN];
  fmtTokensBuf(b, sizeof(b), t);
  TEST_ASSERT_EQUAL_STRING(want, b);
}

void test_tokens_plain_below_a_thousand() {
  tokens(0, "0");
  tokens(7, "7");
  tokens(999, "999");
}

void test_tokens_units_and_one_decimal() {
  tokens(1000, "1.0K");
  tokens(1234, "1.2K");
  tokens(12345, "12.3K");
  tokens(1000000, "1.0M");
  tokens(12345678, "12.3M");
  tokens(1500000000LL, "1.50B");
  tokens(3456000000LL, "3.46B");
}

// Near-ties follow the ESP32 core's dtostrf, NOT printf (the comments give
// printf's answer). The expected strings come from compiling the real
// dtostrf on the host, not from reasoning about the algorithm: 2.25 looks like
// a tie that rounds up, but double noise makes it "2.2".
void test_tokens_near_ties_follow_dtostrf_not_printf() {
  tokens(1250, "1.3K");        // printf: 1.2
  tokens(1950, "2.0K");        // printf: 1.9
  tokens(2350, "2.3K");        // printf: 2.4
  tokens(2250000, "2.2M");     // same as printf, but not "2.3"
  tokens(1135000000LL, "1.13B");   // printf: 1.14
  tokens(1005000000LL, "1.00B");
}

void test_tokens_rounding_carries_into_a_new_digit() {
  tokens(999999, "1000.0K");  // below 1M stays in K
}

void test_btc_groups_thousands() {
  char b[FMT_BUF_MIN];
  struct { double p; const char* want; } rows[] = {
    {0, "0"}, {999, "999"}, {1000, "1,000"}, {82893, "82,893"},
    {82892.5, "82,893"}, {82892.4, "82,892"}, {1234567, "1,234,567"},
  };
  for (auto& r : rows) {
    fmtBtcBuf(b, sizeof(b), r.p);
    TEST_ASSERT_EQUAL_STRING(r.want, b);
  }
}

void test_btc_unknown_is_dashes() {
  char b[FMT_BUF_MIN];
  fmtBtcBuf(b, sizeof(b), -1);
  TEST_ASSERT_EQUAL_STRING("--", b);
}

void test_change_pct_whole_percent_always_signed() {
  char b[FMT_BUF_MIN];
  struct { double p; const char* want; } rows[] = {
    {5.4, "+5%"}, {5.5, "+6%"}, {-3.2, "-3%"}, {0, "+0%"}, {-0.4, "-0%"}, {123.4, "+123%"},
  };
  for (auto& r : rows) {
    fmtChangePctBuf(b, sizeof(b), r.p);
    TEST_ASSERT_EQUAL_STRING(r.want, b);
  }
}

void test_countdown() {
  char b[FMT_BUF_MIN];
  struct { long sec; const char* want; } rows[] = {
    {-1, ""}, {0, "0m"}, {29, "0m"}, {30, "1m"}, {42 * 60, "42m"},
    {3600, "1h 00m"}, {4 * 3600 + 3 * 60, "4h 03m"}, {5 * 3600, "5h 00m"},
  };
  for (auto& r : rows) {
    fmtCountdownBuf(b, sizeof(b), r.sec);
    TEST_ASSERT_EQUAL_STRING(r.want, b);
  }
}

void test_countdown_dhm() {
  char b[FMT_BUF_MIN];
  struct { long sec; const char* want; } rows[] = {
    {-1, ""}, {0, "0h 00m"}, {23 * 3600 + 59 * 60, "23h 59m"}, {86399, "1d 0h"},
    {86400, "1d 0h"}, {6 * 86400 + 16 * 3600, "6d 16h"}, {7 * 86400, "7d 0h"},
  };
  for (auto& r : rows) {
    fmtCountdownDHMBuf(b, sizeof(b), r.sec);
    TEST_ASSERT_EQUAL_STRING(r.want, b);
  }
}

void test_small_buffers_truncate_and_terminate() {
  char b[4];
  fmtBtcBuf(b, sizeof(b), 1234567);
  TEST_ASSERT_EQUAL_INT(3, (int)strlen(b));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tokens_plain_below_a_thousand);
  RUN_TEST(test_tokens_units_and_one_decimal);
  RUN_TEST(test_tokens_near_ties_follow_dtostrf_not_printf);
  RUN_TEST(test_tokens_rounding_carries_into_a_new_digit);
  RUN_TEST(test_btc_groups_thousands);
  RUN_TEST(test_btc_unknown_is_dashes);
  RUN_TEST(test_change_pct_whole_percent_always_signed);
  RUN_TEST(test_countdown);
  RUN_TEST(test_countdown_dhm);
  RUN_TEST(test_small_buffers_truncate_and_terminate);
  return UNITY_END();
}
