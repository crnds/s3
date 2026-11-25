// sim/state.js -- storage keys, device figures and STATE (the firmware UsageState + main.cpp globals).

const WEATHER_HOURLY_N = 11;
const WEATHER_DAILY_N = 5;
const LAST_USAGE_KEY = 's3dashboard_v1_lastusage';
const LAST_WEATHER_KEY = 's3dashboard_v1_weather';
const BRIGHTNESS_KEY = 's3dashboard_v1_brightness';
const BOOT_PAGE_KEY = 's3dashboard_v1_bootpage';
const LAST_PAGE_KEY = 's3dashboard_v1_lastpage';
const PAGES_V2_KEY = 's3dashboard_v1_pagesv2';
const CAT_SHUFFLE_KEY = 's3dashboard_v1_catshuffle';
const NIGHT_MODE_KEY = 's3dashboard_v1_nightmode';
const ROTATION_KEY = 's3dashboard_v1_rotation';
const SHOW_COUNTDOWN_KEY = 's3dashboard_v1_showcountdown';
const BATTERY_SAVE_KEY = 's3dashboard_v1_batterysave';
const SHOW_AQI_KEY = 's3dashboard_v1_showaqi';
const HOURLY_FLASH_KEY = 's3dashboard_v1_hourlyflash';
const SHOW_PROGRESS_KEY = 's3dashboard_v1_showprogress';
const ALERT_VOL_KEY = 's3dashboard_v1_soundlevel';
const TAP_VOL_KEY = 's3dashboard_v1_taplevel';
const HOURLY_CHIME_KEY = 's3dashboard_v1_hourlychime';
const CLAUDE_DING_KEY = 's3dashboard_v1_claudeding';
const SOUND_ALERTS_KEY = 's3dashboard_v1_soundalerts';
const MUTE_SLEEP_KEY = 's3dashboard_v1_mutesleep';
const MUTE_NIGHT_KEY = 's3dashboard_v1_mutenight';
const OTA_AUTO_KEY = 's3dashboard_v1_otaauto';
const SOUND_VOL_MAX = 7;
const REDUCE_MOTION_KEY = 's3dashboard_v1_reducemotion';
const HIGH_CONTRAST_KEY = 's3dashboard_v1_highcontrast';
const LIGHT_MODE_KEY = 's3dashboard_v1_lightmode';
const BATTERY_SAVE_POLL_SEC = 120;

// localStorage can throw (private windows, blocked site data) -- every
// access goes through these, and the sim works without it.
function lsGet(k) { try { return localStorage.getItem(k); } catch { return null; } }
function lsSet(k, v) { try { localStorage.setItem(k, v); } catch { /* best-effort */ } }

// On-device figures with no browser equivalent (they only change on a
// reflash). Flash mirrors format.cpp's flashPercent(): used = the image
// (firmware.bin), total = used + getFreeSketchSpace() (the other 0x640000 OTA
// slot of default_16MB.csv). RAM/PSRAM/SD are from an earlier board boot.
const FLASH_USED_BYTES = 2981920, FLASH_TOTAL_BYTES = 9535520;
const RAM_USED_BYTES = 158720, RAM_TOTAL_BYTES = 348330;
const PSRAM_USED_BYTES = 1301504, PSRAM_TOTAL_BYTES = 8388608;
const SD_USED_BYTES = 2483027968, SD_TOTAL_BYTES = 7948206080;

// ── STATE (mirroring the firmware UsageState struct + main.cpp globals) ──
const STATE = {
  screenSleeping: false, sleepStartMs: 0,
  projects: [], trend: [0,0,0,0,0,0,0],
  haveData: false,
  cpuPercentAvg: 0,
  sessionPercent: -1, sessionResets: '', sessionResetsInSec: -1,
  weekPercent: -1, weekResets: '', weekResetsInSec: -1,
  ctxTokens: -1, ctxPercent: -1,
  weekModelPercent: -1, weekModelName: '', weekModelResets: '',
  creditsUsed: -1, creditsLimit: -1, creditsPercent: -1,
  lastFetchOkMs: 0,
  btcPrice: -1, btcChangePct: null, aqi: -1,
  weatherTempC: -999, weatherCode: -1, weatherHigh: -999, weatherLow: -999,
  weatherCondition: '', weatherHourly: [], weatherDaily: [],
  note: '', noteSize: 1,
  weatherPageOpen: false, devicePageOpen: false,
  lastPollMs: 0,
  lastFetchSuccessMs: 0,
  currentPage: 0,
  connected: false, wifiOk: false,
  pollTimer: null,
  shiftIdx: 0, lastShiftMs: 0, shiftX: 0, shiftY: 0, shiftDirty: false,
  settingsScreen: 'off',  // 'off' | 'list' | 'leaf' -- the firmware's SettingsScreen
  settingsScrollOffset: 0, settingsLeafIndex: -1,
  confirmArmedMs: 0, confirmArmedRow: -1,
  cfgBrightness: 200, cfgBootPage: 0, cfgLastPage: 0,
  catShuffleSec: 0,
  cfgNightModeOn: false, nightDimActive: false,
  cfgBatterySaveMode: 2, serverBatterySave: false,
  cfgShowCountdown: true, cfgShowAqi: true, cfgHourlyFlash: true, cfgShowProgress: true,
  cfgReduceMotion: false, cfgHighContrast: false, cfgLightMode: false,
  // Speaker (firmware-only: the sim keeps the settings and stays silent).
  cfgTapVol: 2, cfgAlertVol: 4, cfgHourlyChime: true, cfgClaudeDing: true, cfgSoundAlerts: true,
  cfgMuteSleep: true, cfgMuteNight: true,
  cfgOtaAuto: true, firmwareOffer: null,  // Settings > Firmware (ota.cpp); offer = /api/usage firmware.s3
  settingsSection: -1,  // -1 = main list, else the SETTINGS row whose children the list shows
  cfgScreenRotation: 1,
  pollOkSeq: 0,
};
let motionTimeScale = 1;
