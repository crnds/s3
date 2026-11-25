// Serial debug keys (serial_debug.cpp); loop() feeds it one byte at a time.
#pragma once
void serialCommand(char c);
extern uint32_t hourlyTestStartMs;  // serial 'h': when the hourly signal test began
