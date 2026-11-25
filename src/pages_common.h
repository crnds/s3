// Drawing shared by the page files (pages_common.cpp). Prototypes first, then
// the Status page / Overview clock + calendar geometry both use.
#pragma once
#include "state.h"

extern const char* const WDAY_ABBR[7];
extern const char* const WDAY_FULL[7];
extern const char* const MON_ABBR[12];
void drawCard(int x, int y, int w, int h);
void drawBtcCard(int cardX = TOK_LAYOUT_COL_LEFT_X, int cardY = BTC_Y,
                 int cardW = TOK_LAYOUT_COL_LEFT_W, int cardH = BTC_CARD_H);
void drawAnalogClock(int cx, int cy, int r, int hour24, int minute, int second, bool haveReset, int resetHour24, int resetMinute);
void aaRingF(int cx, int cy, float ro, float ri, uint16_t c, uint8_t alpha);
void drawSectionLabel(int x, int y, const char* label);
int drawMeter(int x, int y, int w, int h, int percent, uint16_t color);
String fitText(FontId f, const char* s, int maxW);
void drawGearGlyph(int cx, int cy, uint16_t c, uint16_t bg);
void drawIconButtonPressed(int cx, int cy);
long liveResetsInSec(long baseSec);
uint16_t weatherIconColour(int code);
void weatherIconShape(int cx, int cy, int code, float k, uint16_t c);
void wxShade(int mx, int my, int r9, int& r, int& gg, int& b);
void compositeWeatherIcon(int cx, int cy, float k, uint16_t base);
void drawWeatherIcon(int cx, int cy, int code, float k);
int drawDegreeRing(int x, int capTop, uint16_t bg);
void drawTemp(FontId f, int cx, int y, int t, bool have);
bool drawShineStrip(int barIdx, int center, bool force);
bool limitCardsPage();
int sweepCenter(uint32_t nowMs);
String formatPaceDur(int currentPct, long elapsedSec, long remainingSec);
void drawPaceFlag(int x, int baseline, bool ahead, int currentPct, long elapsedSec, long remainingSec, int xMax);
String weekResetWeekdayLabel(long weekRem, const char* resets);
void drawLimitsColumn(bool overview = false);
void drawBtcLogo(int cx, int cy);
void drawTimerWedge(int cx, int cy, int r, float startAngle, float endAngle);
void aqiColors(int aqi, uint16_t& bg, uint16_t& fg);
int aqiBadgeW(int aqi);
void drawAqiBadge(int x, int y, int aqi);
bool isThaiHoliday(int year, int mon, int d);
int daysInMonth(int m, int yr);
void drawCalendarDay(int cx, int cy, int year, int mon, int d, int c, int mday);
int monthGridRows(int year, int mon, int mday, int wday);
void drawMonthGrid(int x, int y, int colW, int rowH, int rowH6, int year, int mon, int mday, int wday);
void drawWeatherNowAndHours(int x, int iy, int hours);
bool sessionResetTime(const struct tm& t, int& hour, int& minute);

// Status page: the clock hero (analog r 76 beside the digital readout, date and
// AQI badge), then the tappable weather strip.
static const int CLOCK_R = 76;
static const int CLOCK_CX = TOK_LAYOUT_COL_RIGHT_X + TOK_SPACE_CARD_PAD_HERO + CLOCK_R;          // 272
// Dial + digital time stack in the card: 4 top, dial 153, 3 gap, time 28, 4 bottom = 192.
static const int CLOCK_CY = CLOCK_CARD_Y + 4 + CLOCK_R;                                         // 88
static const int DIGITAL_Y = CLOCK_CY + CLOCK_R + 1 + 3;                                        // 168
// Two cards: the clock (dial + digital time, 8px pads) and, 8px to its right,
// the calendar (date + AQI row and the month grid, centred in the card).
static const int CAL_COL = 20, CAL_ROW = 20, CAL_ROW_TALL = 17, CAL_DISC_R = 8;
static const int CLOCK_CARD_W = 2 * TOK_SPACE_CARD_PAD_HERO + 2 * CLOCK_R + 1;                  // 169
static const int CAL_CARD_X = TOK_LAYOUT_COL_RIGHT_X + CLOCK_CARD_W + TOK_SPACE_GUTTER;         // 321
static const int CAL_CARD_W = TOK_LAYOUT_COL_RIGHT_X + TOK_LAYOUT_COL_RIGHT_W - CAL_CARD_X;     // 151
static const int CAL_GRID_X = CAL_CARD_X + (CAL_CARD_W - 7 * CAL_COL) / 2;                      // 326
// Calendar card, top to bottom: "Tue 6" (weekday accent, date primary, both
// type.title) at the card's top on one baseline (y 35; it may run under the
// sleep / battery-save buttons, which drawSystemCorner paints last), "Oct 2026"
// under it, a gap, the month grid, and the AQI bar pinned to the bottom pad.
// Left edge 329 is shared by the header rows and the bar.
static const int CAL_PAD = TOK_SPACE_SM;                                                         // 8
static const int CAL_X = CAL_CARD_X + CAL_PAD;                                                   // 329
static const int CAL_BAR_H = 20, CAL_BAR_W = CAL_CARD_W - 2 * CAL_PAD;                           // 135
static const int CAL_BAR_Y = CLOCK_CARD_Y + CLOCK_CARD_H - CAL_PAD - CAL_BAR_H;                  // 172
static const int CAL_BASE_Y = CLOCK_CARD_Y + 27;                                                 // 35
static const int CAL_GRID_Y = CLOCK_CARD_Y + 56;                                                 // 64


static const int WX_GLYPH_DY = 17 + 6 + 11, WX_TEMP_DY = 17 + 12 + 22, WX_ROWS_H = WX_TEMP_DY + 17;

// Row geometry: label (body) left, the percent right, a meter.md under it.
static const int ROW_H = 23 + TOK_SPACE_STACK_TIGHT + TOK_METER_MD;    // 35
static const int ROW_STEP = ROW_H + TOK_SPACE_STACK;                   // 43
