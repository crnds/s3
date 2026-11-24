#pragma once
// Number and time formatting with no Arduino dependency, so `pio test -e
// native` can pin the exact text the board draws (roadmap O4). Each writes a
// NUL-terminated string into `out` (at least FMT_BUF_MIN bytes). format.cpp
// wraps them in the String-returning fmt*() the pages call, and
// simulator-s3.html's formatting block is their twin.
#include <stddef.h>
#include <stdint.h>

const size_t FMT_BUF_MIN = 24;

void fmtTokensBuf(char* out, size_t n, int64_t t);      // "812" "1.2K" "3.4M" "1.25B"
void fmtBtcBuf(char* out, size_t n, double p);          // "82,893"; "--" when p < 0
void fmtChangePctBuf(char* out, size_t n, double pct);  // "+5%" / "-3%" (whole percent)
void fmtCountdownBuf(char* out, size_t n, long sec);    // "4h 03m" / "42m"; "" when sec < 0
void fmtCountdownDHMBuf(char* out, size_t n, long sec); // "6d 16h" / "4h 03m"; "" when sec < 0
