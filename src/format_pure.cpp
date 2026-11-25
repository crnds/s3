// Pure formatting (format_pure.h). Moved out of format.cpp unchanged in
// behaviour: the one subtlety is fixedBuf(), see below.
#include "format_pure.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// What Arduino's String(double, prec) produces: a copy of the ESP32 core's
// dtostrf (cores/esp32/stdlib_noniso.c) with the padding dropped (the callers'
// values are >= 1.0, so the core's width of prec+2 never padded). It is NOT
// snprintf("%.*f"): dtostrf adds half a unit in the last place and then
// truncates digit by digit in double arithmetic, so near-ties land differently
// from printf's exact rounding (1.25K -> "1.3" where printf gives "1.2";
// 2.35K -> "2.3" where printf gives "2.4"). Using snprintf would have changed
// digits on screen, so the algorithm is kept as is. Checked against the real
// core function on 30M values (see tests/native/test_format).
static void fixedBuf(char* out, size_t n, double number, unsigned prec) {
  char tmp[40];
  char* o = tmp;
  bool negative = false;
  if (isnan(number)) { snprintf(out, n, "nan"); return; }
  if (isinf(number)) { snprintf(out, n, "inf"); return; }
  if (number < 0.0) { negative = true; number = -number; }

  double rounding = 2.0;
  for (unsigned i = 0; i < prec; ++i) rounding *= 10.0;
  number += 1.0 / rounding;

  double tenpow = 1.0;
  unsigned digitcount = 1;
  while (number >= 10.0 * tenpow && digitcount < 20) {
    tenpow *= 10.0;
    digitcount++;
  }
  number /= tenpow;

  if (negative) *o++ = '-';
  digitcount += prec;
  while (digitcount-- > 0) {
    int digit = (int)number;
    if (digit > 9) digit = 9;
    *o++ = (char)('0' | digit);
    if (digitcount == prec && prec > 0) *o++ = '.';
    number -= digit;
    number *= 10.0;
  }
  *o = 0;
  snprintf(out, n, "%s", tmp);
}

void fmtTokensBuf(char* out, size_t n, int64_t t) {
  char num[40];
  if (t >= 1000000000) {
    fixedBuf(num, sizeof(num), (double)t / 1000000000.0, 2);
    snprintf(out, n, "%sB", num);
  } else if (t >= 1000000) {
    fixedBuf(num, sizeof(num), (double)t / 1000000.0, 1);
    snprintf(out, n, "%sM", num);
  } else if (t >= 1000) {
    fixedBuf(num, sizeof(num), (double)t / 1000.0, 1);
    snprintf(out, n, "%sK", num);
  } else {
    snprintf(out, n, "%ld", (long)t);
  }
}

void fmtBtcBuf(char* out, size_t n, double p) {
  if (p < 0) { snprintf(out, n, "--"); return; }
  char digits[24];
  snprintf(digits, sizeof(digits), "%ld", (long)(p + 0.5));
  char grouped[32];
  size_t o = 0;
  int len = (int)strlen(digits);
  for (int i = 0; i < len && o + 2 < sizeof(grouped); i++) {
    grouped[o++] = digits[i];
    int rem = len - 1 - i;
    if (rem > 0 && rem % 3 == 0) grouped[o++] = ',';
  }
  grouped[o] = 0;
  snprintf(out, n, "%s", grouped);
}

void fmtChangePctBuf(char* out, size_t n, double pct) {
  long whole = (long)(fabs(pct) + 0.5);
  snprintf(out, n, "%s%ld%%", pct < 0 ? "-" : "+", whole);
}

void fmtCountdownBuf(char* out, size_t n, long sec) {
  if (sec < 0) { if (n) out[0] = 0; return; }
  long m = (sec + 30) / 60;  // round to nearest minute
  long h = m / 60;
  m %= 60;
  if (h > 0) snprintf(out, n, "%ldh %02ldm", h, m);
  else snprintf(out, n, "%ldm", m);
}

// Day-aware countdown for windows that can span multiple days (the weekly
// reset). >=1 day: "1d 12h" (days + hours only). <1 day: "23h 59m", same
// shape as fmtCountdown. No colon: it would read as a clock time (design.md
// 5.3 rule 9). Twin of simulator-s3.html's fmtCountdownDHM.
void fmtCountdownDHMBuf(char* out, size_t n, long sec) {
  if (sec < 0) { if (n) out[0] = 0; return; }
  long totalMin = (sec + 30) / 60;  // round to nearest minute
  long days = totalMin / 1440;
  if (days > 0) {
    long hours = (totalMin % 1440) / 60;
    snprintf(out, n, "%ldd %ldh", days, hours);
  } else {
    snprintf(out, n, "%ldh %02ldm", totalMin / 60, totalMin % 60);
  }
}
