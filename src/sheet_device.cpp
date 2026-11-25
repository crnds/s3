// Device Stats sheet.
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"

// ── DEVICE STATS SHEET ─────────────────────────────────────
// Modal header ("Device stats"), then one card of five data rows: label,
// detail (caption) + percent (headline), a data.system meter (status.error
// at >= 80%, with the % beside it so colour is never the only cue).
static void drawDeviceRow(int y, const char* label, const String& detail, int percent) {
  const int x = TOK_LAYOUT_CONTENT_X0 + TOK_SPACE_CARD_PAD, r = TOK_LAYOUT_CONTENT_X1 - TOK_SPACE_CARD_PAD;
  drawText(TOK_TYPE_BODY, x, y, label, TOK_COLOR_TEXT_PRIMARY);
  String pct = percent >= 0 ? String(percent) + "%" : String("--");
  drawTextR(TOK_TYPE_NUMERAL_MD, r, y, pct, percent >= 0 ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  int dx = r - textW(TOK_TYPE_NUMERAL_MD, pct) - TOK_SPACE_SM;
  if (detail.length())
    drawTextR(TOK_TYPE_CAPTION, dx, y + fontAscent(TOK_TYPE_BODY) - fontAscent(TOK_TYPE_CAPTION), detail,
              TOK_COLOR_TEXT_SECONDARY);
  drawMeter(x, y + TOK_LINE_BODY + TOK_SPACE_STACK_TIGHT, r - x, TOK_METER_MD, percent,
            percent >= 80 ? TOK_COLOR_STATUS_ERROR : TOK_COLOR_DATA_SYSTEM);
}

void drawDevicePage() {
  drawModalHeader(false, "Device stats", pressedId == PRESS_CLOSE);
  const int cardY = TOK_LAYOUT_HEADER_CONTENT_Y;
  drawCard(TOK_LAYOUT_CONTENT_X0, cardY, TOK_LAYOUT_CONTENT_W, 5 * ROW_H + 4 * TOK_SPACE_STACK + 2 * TOK_SPACE_CARD_PAD);
  int y = cardY + TOK_SPACE_CARD_PAD;

  int cpuInt = (int)(cpuPercentAvg + 0.5f);
  drawDeviceRow(y, "CPU", "Render loop duty cycle", cpuInt);
  y += ROW_STEP;
  uint32_t flashUsed, flashTotal;
  int flashPct = flashPercent(flashUsed, flashTotal);
  drawDeviceRow(y, "Flash (app partition)", fmtKB(flashUsed) + " / " + fmtKB(flashTotal), flashPct);
  y += ROW_STEP;
  uint32_t ramUsed, ramTotal;
  int ramPct = staticRamPercent(ramUsed, ramTotal);
  drawDeviceRow(y, "Internal RAM", fmtKB(ramUsed) + " / " + fmtKB(ramTotal), ramPct);
  y += ROW_STEP;
  uint32_t psUsed, psTotal;
  int psPct = psramPercent(psUsed, psTotal);
  drawDeviceRow(y, "PSRAM", fmtKB(psUsed) + " / " + fmtKB(psTotal), psPct);
  y += ROW_STEP;
  uint64_t sdUsed = 0, sdTotal = 0;
  int sdPct = cachedSdCapacityPercent(sdUsed, sdTotal);
  drawDeviceRow(y, "SD card", sdPct >= 0 ? fmtGB(sdUsed) + " / " + fmtGB(sdTotal) : String("Not found"), sdPct);
}
