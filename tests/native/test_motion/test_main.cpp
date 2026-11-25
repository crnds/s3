// motion_math.cpp: spring integrator, release velocity, momentum projection,
// rubber band. The simulator's MOTION block is the twin of all of these.
#include <unity.h>

#include <math.h>

#include <initializer_list>

#include "motion.h"

void setUp(void) { motionTimeScale = 1.0f; }
void tearDown(void) { motionTimeScale = 1.0f; }

void test_spring_to_sets_target_and_period() {
  Spring s;
  springTo(s, 100, 100, 300);
  TEST_ASSERT_EQUAL_FLOAT(100, s.target);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.zeta);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f * 3.14159265f / 0.3f, s.w);
  TEST_ASSERT_TRUE(s.active);
}

void test_slow_motion_scales_the_period() {
  Spring a, b;
  springTo(a, 1, 100, 300);
  motionTimeScale = 10.0f;
  springTo(b, 1, 100, 300);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, a.w / 10.0f, b.w);
}

void test_inactive_spring_does_not_move() {
  Spring s;
  s.x = 5;
  TEST_ASSERT_FALSE(springStep(s, 0.016f));
  TEST_ASSERT_EQUAL_FLOAT(5, s.x);
}

void test_critically_damped_spring_settles_on_target_without_overshoot() {
  Spring s;
  springTo(s, 100, 100, 300);
  float peak = 0;
  int steps = 0;
  while (springStep(s, 0.016f) && steps < 400) {
    if (s.x > peak) peak = s.x;
    steps++;
  }
  TEST_ASSERT_FALSE(s.active);
  TEST_ASSERT_TRUE(steps < 400);
  TEST_ASSERT_EQUAL_FLOAT(100, s.x);  // snapped
  TEST_ASSERT_EQUAL_FLOAT(0, s.v);
  TEST_ASSERT_TRUE(peak <= 100.5f);
}

void test_underdamped_spring_overshoots_then_settles() {
  Spring s;
  springTo(s, 100, 60, 400);
  float peak = 0;
  int steps = 0;
  while (springStep(s, 0.016f) && steps < 600) {
    if (s.x > peak) peak = s.x;
    steps++;
  }
  TEST_ASSERT_TRUE(peak > 100.5f);
  TEST_ASSERT_FALSE(s.active);
  TEST_ASSERT_EQUAL_FLOAT(100, s.x);
}

void test_step_time_is_clamped_to_50ms() {
  Spring a, b;
  springTo(a, 100, 100, 300);
  springTo(b, 100, 100, 300);
  springStep(a, 5.0f);
  springStep(b, 0.05f);
  TEST_ASSERT_EQUAL_FLOAT(b.x, a.x);
  TEST_ASSERT_EQUAL_FLOAT(b.v, a.v);
}

void test_retargeting_keeps_position_and_velocity() {
  Spring s;
  springTo(s, 100, 100, 300);
  for (int i = 0; i < 5; i++) springStep(s, 0.016f);
  float x = s.x, v = s.v;
  springTo(s, 0, 100, 300);
  TEST_ASSERT_EQUAL_FLOAT(x, s.x);
  TEST_ASSERT_EQUAL_FLOAT(v, s.v);
  TEST_ASSERT_EQUAL_FLOAT(0, s.target);
}

static VelocityTracker feed(std::initializer_list<int32_t> xs, uint32_t t0, uint32_t dt) {
  VelocityTracker vt;
  velocityReset(vt);
  uint32_t t = t0;
  for (int32_t x : xs) { velocityAdd(vt, x, 0, t); t += dt; }
  return vt;
}

void test_release_velocity_from_the_window() {
  VelocityTracker vt = feed({0, 10, 20, 30}, 1000, 10);   // newest sample at t=1030
  float vx, vy;
  velocityGet(vt, 1030, vx, vy);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1000.0f, vx);
  TEST_ASSERT_EQUAL_FLOAT(0, vy);
}

void test_paused_finger_means_no_velocity() {
  VelocityTracker vt = feed({0, 10, 20, 30}, 1000, 10);
  float vx, vy;
  velocityGet(vt, 1030 + 51, vx, vy);
  TEST_ASSERT_EQUAL_FLOAT(0, vx);
  velocityGet(vt, 1030 + 50, vx, vy);   // exactly 50 ms is still a flick
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1000.0f, vx);
}

void test_too_few_or_simultaneous_samples_give_zero() {
  float vx, vy;
  VelocityTracker one = feed({5}, 1000, 10);
  velocityGet(one, 1000, vx, vy);
  TEST_ASSERT_EQUAL_FLOAT(0, vx);
  VelocityTracker same;
  velocityReset(same);
  velocityAdd(same, 0, 0, 1000);
  velocityAdd(same, 50, 0, 1000);
  velocityGet(same, 1000, vx, vy);
  TEST_ASSERT_EQUAL_FLOAT(0, vx);
}

void test_old_samples_fall_outside_the_100ms_window() {
  VelocityTracker vt;
  velocityReset(vt);
  velocityAdd(vt, 0, 0, 1000);
  velocityAdd(vt, 100, 0, 1150);
  velocityAdd(vt, 110, 0, 1160);
  float vx, vy;
  velocityGet(vt, 1160, vx, vy);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1000.0f, vx);  // (110-100)/10ms, not from t=1000
}

void test_velocity_is_clamped_both_ways() {
  float vx, vy;
  VelocityTracker up = feed({0, 1000}, 1000, 10);
  velocityGet(up, 1010, vx, vy);
  TEST_ASSERT_EQUAL_FLOAT(4000, vx);
  VelocityTracker down = feed({1000, 0}, 1000, 10);
  velocityGet(down, 1010, vx, vy);
  TEST_ASSERT_EQUAL_FLOAT(-4000, vx);
}

void test_ring_keeps_the_newest_four() {
  // Seven samples 10 ms apart; the first three (x = -9999) must have been
  // overwritten, so the span is t=1030..1060 and the velocity 80 px / 30 ms.
  VelocityTracker vt = feed({-9999, -9999, -9999, 0, 0, 40, 80}, 1000, 10);
  float vx, vy;
  velocityGet(vt, 1060, vx, vy);
  TEST_ASSERT_EQUAL_INT(4, vt.n);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 2666.667f, vx);
}

void test_projection_and_rubber_band() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 199.0f, projectDistance(1000.0f));
  TEST_ASSERT_EQUAL_FLOAT(0, projectDistance(0));
  TEST_ASSERT_EQUAL_FLOAT(0, rubberband(0, 320));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 46.9333f, rubberband(100, 320));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -46.9333f, rubberband(-100, 320));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_spring_to_sets_target_and_period);
  RUN_TEST(test_slow_motion_scales_the_period);
  RUN_TEST(test_inactive_spring_does_not_move);
  RUN_TEST(test_critically_damped_spring_settles_on_target_without_overshoot);
  RUN_TEST(test_underdamped_spring_overshoots_then_settles);
  RUN_TEST(test_step_time_is_clamped_to_50ms);
  RUN_TEST(test_retargeting_keeps_position_and_velocity);
  RUN_TEST(test_release_velocity_from_the_window);
  RUN_TEST(test_paused_finger_means_no_velocity);
  RUN_TEST(test_too_few_or_simultaneous_samples_give_zero);
  RUN_TEST(test_old_samples_fall_outside_the_100ms_window);
  RUN_TEST(test_velocity_is_clamped_both_ways);
  RUN_TEST(test_ring_keeps_the_newest_four);
  RUN_TEST(test_projection_and_rubber_band);
  return UNITY_END();
}
