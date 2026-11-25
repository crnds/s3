// Screen sleep state loop() reads (screen_sleep.cpp); enter/exitScreenSleep are in nav.h.
#pragma once
#include <stdint.h>
const uint32_t SLEEP_LOOP_MS = 50;  // loop() period while asleep: touch only
extern uint32_t sleepStartMs;
extern bool swallowUntilUp;         // the waking press never reaches the screen
