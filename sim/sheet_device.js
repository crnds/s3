// sim/sheet_device.js -- Device Stats sheet.

// ── DEVICE STATS SHEET ────────────────────────────────────────
function drawDeviceRow(y, label, detail, percent) {
  const x = TOK_LAYOUT_CONTENT_X0 + TOK_SPACE_CARD_PAD, r = TOK_LAYOUT_CONTENT_X1 - TOK_SPACE_CARD_PAD;
  drawText(TOK_TYPE_BODY, x, y, label, TOK_COLOR_TEXT_PRIMARY);
  const pct = percent >= 0 ? percent + '%' : '--';
  drawTextR(TOK_TYPE_NUMERAL_MD, r, y, pct, percent >= 0 ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  const dx = r - textW(TOK_TYPE_NUMERAL_MD, pct) - TOK_SPACE_SM;
  if (detail) drawTextR(TOK_TYPE_CAPTION, dx, y + fontAscent(TOK_TYPE_BODY) - fontAscent(TOK_TYPE_CAPTION), detail, TOK_COLOR_TEXT_SECONDARY);
  drawMeter(x, y + 23 + TOK_SPACE_STACK_TIGHT, r - x, TOK_METER_MD, percent,
            percent >= 80 ? TOK_COLOR_STATUS_ERROR : TOK_COLOR_DATA_SYSTEM);
}
function drawDevicePage() {
  drawModalHeader(false, 'Device stats', NAV.pressedId === 'close');
  const cardY = TOK_LAYOUT_HEADER_CONTENT_Y;
  drawCard(TOK_LAYOUT_CONTENT_X0, cardY, TOK_LAYOUT_CONTENT_W, 5 * ROW_H + 4 * TOK_SPACE_STACK + 2 * TOK_SPACE_CARD_PAD);
  let y = cardY + TOK_SPACE_CARD_PAD;
  const pct = (u, t) => Math.round(u / t * 100);
  drawDeviceRow(y, 'CPU', 'Render loop duty cycle', Math.round(STATE.cpuPercentAvg)); y += ROW_STEP;
  drawDeviceRow(y, 'Flash (app partition)', fmtKB(FLASH_USED_BYTES) + ' / ' + fmtKB(FLASH_TOTAL_BYTES), pct(FLASH_USED_BYTES, FLASH_TOTAL_BYTES)); y += ROW_STEP;
  drawDeviceRow(y, 'Internal RAM', fmtKB(RAM_USED_BYTES) + ' / ' + fmtKB(RAM_TOTAL_BYTES), pct(RAM_USED_BYTES, RAM_TOTAL_BYTES)); y += ROW_STEP;
  drawDeviceRow(y, 'PSRAM', fmtKB(PSRAM_USED_BYTES) + ' / ' + fmtKB(PSRAM_TOTAL_BYTES), pct(PSRAM_USED_BYTES, PSRAM_TOTAL_BYTES)); y += ROW_STEP;
  const sdMissing = STATE.simSdMissing;
  const sdPct = sdMissing ? -1 : pct(SD_USED_BYTES, SD_TOTAL_BYTES);
  drawDeviceRow(y, 'SD card', sdPct >= 0 ? fmtGB(SD_USED_BYTES) + ' / ' + fmtGB(SD_TOTAL_BYTES) : 'Not found', sdPct);
}
