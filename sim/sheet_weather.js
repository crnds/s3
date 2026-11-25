// sim/sheet_weather.js -- Weather sheet.

// ── WEATHER SHEET ─────────────────────────────────────────────
const WX_HERO_X = TOK_LAYOUT_CONTENT_X0, WX_HERO_Y = TOK_LAYOUT_CONTENT_Y0;
const WX_HERO_W = TOK_LAYOUT_CONTENT_X1 - WX_HERO_X, WX_HERO_H = 64;
const WX_HOURLY_Y = WX_HERO_Y + WX_HERO_H + TOK_SPACE_GUTTER;
const WX_HOURLY_H = 17 + TOK_SPACE_XS + 28 + TOK_SPACE_XS + 23 + 2 * TOK_SPACE_CARD_PAD_COMPACT_V;
const WX_DAILY_Y = WX_HOURLY_Y + WX_HOURLY_H + TOK_SPACE_GUTTER;
const WX_DAILY_H = TOK_LAYOUT_OVERLAY_CONTENT_Y1 - WX_DAILY_Y;
function drawWeatherPage() {
  drawCard(WX_HERO_X, WX_HERO_Y, WX_HERO_W, WX_HERO_H);
  const hx = WX_HERO_X + TOK_SPACE_CARD_PAD_HERO;
  drawWeatherIcon(hx + 18, WX_HERO_Y + WX_HERO_H / 2, STATE.weatherCode, 2.0);
  const tempY = WX_HERO_Y + Math.trunc((WX_HERO_H - fontLineH(TOK_TYPE_DISPLAY)) / 2);
  const tempX = hx + 36 + TOK_SPACE_SM;
  let metaX;
  if (STATE.weatherTempC > -900) {
    const x = drawText(TOK_TYPE_NUMERAL_HERO, tempX, tempY, String(Math.round(STATE.weatherTempC)), TOK_COLOR_TEXT_PRIMARY);
    const capTop = tempY + fontAscent(TOK_TYPE_DISPLAY) - 29;
    metaX = drawDegreeRing(x + TOK_SPACE_HAIR, capTop, TOK_COLOR_SURFACE_CARD) + TOK_SPACE_LG;
  } else {
    metaX = drawText(TOK_TYPE_NUMERAL_HERO, tempX, tempY, '--', TOK_COLOR_TEXT_TERTIARY) + TOK_SPACE_LG;
  }
  const haveAqi = STATE.cfgShowAqi && STATE.aqi >= 0;
  const badgeW = haveAqi ? aqiBadgeW(STATE.aqi) : 0;
  const badgeX = TOK_CORNER_INK_X1 - badgeW;
  const metaMaxX = haveAqi ? badgeX - TOK_SPACE_SM : TOK_CORNER_INK_X1;
  const metaY = WX_HERO_Y + Math.trunc((WX_HERO_H - (23 + TOK_SPACE_XS + 23)) / 2);
  drawText(TOK_TYPE_HEADLINE, metaX, metaY, fitText(TOK_TYPE_HEADLINE, STATE.weatherCondition || '--', metaMaxX - metaX),
           STATE.weatherCondition ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  {
    const y = metaY + 23 + TOK_SPACE_XS;
    let x = drawText(TOK_TYPE_BODY, metaX, y, 'H ', TOK_COLOR_TEXT_SECONDARY);
    x = STATE.weatherHigh > -900 ? drawText(TOK_TYPE_BODY, x, y, String(STATE.weatherHigh), TOK_COLOR_TEXT_PRIMARY)
                                 : drawText(TOK_TYPE_BODY, x, y, '--', TOK_COLOR_TEXT_TERTIARY);
    x = drawText(TOK_TYPE_BODY, x + TOK_SPACE_LG, y, 'L ', TOK_COLOR_TEXT_SECONDARY);
    if (STATE.weatherLow > -900) drawText(TOK_TYPE_BODY, x, y, String(STATE.weatherLow), TOK_COLOR_TEXT_PRIMARY);
    else drawText(TOK_TYPE_BODY, x, y, '--', TOK_COLOR_TEXT_TERTIARY);
  }
  if (haveAqi) drawAqiBadge(badgeX, WX_HERO_Y + Math.trunc((WX_HERO_H - TOK_BADGE_H) / 2), STATE.aqi);

  drawCard(TOK_LAYOUT_CONTENT_X0, WX_HOURLY_Y, TOK_LAYOUT_CONTENT_W, WX_HOURLY_H);
  const innerX = TOK_LAYOUT_CONTENT_X0 + TOK_SPACE_CARD_PAD_COMPACT_H;
  const innerW = TOK_LAYOUT_CONTENT_W - 2 * TOK_SPACE_CARD_PAD_COMPACT_H;
  const slotW = Math.trunc(innerW / WEATHER_HOURLY_N);
  const slotX0 = innerX + Math.trunc((innerW - slotW * WEATHER_HOURLY_N) / 2);
  const hy = WX_HOURLY_Y + TOK_SPACE_CARD_PAD_COMPACT_V;
  for (let i = 0; i < WEATHER_HOURLY_N; i++) {
    const cx = slotX0 + i * slotW + Math.trunc(slotW / 2);
    const have = i < STATE.weatherHourly.length;
    const hr = have ? STATE.weatherHourly[i] : null;
    const okHour = have && hr.h >= 0;
    drawTextC(TOK_TYPE_NUMERAL_SM, cx, hy, okHour ? String(hr.h).padStart(2, '0') : '--',
              i === 0 ? TOK_COLOR_ACCENT : (have ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_TERTIARY));
    drawWeatherIcon(cx, hy + 17 + TOK_SPACE_XS + 14, have ? hr.code : -1, 1.5);
    const ty = hy + 17 + TOK_SPACE_XS + 28 + TOK_SPACE_XS;
    if (have) drawTextC(TOK_TYPE_BODY, cx, ty, String(hr.tempC), TOK_COLOR_TEXT_PRIMARY);
    else drawTextC(TOK_TYPE_BODY, cx, ty, '--', TOK_COLOR_TEXT_TERTIARY);
  }

  drawCard(TOK_LAYOUT_CONTENT_X0, WX_DAILY_Y, TOK_LAYOUT_CONTENT_W, WX_DAILY_H);
  let minT = 100, maxT = -100;
  for (const d of STATE.weatherDaily) { minT = Math.min(minT, d.low); maxT = Math.max(maxT, d.high); }
  if (maxT <= minT) { minT = 20; maxT = 40; }
  const dayX = innerX, iconCx = innerX + 64, lowR = innerX + 120;
  const barX = TOK_LAYOUT_ALIGN_X - 36, barW = 260, highX = barX + barW + TOK_SPACE_SM;
  const rowH = fontLineH(TOK_TYPE_BODY);
  const dy0 = WX_DAILY_Y + TOK_SPACE_CARD_PAD_COMPACT_V;
  for (let i = 0; i < WEATHER_DAILY_N; i++) {
    const y = dy0 + i * rowH;
    const have = i < STATE.weatherDaily.length;
    const d = have ? STATE.weatherDaily[i] : null;
    const haveWd = have && d.wd >= 0 && d.wd < 7;
    drawText(TOK_TYPE_BODY, dayX, y, haveWd ? WDAY_ABBR[d.wd] : '--',
             !haveWd ? TOK_COLOR_TEXT_TERTIARY : (i === 0 ? TOK_COLOR_ACCENT : TOK_COLOR_TEXT_PRIMARY));
    drawWeatherIcon(iconCx, y + Math.trunc(rowH / 2), have ? d.code : -1, 1.0);
    if (have) drawTextR(TOK_TYPE_BODY, lowR, y, String(d.low), TOK_COLOR_TEXT_SECONDARY);
    else drawTextR(TOK_TYPE_BODY, lowR, y, '--', TOK_COLOR_TEXT_TERTIARY);
    const barY = y + 12 - TOK_METER_MD / 2;
    gfx.fillSmoothRoundRect(barX, barY, barW, TOK_METER_MD, TOK_METER_MD / 2, TOK_COLOR_FILL_TRACK);
    if (have) {
      const span = maxT - minT;
      const x0 = barX + Math.trunc((d.low - minT) / span * barW);
      let x1 = barX + Math.trunc((d.high - minT) / span * barW);
      if (x1 - x0 < TOK_METER_MD) x1 = x0 + TOK_METER_MD;
      gfx.fillSmoothRoundRect(x0, barY, x1 - x0, TOK_METER_MD, TOK_METER_MD / 2, TOK_COLOR_ACCENT);
    }
    if (have) drawText(TOK_TYPE_BODY, highX, y, String(d.high), TOK_COLOR_TEXT_PRIMARY);
    else drawText(TOK_TYPE_BODY, highX, y, '--', TOK_COLOR_TEXT_TERTIARY);
  }
}
