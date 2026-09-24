#include "fonts.h"
#include "state.h"

static lgfx::PointerWrapper fontData[FONT_COUNT];
static lgfx::VLWfont fontObj[FONT_COUNT];
static bool fontOk[FONT_COUNT];

void fontsBegin() {
  for (int i = 0; i < FONT_COUNT; i++) {
    fontData[i].set(FONT_VLW[i]);
    fontOk[i] = fontObj[i].loadFont(&fontData[i]);
    if (!fontOk[i]) Serial.printf("[fonts] font %d failed to load\n", i);
  }
}

int textW(FontId f, const char* s) {
  int w = 0;
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c < 0x20 || c > 0x7E) c = '?';
    w += FONT_ADV[f][c - 0x20];
  }
  return w;
}

int fontLineH(FontId f) { return FONT_METRIC[f][1]; }
int fontAscent(FontId f) { return FONT_METRIC[f][0]; }

int drawText(FontId f, int x, int y, const char* s, uint16_t color) {
  if (!fontOk[f]) return x;
  g->setFont(&fontObj[f]);
  g->setTextSize(1);
  g->setTextDatum(textdatum_t::top_left);
  g->setTextColor(color);  // no background colour: glyph alpha blends over what's there
  g->drawString(s, x, y);
  return x + textW(f, s);
}

int drawTextR(FontId f, int xRight, int y, const char* s, uint16_t color) {
  return drawText(f, xRight - textW(f, s), y, s, color);
}

int drawTextC(FontId f, int cx, int y, const char* s, uint16_t color) {
  return drawText(f, cx - textW(f, s) / 2, y, s, color);
}

void drawChar(FontId f, int x, int y, char c, uint16_t color) {
  char buf[2] = {c, 0};
  drawText(f, x, y, buf, color);
}
