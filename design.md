# S3 Dashboard — Design System

The single source of truth for the visual and interaction design of the
JC3248W535EN (ESP32-S3, 480×320) firmware UI and its browser twin,
`simulator-s3.html`.

| | |
|---|---|
| **Status** | v1.2: migrated. Phases 1–6 of §15.4 are in the firmware and the simulator (tokens in `src/tokens.h`, navigation and motion in `src/nav.cpp` / `src/motion.cpp`). §15.5 lists where the implementation had to differ from the spec; §18.4 lists the board-only checks still open. |
| **Applies to** | `src/pages.cpp`, `src/settings.cpp`, `src/gif_player.cpp` (overlays and placeholders), `src/ap_setup.cpp`, `src/sd_store.cpp` (splash), and their twins in `simulator-s3.html`. |
| **Does not apply to** | `server.html` and `note.html`. They are browser pages and follow `~/CLAUDE.md`'s static-web conventions. The one exception is `note.html`'s board preview, which must use the note-pane tokens (§11.19). |
| **Precedence** | `CLAUDE.md` governs *mechanics*: parity, fonts pipeline, locks, the present pipeline. This file governs *design*: which colour, size, spacing, component and behaviour to use. If a rule here can't be implemented without breaking a `CLAUDE.md` invariant, the invariant wins and this file gets fixed. |

**How to use it.** Build a new screen only from the tokens (§3) and
components (§11). Run the checklist in §16 before calling it done. If you need a
value that isn't here, add a token here first, then use it. Don't invent it
in the screen.

---

## Contents

1. [The device](#1-the-device--hardware-ground-truth)
2. [Principles](#2-principles)
3. [Tokens: naming and code mapping](#3-tokens-naming-and-code-mapping)
4. [Colour](#4-colour)
5. [Typography](#5-typography)
6. [Spacing](#6-spacing)
7. [Layout and proportion](#7-layout-and-proportion)
8. [Shape, stroke and depth](#8-shape-stroke-and-depth)
9. [Touch and gestures](#9-touch-and-gestures)
10. [Iconography](#10-iconography)
11. [Components](#11-components)
12. [Motion](#12-motion)
13. [Patterns: navigation, feedback, states](#13-patterns-navigation-feedback-states)
14. [Hardware budget and trade-offs](#14-hardware-budget-and-trade-offs)
15. [Audit of the current UI, and migration](#15-audit-of-the-current-ui-and-migration)
16. [New-screen checklist](#16-new-screen-checklist)
17. [Process](#17-process)
18. [Appendix](#18-appendix)

---

## 1. The device: hardware ground truth

Every rule in this document follows from one of the facts below. When a rule
looks arbitrary, look for its reason here.

| Fact | Value | Source | Design consequence |
|---|---|---|---|
| Resolution | 480 × 320, landscape (panel is 320×480 portrait; firmware rotates in software) | spec, `display.cpp` | One fixed canvas. No responsive layout, and no breakpoints. |
| Active area | 73.4 × 49.0 mm | spec [S1] | **6.54 px/mm ≈ 166 ppi.** |
| Pixel ≈ point | 1 px here ≈ 1 pt on a 1× iPhone (163 ppi) | derived | Apple HIG point sizes carry over **1:1 in pixels**: 44 pt target → 44 px (6.7 mm); 17 pt body → 17 px; 13 pt footnote → 13 px. |
| Viewing distance | Desk device, ~50–70 cm (a phone is ~30 cm) | usage | Glanceable content has to be **about 1.5–2× the iOS size**. Primary numbers are ≥26 px, and 13 px is the floor for secondary text. |
| Colour depth | RGB565: 32 levels of R and B, 64 of G | spec | Gradients band, and subtle tints snap to the grid. Every colour token is an exact RGB565 value. Grays are exact only where R=B and G=2R (§18.1). |
| No alpha | The panel shows opaque pixels; the frame sprite has no alpha channel | `display.cpp` | No translucency, no blur, no shadows. Blends are **precomputed** into solid tokens (`COL_GOOD_50` is the precedent). Only text is alpha-blended, because VLW glyph coverage blends over whatever is already in the sprite (`fonts.cpp:34`). |
| Full-frame present only | QSPI can't address partial windows; every update streams all 307,200 bytes | `CLAUDE.md` | **The cost of motion doesn't depend on its size.** A 1 px line that animates costs the same as a full page slide: ~16 ms of transfer, ~30 ms with the TE wait. |
| Frame pacing | TE ≈ 60.4 Hz; `loop()` runs every 33 ms (`LOOP_PERIOD_MS`); `render()` at 1 Hz | `main.cpp:408` | Real animation rate is about 30 fps. A 180 ms transition is about 6 frames, and anything under ~100 ms is 3 frames or fewer, so it reads as a cut. |
| Blocking transitions (today) | `pageTransitionRun()` loops synchronously, so touch isn't read for its 180 ms | `pages.cpp:48` | The system replaces it with non-blocking springs that step once per loop pass. A tap during motion is never lost (§12.3). |
| Touch | Capacitive, **single point**; two fingers read as no touch; no gesture byte; no velocity; sampled about every 33 ms | spec, `touch_axs.cpp` | Finger-sized targets. Every gesture is built from down, move and up. Velocity has to be estimated from position samples. There are no multi-touch gestures. |
| No hover and no keyboard | — | — | No hover states and no focus rings **on the device**. Pressed and selected states carry all the feedback. |
| Fonts | 9 pre-rendered VLW fonts, printable ASCII only, tabular digits, ~184 KB in total | `tools/make_vlw.py` | A small, fixed type scale. **No non-ASCII characters** (no `°`, `·`, `—`, `…`, `→`). Tracking is baked into the advances. Adding a size costs flash and a regeneration, not runtime memory. |
| Memory | 8 MB PSRAM; one full frame is 300 KB | spec | An extra frame buffer (for a slide, a swipe preview, or a sheet) is affordable. The limit is bus time, not RAM. |
| Flash | 6.25 MB app slot, build ~1.55 MB | `CLAUDE.md` | Flash size isn't a design constraint. The **parity cost** is: every asset exists twice, once in the firmware and once in the simulator. |
| Panel type | IPS: wide viewing angles, a slightly lifted black, and some image retention | spec | Near-black (gray.0) and true black look the same. Pixel shift (≤2 px orbit, `SHIFT_ORBIT`) needs a margin at every edge. Avoid large, static, saturated blocks. |
| Backlight | PWM, 0–255; Night Mode dims to 25% (`NIGHT_MODE_DIM_VALUE = 64`) | `state.h:365` | Dimming scales everything, so contrast ratios hold. Absolute luminance falls, though, and low-contrast non-text (tracks, separators) nearly disappears at 25%. So grouping must never depend on a separator alone. |
| Audio | NS4168 I2S amp and speaker socket; currently unused | spec | Available as a future feedback channel (§12.10). Off by default: this is a quiet desk device. |

---

## 2. Principles

Apple frames design as serving four human needs: **predictability, understanding,
achievement and joy.** §2.1 takes Apple's eight design foundations (*Principles of Great
Design*) and says what each one means on a 3.5-inch, 30 fps, single-touch desk dashboard.
§2.2 does the same for the fluid-interface rules (*Designing Fluid Interfaces*), and points
to the section that specifies each one.

### 2.1 The eight foundations, on this device

1. **Purpose: glance first.** The device answers one question in about a second from
   half a metre away: *how much Claude budget is left, and am I using it too fast?* Every
   screen has one most-important thing, and it's the biggest and brightest element
   (≥26 px). Decide what *not* to show: developer telemetry leaves the dashboard (§15).
2. **Agency: nothing traps you, and everything can be undone.** Every change is one tap
   from being reversed: the previous preset is on the same grid, and the previous page is
   one tap back. Only the two truly irreversible actions (Restart, Forget Wi-Fi) ask for
   confirmation, and both use the one arm pattern. That keeps confirmation rare enough
   to be read rather than tapped through.
3. **Responsibility: the desk is public.** Colleagues can read this screen, so screen sleep is
   always one tap away in the system corner. The Wi-Fi password is never displayed (the AP
   screen shows only the SSID and IP). Unknown data is `--`, never a guessed `0`. Warnings
   come from real risk (running ahead of pace), never from a level merely being high.
4. **Familiarity: build on what fingers already know.** Page dots, a chevron that means
   "deeper", close at the top-left, sheets that rise from the bottom, lists that scroll and
   rubber-band. Things that look the same behave the same and sit in the same place on
   every screen, like the system corner and the status strip.
5. **Flexibility: adapt to the setting, and let people hide what they don't use.**
   Brightness, Night Mode, rotation for upside-down mounting, a boot page, and Off
   switches for the pace bars, AQI, hourly signal and progress hairline. There is no
   Dynamic Type, because the canvas is fixed and every size is pre-rendered. Legibility
   comes from the scale itself instead (a 13 px floor, glanceable numbers ≥26 px), plus
   the note pane's own size setting.
6. **Simplicity, not minimalism: show context where it saves a step.** Remove what
   doesn't inform, but never hide what the user would otherwise go looking for. Settings
   rows show their current value, and a reset line says both *when* and *how long*.
   Hierarchy (order, size, contrast) makes the important thing obvious. Controls are
   labelled by what they do; they aren't explained by hints.
7. **Craft: every value can be defended.** Every colour is an exact RGB565 value with a
   measured contrast ratio. Every size, gap, duration and spring comes from a token with
   a reason in §1. The firmware and the simulator agree pixel for pixel. Misaligned
   baselines, numbers that jitter (tabular digits prevent this), strobing motion and dropped
   taps are defects, not polish items.
8. **Delight: the feeling is calm confidence.** Nothing moves unless something
   happened. Motion feels physical and answers the finger instantly, and a static screen
   stays still. Here delight is the absence of friction and noise, not decoration.

**Tactical rules**

- **Four kinds of feedback** (status, completion, warning, error), each with a fixed
  place on screen (§13.2).
- **Wayfinding.** Every screen answers four questions: where am I, where can I go,
  what's here, and how do I get out (§13.1).
- **Grouping and mapping.** Things that sit close together are related. A control sits
  next to what it changes: Device Stats opens from the health glyphs, and the shuffle
  control sits on the cat. Option grids run from low to high, left to right, so their
  layout mirrors the value. **If a control needs a hint to explain it, fix the control.**
- **Direct, specific labels.** Name things for what they contain or do: "Status + cats",
  not "Mixed"; "Pace bars", not "Show countdown" (§15.3).
- **A colour means one thing** everywhere, and is never the only cue (§4.3).

### 2.2 Fluid-interface rules, on this device

| Apple rule | Device rule | Spec |
|---|---|---|
| **Response.** React on touch-down; audit every latency. | The pressed state appears on the first present after touch-down (≤66 ms). No debounce, animation or network wait sits on the input path. | §9.3 |
| **Direct manipulation.** Content stays glued to the finger, keeping the grab offset. | Lists, sheets and swiped pages track the finger 1:1 from where they were grabbed. | §9.4 |
| **Interruptibility.** Never lock out input; animate from the on-screen value. | Motion is stepped once per `loop()` pass. A touch during motion takes over from the on-screen position. There are no blocking transitions. | §12.3 |
| **Behaviour over animation.** Use springs. | All spatial motion is a spring (damping ratio + response), not a fixed-duration curve. | §12.2 |
| **Velocity handoff.** | A released drag hands its measured velocity to the spring. | §12.4 |
| **Momentum projection.** | A flick projects where it would come to rest (`d = 0.995`) and snaps from that point. Whether to commit or reverse is decided by the velocity's direction. | §9.4, §12.4 |
| **Spatial consistency.** | Peers move on X and depth moves on Y. Things leave the way they came. Sheets rise from the bottom edge, which is where every sheet's trigger is. | §12.5 |
| **Hint in the direction of the gesture.** | Pressing one half of a page tilts the page 8 px toward where it's about to go. | §12.5 |
| **Rubber-banding.** | The ends of a list, and a sheet pulled above its open position, resist more the further you pull (constant 0.55). | §9.4 |
| **Frame-level smoothness.** | Each frame moves at most 150 px. Offsets are whole pixels, and every present is TE-synced. | §12.6 |
| **Materials and depth.** | There's no alpha, so there's no glass. Depth comes from surface steps, plus a bit-shift dimming layer behind a rising sheet, a shade where a list scrolls under a header, and solid plates over media. | §8.4 |
| **Multimodal feedback.** | The backlight is a feedback channel that costs no frames, used for sleep, wake, night and the hourly signal. Sound is reserved. Feedback follows Apple's causality, harmony and utility rules. | §12.10 |
| **Reduced motion.** | Springs become 3-frame cross-fades. Motion is never removed without something gentler taking its place. | §12.9 |
| **Typography.** Tracking depends on size, leading shrinks as size grows, and weight builds hierarchy. | Tracking is baked in per size (display and title −1 px, the uppercase label +1 px). Leading tightens as size grows. | §5 |

---

## 3. Tokens: naming and code mapping

### 3.1 Naming

Tokens use `category.role[.variant]`, for example `color.text.secondary`,
`space.md`, `touch.min`, `motion.spring.standard`. There are two layers:

- **Primitive tokens** are raw values: `gray.3`, `coral.500`, `space.8`. Screens
  never use them directly.
- **Semantic tokens** are roles: `color.fill.track`, `space.card.pad`. Screens use
  **only** these.

### 3.2 Code mapping (target state)

Tokens become constants in a single header, `src/tokens.h` (created in migration
phase 1), with a mirrored block in `simulator-s3.html`:

```cpp
// src/tokens.h -- design.md is the source of truth; simulator-s3.html mirrors every line.
// color.text.secondary
const uint16_t TOK_COLOR_TEXT_SECONDARY = 0x9CD3;  // rgb(156,154,156)
// space.md
const int TOK_SPACE_MD = 12;
// motion.spring.standard  (damping ratio x100, response in ms)
const uint16_t TOK_MOTION_SPRING_STANDARD_ZETA = 100, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS = 250;
```

```js
// simulator-s3.html
const TOK_COLOR_TEXT_SECONDARY = 'rgb(156,154,156)'; // 0x9CD3
```

- The rule is **`TOK_` + upper snake of the dotted name**. The RGB565 value goes
  in the firmware; the RGB888 value (with the `// 0x....` comment) goes in the
  simulator. Both change together, as the `CLAUDE.md` parity rule requires.
- The existing `COL_*` constants stay as **aliases** of tokens until each screen
  has been migrated (§15.3), and are then deleted.
- Fonts keep their generated `FontId` names (`FONT_MD`, …). The type tokens in
  §5 are `constexpr FontId` aliases (`TOK_TYPE_BODY = FONT_MD`), so a role can
  move to a different font without touching every call site.
- A screen never contains a raw colour literal, a raw font id or a magic
  spacing number. The only exceptions are product-specific geometry inside a
  named component (the analog clock's hand lengths, for example), defined once
  as constants at that component's definition.

---

## 4. Colour

### 4.1 Primitive palette

Every value is exact RGB565. The RGB888 value is what the simulator draws: each
channel is rounded as `v × 255 / max`, the same conversion the simulator's comments use.

**Neutral ramp.** Exact grays: R = B, G = 2R in RGB565 terms. White is the exception.

| Token | RGB565 | RGB888 | Rel. luminance |
|---|---|---|---|
| `gray.0` | `0x0841` | 8, 8, 8 | 0.0024 |
| `gray.1` | `0x18C3` | 25, 24, 25 | 0.0093 |
| `gray.2` | `0x2945` | 41, 40, 41 | 0.0215 |
| `gray.3` | `0x39C7` | 58, 57, 58 | 0.0413 |
| `gray.4` | `0x6B4D` | 107, 105, 107 | 0.1429 |
| `gray.5` | `0x9CD3` | 156, 154, 156 | 0.3258 |
| `gray.6` | `0xFFFF` | 255, 255, 255 | 1.0000 |

The ramp has seven steps with a reason behind each. Levels 0 → 1 → 2 → 3 are
the surface and fill steps: each is about 1.2–1.5× brighter than the one before,
which reads as distinct on IPS at full backlight. Levels 4, 5 and 6 are the
three text levels.

**Hues**

| Token | RGB565 | RGB888 | Note |
|---|---|---|---|
| `coral.500` | `0xFB08` | 255, 97, 66 | Product accent (today's `COL_ACCENT`) |
| `coral.600` | `0xCA66` | 206, 77, 49 | Pressed accent: coral.500 at about 80% |
| `green.500` | `0x2668` | 33, 206, 66 | Today's `COL_GOOD` |
| `green.wedge` | `0x1B65` | 25, 109, 41 | Precomputed, for the clock's pace wedge (today's `COL_GOOD_50`) |
| `green.shine.lo` / `.mid` / `.hi` | `0x5ECE` / `0x9734` / `0xD7BA` | 90,219,115 / 148,231,165 / 214,247,214 | The pace-bar highlight band (§12.7) |
| `amber.500` | `0xFD20` | 255, 166, 0 | **New.** Warning |
| `red.500` | `0xF8C6` | 255, 24, 49 | Today's `COL_WARN` |
| `blue.500` | `0x3C1E` | 58, 130, 247 | Today's `COL_BLUE` |
| `sun.500` | `0xFE60` | 255, 206, 0 | **New.** Replaces pure yellow `0xFFE0` for content glyphs |
| `black` | `0x0000` | 0, 0, 0 | Plates over media only (§11.20) |

**External scale (exempt).** AQI follows the US EPA and aqicn.org scale:
Good `green.500`, Moderate `0xFFE0`, USG `0xFB82`, Unhealthy `red.500`,
Very Unhealthy `0xAABE`, Hazardous `0x78E3`. These are a standard and not
part of the product palette. `0xFFE0` stays **only** here.

### 4.2 Semantic tokens

| Token | Primitive | Use |
|---|---|---|
| `color.bg.canvas` | gray.0 | The screen background, and the pixel-shift margin fill. |
| `color.surface.card` | gray.1 | Card fill; the resting fill of unselected controls on the canvas. |
| `color.surface.raised` | gray.2 | Things that sit *above* cards: toasts, pressed cards and rows, controls resting inside a card. |
| `color.fill.pressed` | gray.3 | Pressed state of a neutral control (on canvas or card). |
| `color.fill.track` | gray.3 | Meter and scroll tracks (the 100% extent). |
| `color.separator` | gray.3 | 1 px dividers inside a card. It is never the only thing grouping content (see Night Mode, §1). |
| `color.text.primary` | gray.6 | Primary data, titles, row labels. |
| `color.text.secondary` | gray.5 | Labels, units, captions, chrome glyphs at rest. |
| `color.text.tertiary` | gray.4 | Placeholders (`--`), disabled text, scale ticks. **Never** essential information. |
| `color.text.onAccent` | gray.0 | Text on an accent-filled or status-filled control. |
| `color.accent` | coral.500 | Selection, the primary data series, current-item markers. Nothing else (§4.3). |
| `color.accent.pressed` | coral.600 | Pressed state of an accent-filled control. |
| `color.status.success` | green.500 | Connected; "done"; the pace (safe-line) series. |
| `color.status.warning` | amber.500 | Degraded but working: server unreachable, Battery Save active. |
| `color.status.error` | red.500 | Something is wrong, or you're about to do something irreversible: ahead-of-pace flag, WiFi down, an armed destructive action. |
| `color.status.info` | blue.500 | Neutral system data: device-stats meters, code spans. |
| `color.data.usage` | coral.500 | Token-usage series (5h and week %, projects, 7-day trend, limits). |
| `color.data.pace` | green.500 | Time elapsed in a window. It is the "safe line" that usage is compared against. |
| `color.data.pace.wedge` | green.wedge | The clock's time-to-reset wedge. |
| `color.data.system` | blue.500 | Device resource meters. |
| `color.content.sun` | sun.500 | Sun and lightning glyphs. |
| `color.content.rain` | blue.500 | Rain glyph. |
| `color.content.snow` | gray.6 | Snow glyph. |
| `color.content.cloud` | gray.5 | Cloud and fog glyph, and any unmapped weather code. |
| `color.plate` | black | The backing plate behind text or glyphs drawn over media (cats). |

### 4.3 Colour rules

1. **The accent has three jobs, and only three.**
   - **Selection.** The selected segment or option.
   - **The primary data series.** Usage: the big percentages, usage bars, trend
     bars, project bars.
   - **Current-item markers.** The "now" hour and "today" in the forecast, and the
     clock's second hand.

   It is **not** used for titles, headings, close or back or chevron glyphs,
   scroll thumbs, placeholder art, or warnings.
2. **Status colours are reserved.** Red appears only when something is wrong or
   irreversible. Green appears only for "fine", "done" or the pace reference.
   Amber appears only for "degraded, still working". A status colour is never
   used just to make something stand out.
3. **Never colour alone.** Every status colour comes with a second cue:
   - The pace flag is `!` plus the time to exhaustion.
   - WiFi down changes the glyph shape (§10.3).
   - An armed destructive button changes its label to "Tap again".
   - Selected segments are also filled, not just tinted.
4. **Colour goes on solid layers.** Coloured text or glyphs over media (cat
   GIFs) always sit on a `color.plate` rectangle; there's no alpha to fall back on.
5. **One accent per region.** A card has at most one accent-coloured element
   group. If two things in a card both want the accent, one of them is wrong.
6. **Data series keep their colour everywhere.** Usage is coral on the status
   page, the limits page, the projects page, and in the cat overlay text
   highlight. Pace is green on the bars and the clock.

### 4.4 Contrast

WCAG 2 contrast ratios, computed from the RGB888 values above (§18.2):

| Foreground ↓ / Background → | canvas (gray.0) | card (gray.1) | raised (gray.2) |
|---|---|---|---|
| text.primary (gray.6) | 20.0 | 17.7 | 14.7 |
| text.secondary (gray.5) | 7.2 | 6.3 | 5.3 |
| text.tertiary (gray.4) | 3.7 | 3.3 | 2.7 |
| fill.track / separator (gray.3) | 1.7 | 1.5 | 1.3 |
| accent (coral.500) | 6.7 | 5.9 | 4.9 |
| status.success | 9.5 | 8.4 | 7.0 |
| status.warning | 10.2 | 9.0 | 7.5 |
| status.error | 5.2 | 4.6 | 3.8 |
| status.info | 5.5 | 4.8 | 4.0 |
| content.sun | 13.4 | 11.9 | 9.9 |

On filled controls:

| Pair | Ratio |
|---|---|
| text.onAccent (gray.0) on accent | 6.7 (white on accent is only 3.0, so never use it) |
| text.onAccent on accent.pressed | 4.5 |
| gray.0 on status.error (armed destructive) | 5.2 (white is 3.9) |
| text.primary on fill.pressed | 11.5 |

**Minimums**

- **Body and caption text** (≤17 px): **≥4.5:1**. text.tertiary (3.3–3.7) is
  therefore only for non-essential text: placeholders and disabled labels.
- **Large text** (`type.title` and `type.display`, 26 px and up): ≥3:1.
- **Coloured text on `surface.raised`:** status.error (3.8) and status.info (4.0) miss
  4.5:1 there, so status-coloured text sits on the canvas or a card only, never on a
  toast or a pressed surface.
- **Non-text UI** (tracks, separators): these are deliberately below 3:1 (1.3–1.7).
  They are *secondary* structure. The meter's **fill** carries the information and
  has to meet 3:1 against the track: coral on gray.3 is 3.9:1, green 5.5:1, and blue 3.1:1.
  The over-threshold red fill is 3.0:1, right at the limit, which is one more reason the
  % label always carries the number.

### 4.5 RGB565 working rules

- **Pick from the grid.** Any new colour is quantised with `to565()` (§18.1)
  and checked in the simulator *after* the round trip. Never tune a colour in
  RGB888 and assume it survives.
- **No gradients** except the three-step shine band. Smooth ramps band into
  visible steps (8 levels per step in R and B).
- **Precomputed blends.** For "X at N% over Y", compute
  `to565(lerp(rgb(X), rgb(Y), N))` once, add it as a primitive token and name
  what it's for (the precedent is `green.wedge`). A blend only works over the
  background it was computed for. Never draw it over media.
- **Near-black is fine.** On IPS, gray.0 and `0x0000` look the same. gray.0 stays
  as the canvas for parity and for the pixel-shift margin fill.

---

## 5. Typography

### 5.1 Families

| Family | Use | Why |
|---|---|---|
| **Inter** (variable, rendered at fixed weights) | All UI text | Designed for screens: high x-height, open apertures, and tabular figures (`tnum`) so ticking numbers don't jitter. |
| **Google Sans Code** (variable, MONO axis = 1) | Note pane only | Monospace makes the note's wrap walk a columns × rows grid, which the parity-checked tokenizer depends on. |

There is no fallback font on the device. Only the generated fonts exist, so
a missing glyph would come out as `?`. **Firmware strings are printable ASCII only.**
Draw a symbol instead of typing it: the degree sign is a ring glyph (§10),
and a separator is two spaces or `/`.

### 5.2 Type scale

The type tokens map onto the nine generated fonts.
- The line box is maxAscent + maxDescent, and leading is shown as a multiple of the size.
- Tracking is a whole-pixel adjustment baked into every glyph advance by `make_vlw.py` (§5.3 rule 3).
- The `0` advance is the tabular digit width *after* tracking.
- `y` is the top of the line box (`fonts.h`).

| Token | Font id | Size / weight | Line box (leading) | Ascent | Tracking | `0` advance | Role |
|---|---|---|---|---|---|---|---|
| `type.display` | `FONT_XL` | 40 / 650 | 49 (1.23×) | 39 | **−1 px** (−0.025 em) | 26 | Hero numerals only: the weather hero temperature and the offline or empty-state hero. At most one per screen. |
| `type.title` | `FONT_LG` | 26 / 650 | 33 (1.27×) | 26 | **−1 px** (−0.038 em) | 17 | Glanceable key numbers (5h %, week %, digital time) and the values to type on the AP setup screen. **Not for screen titles:** every screen title is headline, in the modal header (§11.14). |
| `type.headline` | `FONT_MDB` | 17 / 650 | 23 (1.35×) | 18 | 0 | 12 | Screen titles (modal header), row labels, button labels, emphasised values (BTC price, AQI number, the pace flag `!`). |
| `type.body` | `FONT_MD` | 17 / 500 | 23 (1.35×) | 18 | 0 | 11 | Readable text and values: limits rows, reset lines, forecast temperatures, the date. |
| `type.label` | `FONT_SMB` | 13 / 650 | 17 (1.31×) | 13 | **+1 px** (+0.077 em) | 10 | **UPPERCASE** section labels (`5H`, `WK`, `TOP PROJECTS`), 1–3 words. |
| `type.caption` | `FONT_SM` | 13 / 500 | 17 (1.31×) | 13 | 0 | 9 | Secondary information: units, axis labels, the status strip, subtitles. |
| `type.mono.s` | `FONT_MONO1` | 11 / 450 | 15 (1.36×) | 11 | 0 (grid) | 7 | Note pane size 1 only (an *exception*, §5.3). |
| `type.mono.m` | `FONT_MONO2` | 16 / 450 | 21 (1.31×) | 16 | 0 (grid) | 10 | Note pane size 2. |
| `type.mono.l` | `FONT_MONO3` | 22 / 450 | 29 (1.32×) | 22 | 0 (grid) | 13 | Note pane size 3. |

Numeric roles are aliases of the same fonts, so data code reads as intent:

| Numeric token | Alias of | Use |
|---|---|---|
| `type.numeral.hero` | type.display | Weather hero temperature |
| `type.numeral.lg` | type.title | 5h/week %, digital clock |
| `type.numeral.md` | type.headline | BTC price, stat values, device-stat % |
| `type.numeral.sm` | type.caption | Axis values, forecast hours, small temperatures |

The hierarchy comes from **size, weight and colour together**, as the Apple
skill recommends:
- **Primary:** title / primary colour.
- **Supporting:** body / secondary colour.
- **Scaffolding:** label or caption / secondary colour.

Emphasis within a size comes from weight (headline vs body), never from colour.

### 5.3 Rules

1. **13 px is the floor.** Nothing smaller ships. At 166 ppi and 60 cm, 13 px
   Inter (x-height ≈ 7 px ≈ 1.1 mm) is the smallest comfortable size for
   secondary text. `type.mono.s` (11 px) is the one exception: the user picks
   note size 1 in `note.html` to trade legibility for capacity.
2. **Case.**
   - `type.label` is always UPPERCASE and 1–3 words.
   - Everything else is **sentence case**: screen titles ("Device stats"), row labels
     ("Poll interval"), buttons ("Restart"), hints ("Tap a level to apply").
   - Units and abbreviations keep their own case (`5h`, `KB`, `AQI`, `BTC`).
   - Never set whole screens in capitals; it slows reading and flattens the hierarchy.
3. **Tracking depends on size, and is never one value for every size.** Large text needs
   negative tracking, because letters look further apart as they grow. Small uppercase text
   needs positive tracking. Body text stays near 0.
   - The values in §5.2 follow Inter's own dynamic-metrics formula
     (`tracking_em = −0.0223 + 0.185·e^(−0.1745·size)`), which gives −0.89 px at 40,
     −0.53 px at 26 and ≈0 at 17 and 13. Each is rounded to a whole pixel, because a VLW
     advance is an integer.
   - Uppercase `label` gets +1 px on top, because all-caps text reads better slightly open.
   - `make_vlw.py` adds a per-font `track_px` to every glyph's `xAdvance` and to `FONT_ADV`.
     Both parity rules still hold, since width is still the sum of the advances, and no
     new font ids are needed.
   - Never write per-call letter-spacing code.
   - Monospace fonts keep 0, because the note pane's column grid depends on it.
   - **Optical sizing.** The current source, `Inter-VariableFont_slnt,wght.ttf`, has no
     `opsz` axis. Inter 4's `InterVariable.ttf` does (range 14–32). Rendering each size at
     `opsz = size` gives 13 and 17 px the open text cut and 26 and 40 px the tighter display
     cut, the way Apple's system font changes shape with size. `load()` would set the
     `Optical size` axis the same way it already sets `Weight`. This is recommended for
     migration phase 2; regenerate and compare with screenshots.
4. **Numbers.**
   - Always tabular (every generated font has `tnum`).
   - A unit goes directly after its value, in `type.caption` + `text.secondary`,
     on the **same baseline** (`y_unit = y_value + ascent(value) − ascent(unit)`).
     Example: `27` in title + `C` in caption.
   - `%` stays in the value's font; it is part of the number.
   - The temperature degree is the ring glyph (§10.4), in text.secondary.
5. **Unknown values** are `--`, in the value's own font, in `text.tertiary`.
   Using the same font keeps the width and baseline, so nothing jumps when data arrives.
6. **Truncation** works on **measured width** (`textW`) and never on a character
   count, because Inter is proportional. Truncate at a word boundary if one
   is within 4 characters; otherwise cut hard. There is no ellipsis glyph (ASCII
   only), so don't append `...` unless the width allows all three dots.
7. **Leading shrinks as size grows.** Display and title are **single-line only** and
   never wrap, so their line boxes (1.23× and 1.27×) are about fit, not reading. Stacked
   lines of body or caption step by one line box (1.31–1.35×: body 23, caption 17).
   Dense data rows may tighten by `space.hair`, but never open up past line box + `space.xs`. Between a label and the value it names, the gap is
   `space.xs` (4). Between separate text groups, the gap is `space.sm` (8) or more.
8. **Alignment.** Text that shares a row shares a **baseline**, not a top.
   Mixed-size runs (the `17%` + `5H` label pattern) compute each run's `y`
   from a common baseline, as `drawLimitHalf` does today (`pages.cpp:511`).
9. **Content style.** These formats are produced by `format.cpp` helpers only,
   never by ad-hoc `String` building in draw code:

   | Kind | Format | Example |
   |---|---|---|
   | Time of day | 24 h `HH:MM` | `04:59` |
   | Countdown | `Xh MMm`, `Xd Yh`, `Xm` | `4h 03m`, `6d 16h`, `42m` |
   | Weekday / month | 3-letter | `Thu`, `Sep` |
   | Date | `Www D Mmm` | `Thu 24 Sep` |
   | Large counts | `fmtTokens` | `1.2M` |
   | Money | `fmtCost` | `$12.40` |
   | BTC | `fmtBtc` | `84,194` |
   | Temperature | integer + ring glyph (or `C` in caption on compact tiles) | `27` |

   Countdowns change from today's `4h:03m` / `6d:16h`, where the colon wrongly
   suggests a clock time.

---

## 6. Spacing

### 6.1 Scale

The base unit is **4 px**, with one 2 px half-step for tight internal
alignment. Every gap, padding and offset is one of these values.

| Token | px | mm | Use |
|---|---|---|---|
| `space.none` | 0 | 0 | Flush alignment |
| `space.hair` | 2 | 0.3 | Inside dense data only: between a value and its unit's optical edge, between stacked meters' ends. **Never between components.** |
| `space.xs` | 4 | 0.6 | A label to the value it names; paired meters (usage over pace); a tight icon-to-text gap |
| `space.sm` | 8 | 1.2 | **Default gap.** Screen margin, card gutter, list gap, grid gap, icon-to-text gap, between text groups in a card |
| `space.md` | 12 | 1.8 | **Standard card padding**; between sub-groups inside a card |
| `space.lg` | 16 | 2.4 | Between major groups inside a full-width screen; empty-state title to description |
| `space.xl` | 24 | 3.7 | Section separation on full-screen overlays and sheets |
| `space.xxl` | 32 | 4.9 | Hero breathing room: empty states, the AP setup steps |

### 6.2 Semantic spacing

| Token | Value | Rule |
|---|---|---|
| `space.screen.margin` | 8 (sm) | No content ink within 8 px of the physical edge. This also absorbs the 2 px pixel-shift orbit. Hit boxes may extend to the edge. The progress hairline is the one exception (§11.13). |
| `space.gutter` | 8 (sm) | Between sibling cards, horizontally and vertically. |
| `space.card.pad` | 12 (md) | Standard card, all four sides. |
| `space.card.pad.compact` | 8 (sm) vertical / 12 (md) horizontal | Compact and hero cards (§11.1). |
| `space.stack.tight` | 4 (xs) | Label → value; meter → meter. |
| `space.stack` | 8 (sm) | Between rows of content inside a card. |
| `space.group` | 16 (lg) | Between groups inside a full-width screen. |
| `space.list.gap` | 8 (sm) | Between list rows. |
| `space.touch.gap` | 8 (sm) | Minimum visual gap between two distinct touch targets. |
| `space.icon.text` | 8 (sm), or 4 (xs) when inline in a caption | Glyph to its label. |

### 6.3 When to use which

- **Inside a component** use hair, xs or sm. **Between components** use sm (the gutter).
  **Between regions of a full-screen layout** use lg or xl.
- If you want a value that isn't on the scale (say 10 or 14), the layout above it is
  wrong. Fix the container, not the gap.
- Vertical rhythm inside cards comes from line boxes plus scale gaps. Don't
  nudge text by 1–3 px to "look right". Align on baselines instead (§5.3.8).

---

## 7. Layout and proportion

### 7.1 The frame

```
x = 0   8                                                 471 479
    +--------------------------------------------------------+  y = 0
    |  margin 8                                              |
    |   +------------------------------------------------+   |  y = 8     content top
    |   |                                                |   |
    |   |          CONTENT AREA  464 x 272               |   |
    |   |          x 8..471, y 8..279                    |   |
    |   |                                                |   |
    |   +------------------------------------------------+   |  y = 279   content bottom
    |      gap 8                                             |
    +--------------------------------------------------------+  y = 288   status strip top
    |  STATUS STRIP 480 x 31, y 288..318                     |
    +========================================================+  y = 319   progress hairline
```

| Token | Value |
|---|---|
| `layout.screen` | 480 × 320 |
| `layout.content.x0` / `x1` | 8 / 472 (exclusive) → 464 wide |
| `layout.content.y0` / `y1` | 8 / 280 (exclusive) → 272 tall |
| `layout.strip.y0` / `h` | 288 / 32 (the last row, y 319, is the progress hairline) |
| `layout.overlay.content.y1` | 312 (exclusive). Overlays and sheets have no status strip, so their content runs to 304 px tall. |
| `layout.header.h` | 44. The modal header band is y 0..43, with content from y 52. |

The sums: 8 + 272 + 8 + 32 = 320 ✓, and 8 + 464 + 8 = 480 ✓.

### 7.2 Grid

**Two columns** (the dashboard pages). These are deliberately **unequal**. The left
column is a narrow *reserved zone* for the limit and BTC cards (5h, week, BTC), fixed so
that its content ends at x 180. The remaining width goes to the visual cards on the
right (clock, weather, note, cats).

| Token | Value |
|---|---|
| `layout.col.left.x` | 8 |
| `layout.col.left.w` | 172 (x 8..179; content ends at x 180) |
| `layout.col.right.x` | 188 |
| `layout.col.right.w` | 284 (x 188..471) |

8 + 172 + 8 + 284 + 8 = 480 ✓. The left column's width is a **fixed reservation**. It
doesn't grow for longer text; content that doesn't fit gets shorter labels or wraps
onto a second caption line (§7.4).

**Page-navigation halves are screen halves** (x < 240 and x ≥ 240). They don't follow the
grid, so the split passes through the right column at x 240. That's intentional:
navigation is a background layer (§9.2), and "left side goes back, right side goes
forward" has to stay symmetric and predictable whatever the layout. Only tappable cards
(which win in hit-testing) have to avoid depending on which half they're in.

**One column** (full-width pages and overlays): x 8..471, 464 wide.

**Option grids** (§11.8) divide the 464 content width with `space.sm` gaps:

| Columns | Cell width |
|---|---|
| 5 | 86 (5 × 86 + 4 × 8 = 462, centred) |
| 4 | 110 |
| 3 | 149 |
| 2 | 228 |
| 1 | 464 |

**Rules**

1. Everything aligns to the column edges or to the padding edges inside a card. A
   left edge is always one of: 8, 16 or 20 (8 + a card's padding), 188, 196 or 200, or the
   left edge of a grid cell.
2. **Primary content goes left (reading order), and the glanceable hero goes top-left.**
   Secondary and contextual content goes right or below.
3. **Proportion.** Split a column vertically in whole-card units that sum
   exactly to 272 with 8 px gutters. No card is shorter than 32 (the compact card).
   The hero card of a column should hold about ⅔ of its height.
4. **Density.** A card shows one idea. If a card needs two section labels,
   it is two cards.
5. **Empty space is deliberate.** When content is shorter than its card, the
   extra space goes at the **bottom** of the card; content is never vertically
   centred, except in empty states and hero readouts. That keeps baselines
   aligned across neighbouring cards.

### 7.3 Reserved zones

**The system corner** (top-right, present on every screen):

| Slot | Glyph box | Hit box | Contents |
|---|---|---|---|
| `corner.slot.a` | x 448..471, y 8..31 (24 × 24) | x 436..479, y 0..43 (44 × 44; the icon-button box, clipped to the edges) | Screen sleep (always) |
| `corner.slot.b` | x 404..427, y 8..31 | none (status only) | Battery Save indicator (only while active). It sits 8 px left of slot a's hit box, so a tap on it never sleeps the screen. |

- Content ink stays **8 px clear** of an occupied slot's glyph box.
- Slot b's space is reserved even while its icon is hidden, so nothing jumps when Battery Save turns on.
- Over media, each occupied slot gets a `color.plate` backing (§11.20).

**The close slot** (top-left, on every overlay, sheet and modal screen):

| Glyph box | Hit box |
|---|---|
| x 8..31, y 8..31 | x 0..51, y 0..51 |

It mirrors the system corner. The band y 0..43 across the top of these screens is the
**modal header** (§11.14): close or back in the slot, the title in headline at x 60, and the
system corner on the right. An overlay whose first content is a hero readout (Weather)
may put that hero card at x 60..471, y 8..71 **in place of the title**. Nothing else
may share the band.

**The status strip.** See §11.12.

### 7.4 Reference layout: Status page on the grid

This shows that the grid holds the real content with no cheating. Every number
below is a token sum. It is the target for migrating page 0.

```
x: 8           179 188                                471
y: 8  +----------+ +--------------------------[b]--[a]---+  <- corner slots
      | 17%  5H  | |     .-------.                         |
      | ======== | |    /         \                        |
      | ====     | |   |           |      12:27            |
      | Resets   | |   |   clock   |      Thu 24 Sep       |
      | 16:30    | |   |   r 76    |      [51]             |
  119 +----------+ |    \         /                        |
  128 +----------+ |     '-------'                         |
      |8% WK !86h| |                                       |
      | ======== | +---------------------------------------+  199
      | ==       | +---------------------------------------+  208
      | Resets   | | 28 (rain)  13   14   15   16        > |
  239 +----------+ | 24  27     ..   ..   ..   ..          |
  248 +----------+ |                                       |
      |BTC 84,194| |                                       |
  279 +----------+ +---------------------------------------+  279
```
(The left cards' text is abbreviated in the sketch. The real strings are measured with
the firmware glyph advances and the §5.2 tracking in the table below.)

| Card | Box | Internal budget (px) |
|---|---|---|
| 5H limit | 172 × 112, pad 8 / 12 (compact) | **Width:** 148 inner. Worst-case top row `99%  WK  ! 167h` = 142 px ✓. At 100% the flag has no duration (`formatPaceDur` returns ""), so `100%  WK  !` = 113 ✓. The label-to-flag gap is `space.xs`. **Vertical:** 96 inner = numeral.lg 33 + 4 + usage meter 8 + 4 + pace meter 6 + 4 + caption `Resets 16:30` / `Resets Thu 05:00` (≤113 px) 17 + caption `in 4h 03m` / `in 6d 16h` (≤63 px) 17 = 93 ✓, with 3 px of slack at the bottom (§7.2 rule 5). |
| Week limit | 172 × 112, pad 8 / 12 | Same as 5H. The label stays `WK`: `WEEK` would push `99% WEEK ! 86h` to 149 px, over the 148 available. |
| BTC (compact) | 172 × 32, pad 8 / 12 | caption `BTC` 26 + 8 + headline `123,456` 76 = 110 ≤ 148 ✓. Headline 23 is vertically centred. |
| Clock (hero) | 284 × 192, pad 8 | **Side by side**, because the extra width lets the clock grow. Clock ⌀ 153 (r 76, up from today's 68) + 12 + readout column 103 = 268 ✓. The readout is numeral.lg `12:27` 75 px, 4, body `Thu 24 Sep` 92 px, 8, then the AQI badge (48 × 24) = 92 px tall, vertically centred (y 58..149). That keeps it clear of the corner slots, which end at y 31 plus 8 px of clearance. |
| Weather (compact) | 284 × 72, pad 8 / 12 | Vertical: caption 17 + content.sm glyph 22 + caption 17 = 56 ✓. Horizontal: 260 inner = H/L 20 + now 40 + **4** × 44 hourly (one more hour than before) + 24 disclosure column ✓. |

Left column: 112 + 8 + 112 + 8 + 32 = 272 ✓. Right column: 192 + 8 + 72 = 272 ✓.

What the migration does to page 0:
- The left column narrows from today's 236 px to its fixed 172 px reservation, and the
  right column widens from 236 to 284.
- Each limit card keeps two short caption lines, `Resets …` then `in …`, now in caption
  rather than body.
- The clock moves beside its readout and grows to r 76.
- The Weather strip gains a fourth hour and a disclosure chevron, because it's tappable.

**The same left column is shared** by the Mixed and Note pages (`drawLimitsCard` +
`drawBtcCard`), so their right panes widen to 284 × 272 too:
- **Note pane:** with a 260 px text width it holds 37 / 26 / 20 columns at mono.s / m / l
  (today 31 / 22 / 16). That's still at least the CYD pane's 24 / 12 / 8, so the
  `note.html` / `note.py` fit check stays a safe bound. The server's 480-character cap,
  not the pane, still limits the text.
- **Mixed cat pane:** becomes x 188..471 × y 8..279, and the cover-fit scale is recomputed
  for that box (`MIXED_GIF_X0/W/Y0/H`).
- The 5h/week shine strip geometry follows the new meter width (`SHINE_BAR_W`: 148 instead
  of 208).

---

## 8. Shape, stroke and depth

### 8.1 Radius

| Token | Value | Use |
|---|---|---|
| `radius.none` | 0 | The canvas; full-bleed media |
| `radius.sm` | 4 | Badges (AQI), chart bars, toast |
| `radius.md` | 8 | Cards, list rows, buttons, option cells |
| `radius.full` | h / 2 | Meters and tracks, pills, dots, the scroll thumb |

Rules:
- Nested corners are concentric: inner radius = outer radius − padding, with a minimum of 0.
  So a control inside a card with pad 8 gets radius 0 if the card is 8. In
  practice, controls inside cards use `radius.sm`.
- Radius depends on the component, not the size.

**AA primitives.** Use `aaFillRoundRect` / `aaFillCircle` / `aaRing` (pages.cpp)
for anything with radius ≥ 4. They run LovyanGFX 1.2.30's `fillSmoothRoundRect`
algorithm but blend edge pixels straight into the frame buffer; the simulator's
`gfx.fillSmoothRoundRect` / `gfx.aaRing` port the same loops (confirmed by a
board-vs-simulator pixel compare). Plain `fillRoundRect` corners are aliased and
look stair-stepped at this density.

### 8.2 Stroke

LovyanGFX `drawWideLine(x0, y0, x1, y1, r, c)` takes a **half-width**, so the drawn
stroke is `2r` wide (see the simulator's note at `simulator-s3.html:640`). The
tokens are in rendered pixels:

| Token | Rendered width | `r` arg | Use |
|---|---|---|---|
| `stroke.hairline` | 1 px | — (`fillRect`, 1 px) | Separators, the progress hairline |
| `stroke.glyph` | 2 px | 1.0 | System glyphs at 16 and 20 px (matches the ~2.2 px stem of 17 px Inter at 650) |
| `stroke.glyph.lg` | 3 px | 1.5 | System glyphs at 24 px |
| `stroke.clock.*` | 6 / 4 / 2.4 / 1.6 px | 3.0 / 2.0 / 1.2 / 0.8 | Clock hour, minute, second and reset hands, as today (product-specific, §11.18) |

Caps are round, which is what drawWideLine draws. Never mix stroke widths within one glyph.

### 8.3 Depth and elevation

There are no shadows, blur or translucency (§1: no alpha, full-frame cost). Depth is
**surface steps**:

| Level | Token | Examples |
|---|---|---|
| 0 | `color.bg.canvas` | The screen |
| 1 | `color.surface.card` | Cards, list rows, resting controls |
| 2 | `color.surface.raised` | Toasts, pressed rows and cards, controls inside a card |
| Plate | `color.plate` | Anything over media |

- **Cards have no border.** The surface step groups them. Borders are reserved for
  the *selected* outline of an option cell, if one is ever needed. Today, selection
  is shown by a fill.
- A full-screen sheet is level 0 (the canvas) with its own cards at level 1. Once it's
  fully open nothing sits behind it; while it moves, the page behind is dimmed (§8.4).

### 8.4 Materials without alpha

Apple uses translucent materials to show hierarchy without stealing focus. This panel
has no alpha, but RGB565 allows one cheap trick: **halving every channel takes a single
shift and mask**, and so does quartering. `display.cpp` already touches every pixel in its
rotate copy on the way to the panel, so a darkened layer applied there costs nothing extra.

| Token | Per-pixel operation | Brightness | Use |
|---|---|---|---|
| `material.scrim.light` | `c − ((c >> 2) & 0x39E7)` | 75% | The page behind a sheet, early in its travel |
| `material.scrim` | `(c >> 1) & 0x7BEF` | 50% | The page behind a sheet at full travel; the scroll-edge shade |
| `material.scrim.deep` | `(c >> 2) & 0x39E7` | 25% | Reserved |

**Rules**

- **Dim to focus.** While a sheet rises, the page behind it steps from 100% to 75% to 50%
  as the sheet travels. These are three fixed steps: they're cheaper than continuous
  blends and band less. Dismissing reverses the steps.
- **A scroll-edge shade, not a divider.** Once a list has scrolled (offset > 0), the 8 rows
  of content passing under the bottom of the modal header (y 44..51) are drawn at
  `material.scrim`, so the content visibly slides *under* the header. There's never a 1 px
  line under a header. At offset 0 there's no shade, because nothing is underneath.
- **Legible text over media.** Text over cats sits on a solid `color.plate`, in primary
  colour, one weight heavier than it would otherwise be (body becomes headline). Never put
  secondary-colour text on a plate over busy content. Colour always goes on the solid layer.
- **Never stack scrims.** At most one dimmed layer exists at any time, because two in a
  row make everything unreadable.
- The simulator reproduces the same operations on its canvas `ImageData`, so these
  effects are covered by the parity rule too.

---

## 9. Touch and gestures

### 9.1 Targets

| Token | Value | Physical | Rule |
|---|---|---|---|
| `touch.min` | 44 × 44 px | 6.7 mm | Every target's **hit box** is at least this. |
| `touch.comfortable` | 56 px tall | 8.6 mm | Default height for rows, buttons and option cells. |
| `touch.edge.min` | 40 px | 6.1 mm | Allowed in the dimension *normal to the physical edge* for edge-anchored targets (the status strip, the corners). The bezel stops the finger, so the touch centroid lands inside the band. |
| `touch.hit.outset.max` | 8 px | 1.2 mm | A hit box may extend up to 8 px beyond its **component box** into margins and gutters, **never** into another target. A control's component box is its visible shape. An icon button's component box is 44 × 44 centred on the glyph (§11.10), even though only the glyph is drawn. |
| `touch.gap` | 8 px (visual) | 1.2 mm | Minimum visual gap between targets. Where outset hit boxes would overlap, split at the midpoint. |

**Every target has a visible affordance:**
- a filled control shape (button, cell, row), or
- a glyph (close, back, gear, sleep), or
- a disclosure chevron on a tappable card.

Invisible hot zones are not allowed. The one exception is the page-navigation halves, which are the
background layer of every page and are explained by the page indicator (§11.11).

### 9.2 Hit-test order

A touch goes to the **first** match:

1. **Wake from sleep.** Any touch; it is swallowed (§9.5).
2. **The system corner** (sleep).
3. **The active modal layer** (Settings list or leaf, overlay): its controls, then its
   close/back, then (for overlays) "tap anywhere to dismiss".
4. **Page controls:** tappable cards, the status strip's targets, the media control (§11.20).
5. **Page navigation halves:** x < 240 → previous, x ≥ 240 → next.

This matches today's router in `main.cpp:505–553`. The design change is only in
*what counts as a target*.

### 9.3 Tap lifecycle

The Apple rule: highlight on down, commit on up.

```
touch-down on target        -> PRESSED state on the next present (target <= 66 ms: <=33 loop + ~30 present)
move  > touch.slop (10 px)  -> tap CANCELLED; if the surface scrolls, becomes a drag (9.4)
up    inside hit box + slop -> COMMIT the action, show the result on the same present
up    outside               -> cancel, return to REST (no action)
```

| Token | Value | Notes |
|---|---|---|
| `touch.slop` | 10 px | One value for both "cancel the tap" and "start a drag". Replaces `DRAG_TAP_PX = 8`. |
| `touch.rearm` | 120 ms | A new touch-down is ignored if it comes within 120 ms of the **previous touch-up**. This suppresses controller bounce without eating deliberate rapid taps. Replaces the 350 ms guard *measured from the previous down* (`TOUCH_DEBOUNCE_MS`). **Tune on the board** (§18.4). |
| `touch.longPress` | 500 ms | Reserved (§9.4). |
| `touch.armWindow` | 4000 ms | Destructive arm window (`CONFIRM_ARM_MS`, §9.5). |
| `touch.wakeGuard` | 600 ms | Ignore touches right after entering sleep (`SLEEP_WAKE_GUARD_MS`). |
| `touch.velocity.window` | 100 ms | Velocity is measured over the last 100 ms of samples (§9.4). |
| `touch.decel` | 0.995 per ms | Momentum deceleration rate (§9.4). |
| `touch.flick.commit` | 150 px/s | Above this speed, the velocity's direction decides commit vs reverse. |
| `touch.flick.bounce` | 300 px/s | Above this speed, a released sheet may use `motion.spring.flick`. |

Why commit on up: a desk device gets brushed when it's handled or plugged
in. Today every touch-down commits immediately (page change, overlay open), and
there is no way to cancel. Committing on up adds no *perceived* latency, because
the pressed state appears on the down, and it makes brushes harmless.

**Page navigation** commits on up too. Its pressed state is the **lean**: on touch-down,
the page tilts 8 px toward where it will go (§12.5). Releasing continues the slide from
there; sliding off springs it back.

### 9.4 Gestures

**Recognise gestures in parallel.** From touch-down, every gesture the surface supports
is a candidate: tap, horizontal drag, vertical drag. The first one to be proved wins.
Movement beyond `touch.slop` picks a drag on whichever axis moved more (|dx| vs |dy|),
and a release inside the slop is a tap. The losing candidates cancel silently.
**There's no double-tap anywhere**, so a single tap never waits to be told apart from a
double. The destructive arm is two separate taps, with no timing on the first.

**Grab offset.** A dragged thing keeps the offset between the finger and where it was
grabbed: `offset = offset_at_down + (finger − finger_at_down)`. It never snaps to the
finger. The existing Settings drag already does this with per-move deltas
(`settings.cpp:464`).

**Velocity.** Keep a ring buffer of the last 4 touch samples (position plus `millis()`).
- The release velocity is (newest − oldest sample within `touch.velocity.window`) / Δt.
- If the newest sample is more than 50 ms old at release (the finger paused before
  lifting), the velocity is 0.
- Clamp to ±4000 px/s to discard controller glitches.

At 30 Hz, fewer than 3 samples is too noisy to use.

| Gesture | Surface | Behaviour |
|---|---|---|
| **Tap** | Everywhere | §9.3. |
| **Vertical drag to scroll** | Lists | Tracks 1:1 from the grab. Past either end it rubber-bands; on release it continues with momentum. |
| **Momentum** | Lists | Exponential deceleration, `v ← v · d^dt_ms` with `touch.decel` d = 0.995. That sits between Apple's 0.998 (normal) and 0.99 (snappy), because long coasts judder at 30 fps and cost presents. It comes to rest at `x + project(v)` (§12.4) and stops when \|v\| < 20 px/s. If it reaches an end while coasting, the remaining velocity passes to the rubber-band spring. A touch-down stops it dead, at its on-screen position. |
| **Rubber band** | List ends; a sheet pulled above its open position | `rubberband(over, dim) = over · dim · 0.55 / (dim + 0.55 · \|over\|)`, with `dim` = viewport height. **No hard stops.** Today's list clamps at its ends (`settings.cpp:473–474`). On release it springs back (`motion.spring.standard`) with the release velocity. |
| **Vertical drag to dismiss** | Sheets (Weather, Device Stats, Settings) | The sheet follows the finger down 1:1, and rubber-bands if pulled up. In Settings the list scrolls first; once it's at offset 0 and the drag goes down, the sheet itself moves (the iOS rule). **On release:** if \|v\| > `touch.flick.commit`, the velocity's direction decides (down dismisses, up stays open). Otherwise the projected position is compared with half the sheet height. The spring takes the release velocity (`motion.spring.flick` above `touch.flick.bounce`). |
| **Horizontal drag back** | Settings detail | The detail follows the finger to the right, revealing the list: the push path in reverse. Release rules are the same as for dismissing. |
| **Horizontal swipe between pages** | Dashboard pages | This adds to the tap halves; it doesn't replace them. The current page and its neighbour track 1:1. The neighbour is rendered into a PSRAM frame on touch-down, the same frame the lean hint uses (§12.5). On release, the velocity's direction decides if \|v\| > `touch.flick.commit`; otherwise the projected offset is compared with 50%. The spring takes the release velocity. |
| **Long press** | Reserved | 500 ms (`touch.longPress`). Only ever an *accelerator* for something a tap can also reach, never the only path. Unused in v1. |
| **Pinch, two-finger, drag and drop** | Excluded | The controller is single-touch, and nothing on the device needs them. |

### 9.5 Accidental-touch prevention

- **Commit on up** (§9.3). A brush that slides off does nothing.
- **Destructive actions need the arm pattern** (§11.9): tap to arm (the button turns
  red and reads "Tap again"), then tap again within `touch.armWindow` = 4000 ms
  (`CONFIRM_ARM_MS`). It disarms on timeout or on leaving the screen. This is the
  **only** confirmation pattern; there are no dialogs.
- **Wake guard.** A touch within 600 ms of entering sleep is ignored
  (`SLEEP_WAKE_GUARD_MS`). The waking touch is swallowed and never reaches
  the screen underneath.
- **Edge targets are small and specific.** Only the system corner and the status strip have
  targets at the edge. The rest of the edge band belongs to page navigation,
  where a stray commit is cheap: it's undone with one tap the other way.
- **Two fingers** already read as no touch, because the driver drops them (spec).

### 9.6 Touch zones (hit map, dashboard page)

```
x: 0                        240 (screen-half split)               436     479
   +---------------------------+-------------------------------+---------+  y 0
   |                           |                               |  SLEEP  |
   |                           |                               | 44 x 44 |  y 43
   |   PREVIOUS PAGE           |   NEXT PAGE                   +---------+
   |   (background layer)      |   (background layer)                    |
   |                           |                                         |
   |                           |   [tappable card -> its overlay]        |
   |                           |   e.g. Weather, with chevron            |
   +--------+------------------------------------------------+-----------+  y 280
   | HEALTH |                                                | SETTINGS  |
   | ->Dev. |  (page halves continue; dots are not a target) | 56 x 40   |
   | 56 x 40|                                                |           |
   +--------+------------------------------------------------+-----------+  y 319
           x 56                                             x 424
```

The strip's targets begin at y 280, so their hit boxes reach up into the
8 px gap above the strip. That makes them 40 px tall, which meets
`touch.edge.min` for a bottom-edge target.

---

## 10. Iconography

### 10.1 Two families

| | **System glyphs** | **Content glyphs** |
|---|---|---|
| What | Chrome and state: close, back, disclosure, gear, sleep, battery, Wi-Fi, status dot, shuffle, degree ring | Weather conditions |
| Style | **Monochrome outline**, `stroke.glyph`, round caps | **Filled** shapes, with semantic colour allowed |
| Colour | Rest: `text.secondary`. Active or pressed: `text.primary`. Selected: `accent`. State glyphs use their status colour. | `color.content.*` (§4.2) |
| Sizes | `icon.sm` 16, `icon.md` 20, `icon.lg` 24 | `glyph.content.sm` (k 1.2 ≈ 22 px), `.md` (k 1.5 ≈ 27 px), `.lg` (k 2.0 ≈ 36 px) |

The **quantity exception:** glyphs that show an *amount* (Wi-Fi bars, the status dot,
the battery level) are filled even in the system family, because a filled shape
reads as a quantity and an outline reads as an object or action.

### 10.2 Construction

- **Grid.** Draw each system glyph on a square box of its size, with a 2 px inset
  (so a 16 px live area in a 20 px box). The glyph's optical centre is the box
  centre. Round glyphs (the status dot, the gear) may use the full box; square
  ones (battery) stay inside the inset. That is basic optical sizing.
- **Primitives.** Build everything from `drawWideLine`, `fillSmoothCircle`,
  `fillSmoothRoundRect` and `fillTriangle`. There are no bitmaps. Vector
  primitives scale, cost no flash, and are mirrored by the simulator's `gfx` layer.
- **Stroke.** 16 and 20 px glyphs use 2 px; 24 px glyphs use 3 px. Never mix widths.
- **Corners.** Round caps and round joins everywhere. Rectangles inside glyphs use
  `radius` 2 (battery body).
- **Pixel snapping.** Put stroke centres on half-pixels for odd widths and on whole
  pixels for even widths, so 2 px strokes land exactly on two columns and don't
  smear across three.
- **Scaling content glyphs.** Scale by `k` from the CYD geometry, as
  `drawWeatherIcon(cx, cy, code, k)` does today. Line widths scale with `k` too.

### 10.3 System glyph set

| Glyph | Size | Construction | States |
|---|---|---|---|
| **Close** | 20 | Two diagonals across the 16 px live area | Rest: secondary. Pressed: primary, with a `fill.pressed` 44 px disc behind it. |
| **Back** | 20 | A chevron, 8 px wide × 14 px tall, pointing left, at the left of its label | Same as close |
| **Disclosure** | 16 | A right-pointing chevron, 5 × 10 | Always `text.tertiary`. It's a hint, not a control. The row or card is the target. |
| **Gear** | 20 | A ring (r 5, 2 px) plus 8 teeth (2 px, from r 7 to r 9) | Rest: secondary. Pressed: primary. |
| **Sleep** | 20 | A crescent: a filled r 7 disc minus an offset r 6 disc in the background colour, on a `surface.raised` 24 px disc | Rest: secondary. It **replaces the meaningless 40 × 9 grey pill.** |
| **Battery Save** | 20 | Rounded body 16 × 10 (radius 2) plus a 2 × 4 nub; filled at 50% | Always `status.warning` (a mode indicator; not a target). |
| **Wi-Fi** | 16 | 3 filled bars, 3 px wide with 2 px gaps, heights 4 / 8 / 12, bottom-aligned | Connected: `status.success`, all filled. **Down: `status.error`, bars drawn as 1 px outlines** (the shape changes, not just the colour). |
| **Status dot** | 8 | A dot | Server OK: `status.success`, filled. Unreachable: `status.warning` **ring** (2 px). Unknown or booting: `text.tertiary` ring. |
| **Shuffle** | 20 | Two crossing arrows | Media control (§11.20). Rest: primary on a plate. |
| **Degree ring** | 8 | Two concentric circles, r 4 and r 3 (a 2 px ring), top-aligned to the cap height | `text.secondary`. Inline only. |

### 10.4 Placement

- **Icon-to-text gap:** `space.icon.text` = 8. Inline in a caption, it's 4.
- **Vertical alignment:** centre the glyph on the text's **cap-height centre**:
  `cy = y + ascent(font) − capH/2`, where `capH ≈ 0.73 × size` (Inter).
  - caption and label: cy = y + 13 − 5 = y + 8
  - body and headline: cy = y + 18 − 6 = y + 12
  - title: cy = y + 26 − 9 = y + 17
- **Over media:** every glyph and its text sits on a `color.plate` rectangle
  with a 4 px inset around the ink (§11.20).
- **Semantics:** one glyph means one thing everywhere. Don't reuse the gear
  for anything but Settings, or a chevron for anything but "goes deeper".

---

## 11. Components

The only components this device needs. Each lists its **purpose, anatomy,
dimensions, spacing, type, colour, states and behaviour**. Components that are
excluded on purpose are listed in §11.23.

### 11.1 Card

- **Purpose:** groups one idea: a metric, a readout, a list of related values.
- **Anatomy:** a surface, and optionally a section label (§11.2) and a disclosure chevron (if tappable).
- **Variants:**

  | Variant | Padding | Use |
  |---|---|---|
  | standard | 12 all round | Most cards |
  | compact | 8 vertical / 12 horizontal | Single-line or dense tiles (BTC, the weather strip). Minimum height 32. |
  | hero | 8 all round | Visual-first content that should dominate (the clock) |

- **Dimensions:** width is a column or a full width (§7.2). Height sums to the column (§7.2 rule 3).
- **Colour:** `surface.card`, `radius.md`, no border.
- **States:**
  - static
  - tappable: rest → pressed (`surface.raised`) → commit opens its overlay
- **Behaviour:** a tappable card shows a disclosure chevron (16 px, tertiary) at
  the right edge of its content box, vertically centred. The card reserves a 24 px
  column for it (the glyph plus `space.sm`). The **whole card** is the hit box. A card
  never contains a second target unless each target is ≥44 px and 8 px apart.

### 11.2 Section label

- **Purpose:** names a card or a group.
- **Anatomy:** `type.label`, UPPERCASE, 1–3 words, `text.secondary`.
- **Placement:** top-left at the padding origin, or baseline-aligned after a `numeral.lg` value
  (the `17%  5H` pattern), with a gap of `space.sm`.
- **Rules:** a card has at most one. It's never accent-coloured and never the
  largest text in its card.

### 11.3 Metric

- **Purpose:** shows a single number with its meaning.
- **Anatomy:** a value (numeral token), an optional unit (caption, secondary, shared
  baseline), an optional label (label or caption, secondary), and an optional flag.
- **Sizes:**

  | Size | Font | Use |
  |---|---|---|
  | hero | `numeral.hero` | Weather temperature |
  | lg | `numeral.lg` | 5h and week %, digital time |
  | md | `numeral.md` | BTC, stat % |
  | sm | `numeral.sm` | Forecast values |

- **Colour:** the value's colour is its **data-series colour** (usage = accent) or
  `text.primary` for neutral data. The label and unit are always secondary.
- **States:**
  - known
  - unknown: `--` in tertiary, same font
  - flagged: the pace flag `!` (headline, `status.error`) followed by the projection
    (`86h`, body, secondary), `space.sm` after the label. It appears **only** when
    usage is more than pace + 2 points (the existing deadband, `pages.cpp:563`).

### 11.4 Meter (progress bar)

- **Purpose:** shows a fraction of a known extent.
- **Anatomy:** a track (100%) and a fill.
- **Heights:** `meter.sm` 6, `meter.md` 8, `meter.lg` 12. Radius `full`.
- **Fill minimum** = the meter's height, so a non-zero value shows as a full-height
  round-ended stub and never a 1 px sliver.
- **Colour:** the track is `fill.track`; the fill is the data-series colour.
- **States:**
  - known
  - unknown: the track alone, with no fill and no stub
  - over-threshold (system meters only): the fill becomes `status.error` at ≥80%, with the %
    label beside it carrying the number, so colour isn't the only cue
- **Rules:**
  - Width is the full content width of its container.
  - One track colour everywhere. Today's pure-black pace track (`COL_TRACK_BLACK`) and
    invisible project track (`COL_SURFACE` = bg) are both replaced by `fill.track`.

### 11.5 Paired meter (usage against pace)

- **Purpose:** the product's core idea: *am I using faster than time is passing?*
- **Anatomy:** a usage meter (`meter.md`, `data.usage`) with a pace meter (`meter.sm`,
  `data.pace`) 4 px below it (`space.stack.tight`), both the same width and left edge.
- **Behaviour:**
  - When the Pace bars setting (today's Show Countdown) is off, the pace meter is removed and the
    space closes up.
  - The flag logic lives in the Metric (§11.3), not here.
  - An optional highlight sweep plays on a new poll (§12.7).

### 11.6 Bar chart

- **Purpose:** compares a small series (7-day trend, top projects).
- **Anatomy:**
  - bars in `data.usage`, `radius.sm`, with a minimum height of 4
  - a caption-sized axis in tertiary; "today" in `accent`, per the current-item rule
  - values in `numeral.sm`, secondary
- **Spacing:** the bar gap is `space.sm` or larger. Horizontal (project) bars are
  `meter.md` height on `fill.track`, with the label above in body and the value
  right-aligned in caption.
- **Dense single-line variant** (the Projects page, where four ranked rows and the
  trend card must share 272 px): the name (body) sits in the left column, the meter
  runs from the right column's edge (x 188) vertically centred on the name's cap
  height, and the value (caption) is right-aligned on the name's baseline. Rows step
  by line box + `space.stack` (31).
- **Rules:** no gridlines or axis lines. The baseline is implied by aligned bar feet.

### 11.7 List row

- **Purpose:** one entry in a modal list (Settings).
- **Anatomy:** a label (headline, primary, sentence case), a trailing value
  (body, secondary, right-aligned), and a trailing affordance.
- **Dimensions:** height `touch.comfortable` (56), full content width, `radius.md`,
  `surface.card`. The gap is `space.list.gap` (8), so the step is 64. The label
  inset is 16 (`space.lg`). The trailing element sits 16 from the right edge.
- **Variants:**

  | Variant | Trailing element | Tap does |
  |---|---|---|
  | **navigation** | Current value (e.g. `75%`, `20s`, `Auto`) + disclosure chevron | Pushes its detail screen |
  | **toggle** | A 2-state pill: 52 × 28, `radius.full`, filled `accent` with "On" in `text.onAccent`, or filled `fill.track` with "Off" in secondary. The label names the state, so it doesn't rely on colour alone. | Flips in place: **one tap, no detail screen**. Used for every Off/On setting. |
  | **action** | No value; the label is `status.error` for destructive actions | Pushes an action detail with the arm button (§11.9) |

- **States:** rest → pressed (`surface.raised`) → commit. The toggle pill updates on
  the commit present. There is no disabled state in v1.
- **Behaviour:** the whole row is the hit box. Scrolling beats tapping: moving more than
  `touch.slop` cancels the tap (§9.3).

### 11.8 Option grid (segmented choice)

- **Purpose:** choose one of 2–8 discrete values (a Settings detail screen).
- **Anatomy:** cells in a grid (§7.2 column table), with ≤5 cells per row and
  ≤2 rows. Each cell holds a centred label (headline).
- **Dimensions:** cell height 64 (so ≥ `touch.comfortable`), `radius.md`, gap
  `space.sm`. The grid starts `space.xl` (24) below the subtitle.
- **Colour:**

  | State | Fill | Label |
  |---|---|---|
  | unselected | `surface.card` | primary |
  | selected | `accent` | `text.onAccent` |
  | pressed | `fill.pressed` (or `accent.pressed` if selected) | unchanged |

- **Behaviour:** commit on up; applies **immediately** and persists silently
  (the existing `queueConfigSave` path). There's no save button. Selection moves
  on the same present, as a cut.
- **Mapping:** cells run from low to high, left to right and then top to bottom, in the
  same direction as the value they set (0% → 100%, 5 s → 5 min, Off → On). Nominal sets
  (like Boot page) follow the carousel order, so the grid mirrors what it controls.
- **Rules:** a value that isn't an exact preset highlights nothing (as today). For
  more than 8 options, use a list instead.

### 11.9 Button

- **Purpose:** a single action (Restart, Forget Wi-Fi, and any future action).
- **Variants:**

  | Variant | Rest | Pressed | Label |
  |---|---|---|---|
  | primary | `accent` fill | `accent.pressed` | headline, `text.onAccent` |
  | secondary | `surface.card` | `fill.pressed` | headline, primary |
  | destructive | `surface.card` fill, `status.error` label | `fill.pressed` | headline, `status.error` |
  | destructive (armed) | `status.error` fill | — | "Tap again", `text.onAccent` |

- **Sizes:** `button.md` is 44 tall; `button.lg` is 56 tall. Width is a grid-cell width or the
  full content width (a lone action takes the full width, as today). `radius.md`.
- **Arm pattern** (destructive): the first commit arms it; a second commit within 4000 ms
  executes; a timeout or leaving the screen disarms it. The hint line below
  reads "Tap twice to restart the board". This is the device's only confirmation.

### 11.10 Icon button

- **Purpose:** a chrome action with a glyph: close, back, sleep, gear, shuffle.
- **Anatomy:** a glyph (`icon.md`, 20 px) centred in a ≥44 × 44 hit box. There's no
  visible container at rest.
- **Pressed:** a `fill.pressed` disc (40 px, `fillSmoothCircle`) behind the glyph,
  and the glyph turns primary.
- **Component box:** 44 × 44 centred on the glyph. At a screen edge it's clipped to the edge and
  slides inwards to stay 44 px (for example the sleep button, x 436..479).
- **Back is glyph-only.** The title beside it names the current screen (§11.14), so a
  "Back" label would repeat information.

### 11.11 Page indicator

- **Purpose:** wayfinding in the carousel: *where am I, how many are there, which way now.*
- **Anatomy:** 6 dots, 6 px across, 6 px gaps (66 px wide), centred at x 240
  in the status strip. The current dot is `text.primary`; the others are `fill.track`.
  It replaces the `1 / 6` text.
- **Behaviour:** not a target. It updates on the commit present of a page change,
  before the slide starts.

### 11.12 Status strip (footer)

- **Purpose:** persistent system status and the entry points to Settings and Device Stats.
- **Box:** y 288..318 (31 px) plus the progress hairline at y 319. Canvas background. It appears only on
  the dashboard pages, never on overlays, sheets or full-screen media.
- **Anatomy, left to right:**

  | Zone | Contents | Target |
  |---|---|---|
  | **Health cluster** (glyphs x 8..39) | Status dot (x 8..15), then 8 px, then the Wi-Fi glyph (x 24..39) | Hit x 0..55, y 280..319 (56 × 40) → opens **Device Stats**. Pressed state: a `fill.pressed` pill behind both glyphs. |
  | **CPU** (x 47) | `type.caption`: "CPU" in `text.secondary`, then the render-loop duty-cycle percentage in `text.primary` (e.g. "CPU10%") -- no space, no bracket | none |
  | **Centre** | Page indicator | none |
  | **Settings** (x 452, centred) | Gear glyph | Hit x 424..479, y 280..319 (56 × 40) → opens **Settings** |

- **Vertical:** glyph centres at y 303. Any caption text in the strip uses top y 295.
- **ROM/RAM stay out** of the strip; both remain developer telemetry available in Device Stats.
- **Pinned during the carousel slide.** "Persistent" means visually static: the strip
  (and the hairline under it) stays put while the page above it slides horizontally,
  rather than sliding off with the outgoing page and back in with the incoming one.
  Settings' push/pop has no strip, so nothing is pinned there.

### 11.13 Progress hairline

- **Purpose:** ambient "next poll in…" status.
- **Anatomy:** 1 px, `text.secondary`, along y 319, filling left to right over the poll interval.
  It is the **one** element allowed inside the screen margin, because it's a
  system edge indicator.
- **Rules:** re-presents as soon as the fill would grow by one physical pixel --
  its finest visible step -- floored at `motion.progress.maxHz` so a short poll
  interval can't out-pace the render loop (§12.7). It is hidden while offline
  and when the Progress Bar setting is off.

### 11.14 Modal header

- **Purpose:** wayfinding inside a modal stack (Settings list and detail).
- **Box:** y 0..43.
- **Anatomy:**
  - left: an icon button in the close slot (§7.3): **close** at the root of the stack,
    **back** deeper in
  - after the button: the title in headline, primary, sentence case ("Settings",
    "Brightness")
  - right: the system corner
- **Title position:** text top at y 11, so the headline is optically centred in 44 px. The title starts at x 60.
- **No divider under the header.** A list below it has its viewport at y 44..319, with its
  first row resting at y 52. Once scrolled, the rows passing through y 44..51 get the
  scroll-edge shade (§8.4), so the content visibly slides under the header.

### 11.15 Detail screen (Settings leaf)

- **Anatomy:**
  1. the modal header
  2. a subtitle: body, secondary, sentence case, y 52
  3. the option grid or an action button, 24 below
  4. an optional hint: caption, secondary, `space.lg` below the grid, centred. **Only when
     it adds something the control can't show** ("Tap twice to restart the board",
     "23:00–07:00, dims to 25%"). Never a "Tap to…" instruction. Those hints exist today
     because the mapping was weak: a correctly styled selected cell doesn't need one.
- **Behaviour:** Back pops. Changes are already applied, so leaving never loses anything.

### 11.16 Badge

- **Purpose:** a compact coded value (AQI).
- **Anatomy:** a pill with a number: height 24, horizontal padding 6, `radius.sm`, headline.
- **Colour:** the fill comes from the external AQI scale. The text is gray.0, except white on
  Hazardous (both chosen for contrast; `aqiColors()` today).
- **Placement:** on its own row under the date in the clock readout (§7.4), or right
  of the text in the Weather hero. Keep 8 px clear of the corner slots.

### 11.17 Toast

- **Purpose:** confirms an action that has **no other visible result**
  (for example "Restarting…" becomes `Restarting...`, or `Wi-Fi forgotten, restarting...`).
- **Anatomy:** `surface.raised`, `radius.sm`, height 36, body text in primary,
  12 px horizontal padding. It sits centred horizontally, 8 px above the status strip (or
  above the bottom margin on screens without a strip).
- **Behaviour:** appears and disappears as a cut. It stays for `motion.toast` (2500 ms), is not
  a target, and doesn't block touches beneath it. One at a time; a new one replaces the old.
- **Rule:** never for things the UI already shows (a selected option, a toggled pill).
  Use at most one toast per user action.

### 11.18 Analog clock (product-specific)

- **Anatomy:**
  - a face: 2 px ring, `text.primary`
  - 12 ticks: 2 px, from r−2 to r−8, `text.primary`
  - hour hand 6 px at 0.5 r, minute hand 4 px at 0.8 r, both `text.primary`
  - the pace wedge from the hour hand to the reset, `data.pace.wedge` (Pace bars setting only)
  - the reset radius, 1.6 px, `data.pace`
  - the second hand, 2.4 px, `accent` (the current-item marker)
  - a hub, r 3
- **Size:** r 76 on the grid (§7.4). The hand ratios scale with r. The face is an
  anti-aliased ring (`aaRing`), not two filled discs: only the band's pixels are drawn.
- **Motion:** it ticks at 1 Hz (the one ambient motion on the status page, §12.7).

### 11.19 Note pane (product-specific)

- **Tokenizer colour mapping.** This follows the six-copy parity surface in `CLAUDE.md`. The rules stay
  the same; only the colours move to tokens:

  | Token | Match | Colour |
  |---|---|---|
  | `note.code` | inside a backtick span | `status.info` |
  | `note.keyword.bad` | `TODO`, `FIXME`, `BUG` | `status.error` |
  | `note.keyword.good` | `DONE`, `OK` | `status.success` |
  | `note.number` | numbers | `content.sun` (was pure yellow) |
  | `note.heading` | line begins `#` | `accent` |
  | `note.quote` | line begins `>` | `text.secondary` |
  | `note.marker` | list marker | `accent` |
  | `note.text` | everything else | `text.primary` |

- **Anatomy:** the card; the section label "NOTE"; and text in `type.mono.*` from the
  first row below the corner slots (y ≥ 40).
- **Empty:** the empty state (§13.4): "No note yet" / "Edit at :8787/note".
- **Parity:** a colour change here touches the S3 `pages.cpp`, `simulator-s3.html` and
  `note.html`'s preview. The CYD copies keep their own palette, since only the
  *rules* are shared across boards.

### 11.20 Media pane (cats)

- **Purpose:** full-bleed or pane-filling animated content. It is also the offline screen.
- **Anatomy:** the GIF (cover-fit in a pane, 1:1 centred full-screen), plus overlays on plates:
  - the reset readout, bottom-left: **headline** text (one weight up from body, §8.4) on a
    `color.plate` box with 8 / 6 padding (today's `drawSessionResetOverlay`)
  - the system corner glyphs on plates
  - the **media control** (only while Cat Shuffle is Fixed): a **shuffle icon button**,
    bottom-right of the media area, glyph on a 32 px plate disc, hit box 48 × 48.
    It **replaces the invisible middle-third and left-half "next cat" zones.**
- **Tap acknowledgement:** the shuffle button's pressed state is the
  acknowledgement. There is no border flash.
- **Rule:** nothing is drawn over media without a plate.

### 11.21 System corner glyphs

These are defined in §7.3 and §10.3. They are drawn **last** on every screen, including
settings, overlays and media. Sleep is checked first in hit-testing (§9.2).

### 11.22 Sheet

- **Purpose:** the modal level above the carousel: Weather, Device Stats and Settings.
- **Anatomy:** the full 480 × 320 canvas; the modal header band (the close slot, a title or
  hero, the system corner); content. There's no status strip.
- **Enter:** rises from the bottom edge, where its trigger is (§12.5), with
  `motion.spring.sheet`. The page behind steps through the scrim (§8.4).
- **Dismiss**, always downward along the path it came in:
  - the close glyph
  - a vertical drag down (§9.4), with velocity handoff
  - **tap anywhere**, on read-only sheets only (Weather and Device Stats have no other
    targets). Settings has targets, so a stray tap there must never close it.
- **States:** rest; dragging (1:1, with the scrim tracking the travel); settling (a spring,
  interruptible, so grabbing it mid-flight takes over from where it is).
- **Stacking:** inside a sheet, depth moves on X (push to a detail). Only one sheet is
  ever open; a sheet never opens another sheet.

### 11.23 Excluded components (and why)

| Component | Why not |
|---|---|
| **Text input field** | No keyboard, and a 3.5-inch single-touch keyboard is error-prone. Text is entered in a browser: the AP portal for Wi-Fi, and `note.html` for the note. |
| **Slider** | Discrete presets (≤8) tap more reliably than dragging a thumb at 30 Hz with no velocity from the controller, and they map exactly to stored values. Revisit only for a setting that truly needs a continuum. |
| **Toggle switch** | The toggle row's labelled On/Off pill (§11.7) does the same job in one tap and doesn't rely on colour or a knob position. |
| **Tab bar** | Six peer pages would cost 44 px on every page, or 16% of the height. The carousel, page dots and tap halves cover navigation. |
| **Dialog / alert** | The arm pattern (§11.9) is the only confirmation. Modal dialogs would teach tap-through. |
| **Skeleton or shimmer loaders** | Shimmer is continuous full-frame motion (§1). Use `--` placeholders (§13.4). |
| **Tooltip, hover card, context menu** | There's no hover and long press is reserved. |

---

## 12. Motion

### 12.1 The constraint

Every animated frame is a **full 300 KB present**: ~16 ms of bus time plus waiting for the
next TE edge, which gives about 30 fps. A 10 px spinner costs exactly as much as a page
slide. So motion is spent only where it **explains cause and effect, or space**, and it
must never cost the user the ability to touch.

### 12.2 Springs are the motion model

All spatial motion is a **spring**, defined by Apple's two designer parameters: the damping
ratio ζ (1.0 means no overshoot) and the *response* in seconds. Response isn't a duration:
a spring has no fixed length, and its settle time follows from the parameters.

```cpp
// One step per loop() pass. x = presentation offset (px), v = px/s, w = 2*PI / response.
// dt = real elapsed time, clamped to 50 ms, integrated in 4 sub-steps (semi-implicit Euler).
float a = -w * w * (x - target) - 2 * zeta * w * v;
v += a * dt;  x += v * dt;
// Done when |x - target| < 0.5 px and |v| < 15 px/s: snap to target, then present once more.
```

| Token | ζ | Response | Reaches 90% | Settles (30 fps) | Use |
|---|---|---|---|---|---|
| `motion.spring.standard` | 1.0 | 0.25 s | 167 ms | ≤12 frames | Page slide, push and pop, scroll spring-back, a sheet closed by a tap |
| `motion.spring.sheet` | 1.0 | 0.30 s | 200 ms | ≤14 frames | Sheet opening (a larger surface should feel heavier) |
| `motion.spring.flick` | 0.8 | 0.30 s | 133–167 ms | ≤12 frames, 4–6 px overshoot | **Only** after a release with momentum (\|v\| > `touch.flick.bounce`) on a sheet. Never on pages (§12.6). |

These figures come from simulating the integrator at 30 fps over 480 and 280 px of travel.

Why springs rather than today's 180 ms ease-out cubic:

- **They can be interrupted by design.** Retargeting changes only `target`. Position and
  velocity carry on, so there's no jump and no sudden velocity change.
- **Velocity handoff comes for free.** A released drag simply sets `v`.
- **They're smoother at 30 fps.** Starting from rest, the spring's largest single-frame step
  for a 480 px slide is 141 px. The cubic's first frame jumps 220 px, which is 46% of the
  screen in 33 ms, and that strobes.
- **The cost:** a spring's tail makes about 12 presents instead of 6. They don't block,
  and nothing else is presenting at the time, so what they use is idle time. The last
  ~5 frames move less than 10 px.

**Non-spatial tokens**

| Token | Value | Use |
|---|---|---|
| `motion.instant` | The next present (≤33 ms, plus the present) | Every state change: pressed, selected, toggled, value updates. **These are cuts.** |
| `motion.lean` | 8 px, `motion.spring.standard` | The page-navigation pressed hint (§12.5) |
| `motion.sweep` | 600 ms, linear | The per-poll pace highlight. It moves at constant speed because it's light passing over, not an object settling. |
| `motion.pulse` | 3 frames | The status-dot pulse on each successful poll |
| `motion.fade` | 3 frames (25 / 50 / 75%) | The reduced-motion replacement for springs (§12.9) |
| `motion.ambient.maxHz` | 1 Hz | Continuous motion that isn't content (the second hand) |
| `motion.progress.maxHz` | ~30 Hz (floor only) | The hairline re-presents on every 1 px of growth; this only caps how often, for a poll interval short enough that 1 px would arrive faster than the loop can present |
| `motion.toast` | 2500 ms | How long a toast stays |
| `motion.ack` | 100 ms (3 frames), non-blocking | Acknowledges a tap on a target with no visible control (§12.8). Unused after migration. |
| `motion.backlight.*` | §12.10 | Backlight fades |

### 12.3 Interruptibility

- **No transition locks out input.** `pageTransitionRun()`'s blocking loop becomes a
  motion state that `loop()` steps once per pass, reading touch between presents.
- **Start from the on-screen value.** A touch-down during motion stops the object where
  it *appears on screen*, not at its target, and a drag carries on from there with the grab
  offset. A tap retargets from the current position and velocity.
- **Carousel retargets.** Tapping "next" again mid-slide retargets to the page after, and
  the velocity carries over. Doing that without a jump needs the strip compositor to read
  from three frame buffers (current, next, and the one after: 900 KB of PSRAM). Until then,
  the minimum acceptable behaviour is to finish the slide in flight on the same present and
  start the next with the carried velocity. The tap is never dropped; only its first frame jumps.
- **2D motion is split into independent X and Y springs.** In this system only one axis
  moves at a time today, but the rule holds for anything that moves diagonally.

### 12.4 Velocity handoff and projection

- **Handoff.** On release, `v` is the release velocity (§9.4) and the spring starts with it.
  This integrator takes absolute px/s; an API that takes normalised velocity would be
  given `v / (target − x)`.
- **Projection.** Pick the target from where the motion *would come to rest*:
  `x + project(v)`, with `project(v) = (v / 1000) · d / (1 − d)` and d = `touch.decel`.
  Then snap to the nearest valid stop (page 0 or ±1; sheet open or closed). Above
  `touch.flick.commit`, commit vs reverse is decided by the velocity's **direction**, not
  its position.

### 12.5 The spatial model

| Change | Motion | Axis and path |
|---|---|---|
| Next or previous page | `motion.spring.standard` | **X.** Next comes in from the right, previous from the left. A swipe tracks the finger 1:1. |
| Push a detail (Settings list → detail) | `motion.spring.standard` | **X.** The detail comes in from the right. Back sends it out to the right, and a rightward swipe tracks 1:1. |
| Open a sheet (Weather, Device Stats, Settings) | `motion.spring.sheet` | **Y.** Rises from the bottom while the page behind dims 100 → 75 → 50% (§8.4). |
| Dismiss a sheet | `motion.spring.standard`, or `.flick` after momentum | **Y.** Drops back down along the path it came up. |
| Sleep and wake | Backlight fade (§12.10) | — |
| Data updates | Cut | Values change in place. |

- **Anchored to the source.** Every sheet's trigger sits in the bottom band: the health
  glyphs (bottom-left) open Device Stats, the Weather card (bottom-right) opens Weather,
  and the gear (bottom-right) opens Settings. So rising from the bottom edge means rising
  from the thing you tapped.
- **Hint in the direction of travel.** On touch-down on a page half, the page leans
  `motion.lean` (8 px) toward where it will go. Pressing the right half reveals the first
  8 px of the next page at the right edge. Releasing commits, and the slide continues
  *from* the 8 px, the on-screen value. Sliding off cancels, and the page springs back.
  This lean *is* the page-navigation pressed state. The cost is rendering the neighbour
  page into a PSRAM frame on touch-down (the same frame a swipe uses), plus one present.
- **Implementation.** Vertical travel needs a `displayPresentSlideV` counterpart to
  `displayPresentSlide`. It costs the same, because the offset is applied in the rotate
  copy that happens anyway. The incoming frame is rendered off-screen first, using the
  existing `pageTransitionBegin` / `presentHold` mechanism.

### 12.6 Frame-level smoothness

- **Keep each frame's movement small:** ≤150 px per frame for full-width moves. From
  rest, `motion.spring.standard` peaks at 141 px.
- **Whole-pixel offsets, rounded** (not truncated), so nothing drifts a pixel when it settles.
- **Every animated present waits for the TE edge**, as today, so there's no tearing.
- **60 Hz if the present fits.** A present takes about 16 ms (fill plus transfer) against a
  TE period of about 16.5 ms. If it fits within one TE period, springs can step at 60 Hz,
  which halves each frame's movement. The tokens don't change, only `dt` (§18.4).
- **Pages never overshoot.** Overshooting a page would expose a strip of canvas beyond
  the incoming frame, because there's no third frame to show there. So pages always
  use ζ = 1.0.
- **No motion blur.** That would mean resampling every pixel on every frame, which the bus
  can't afford. Fast moves rely on short travel instead.

### 12.7 Ambient and feedback motion

| Element | Rule |
|---|---|
| Clock second hand | 1 Hz, as today. This is the status page's one ambient motion. |
| Cat GIFs | Content, so exempt. They play at their own frame delays. |
| Progress hairline | Re-presents on every 1 px step -- ~24 Hz at the default 20 s poll (§15.1) -- floored at `motion.progress.maxHz` (~30 Hz) so a 5 s poll (96 px/s) can't out-pace the loop. A fixed low-Hz cap (4, then 8 Hz) made the fill visibly jump several px at once; matching the step to what the loop can actually deliver removes the jump instead of just slowing it. |
| Status dot | **Steady**, with one `motion.pulse` (r 4 → 5 → 4) on each successful poll. That confirms data arrived; today's 1 Hz blink says nothing. |
| Pace-bar highlight (the "shine") | **One `motion.sweep` per successful poll**, across the pace fills, then still. It means "fresh data". Never the perpetual 2.6 s loop, which at about 0.4 Hz is close to the slow oscillations Apple's accessibility guidance tells you to avoid. |
| Hourly signal | Replace the 6 s full-screen inversion with a **backlight breath** (§12.10). An inversion is an abrupt jump in brightness across the whole screen. Until it's migrated, the inversion is an exception for alerts only, and no other feature may flash the screen. |

### 12.8 Press feedback

- The pressed state is a cut on the next present (§9.3), and so is the release. Nothing on
  the press path animates, except the page lean, which is spatial.
- **Acknowledging an invisible target** only applies where there's no control to press.
  After §11.20 there are none. If one is ever needed: a 2 px `text.secondary` border for
  `motion.ack`, **non-blocking**, stepped by `loop()`. Never a `delay()`.

### 12.9 Reduced motion and accessibility settings

Reduced motion doesn't mean no feedback. It means a gentler equivalent that doesn't
cause motion discomfort. These are future Settings items, and they're cheap because every
motion reads its tokens.

- **Reduce Motion** replaces every spring slide and sheet with a **3-frame cross-fade**
  (`motion.fade`: 25, 50, 75%).
  - The blends use the same shift-and-mask operations as §8.4. For example, 50% is
    `((a >> 1) & 0x7BEF) + ((b >> 1) & 0x7BEF)`.
  - They're done in the rotate copy while reading the same two source frames a slide reads,
    so a fade costs about what a slide does.
  - Reduce Motion also drops the lean, the scrim steps, the sweep and the pulse. Pressed
    states and backlight fades stay, because they aren't vestibular.
- **Increase Contrast** changes three tokens:
  - `text.secondary` becomes `0xBDF7` (rgb 189,190,189; ≈9:1 on a card).
  - Cards gain a 1 px `gray.4` outline.
  - `fill.track` becomes `gray.4`.

  The meaning is unchanged; only the separation is stronger.
- There's nothing to reduce for transparency: there isn't any (§8.4).

### 12.10 The backlight and sound: channels that cost no frames

The backlight is driven by PWM (5 kHz). Changing it costs **no presents and no bus time**,
so it's the one channel where smooth, continuous change is free. It's also where
brightness jumps are easiest to avoid.

| Token | Fade | Use |
|---|---|---|
| `motion.backlight.sleep` | 200 ms to 0, then DISPOFF / SLPIN | Entering sleep |
| `motion.backlight.wake` | DISPON, then 200 ms up to the target | Waking. The frame is still intact, so the screen is effectively there at once, and the fade starts on the touch itself. |
| `motion.backlight.night` | 2 s | Night Mode starting and ending (today these are instant cuts) |
| `motion.backlight.adjust` | 150 ms | Picking a new brightness, so you see the change you chose |
| `motion.backlight.breath` | Dip to 40% and back over 1 s, three times | The hourly signal (replaces the inversion) |

**Sound** is unused. If it's ever added, it follows Apple's three rules:
- **Causality:** sound only for meaningful events, like a destructive action executing or the hourly signal.
- **Harmony:** it fires on the same frame as the visual or backlight change.
- **Utility:** the default volume in Settings is Off, because this is a quiet desk device.

---

## 13. Patterns: navigation, feedback, states

### 13.1 Navigation model

```
                +-------------------- CAROUSEL (X axis, wraps) ---------------------+
                | Status | Projects | Limits | Cats | Mixed | Note |  (6 peers)      |
                +--------------------------------------------------------------------+
                     |  tap card / strip target               |  gear
                     v  (Y axis: sheet up)                    v  (Y axis: sheet up)
              +--------------+  +---------------+     +------------------+  push (X)  +-------------+
              | Weather      |  | Device Stats  |     | Settings (list)  | ---------> | Detail      |
              | overlay      |  | overlay       |     | modal root       | <--------- | (option grid)|
              +--------------+  +---------------+     +------------------+    back    +-------------+
  SYSTEM: sleep (corner, any screen) . offline = media screen . AP setup . boot splash
```

**Gestures by level.**

| Level | Tap | Drag or swipe |
|---|---|---|
| Carousel | Page halves | Horizontal swipe |
| Sheet | Close glyph, and tap-anywhere on read-only sheets | Drag down to dismiss |
| Settings detail | Back | Swipe right to go back |
| Lists | — | Scroll with momentum and rubber-banding |

Every gesture has a visible tap equivalent, so swiping speeds things up but is never
required.

**Wayfinding.** Every screen answers four questions.

| Question | Answer |
|---|---|
| Where am I? | Page dots, or the modal header title |
| Where can I go? | Left or right halves, chevrons, strip glyphs |
| What's here? | Section labels |
| How do I get out? | Close or back, and for overlays, tap anywhere |

**Context is kept:**
- Closing an overlay returns you to the exact page underneath.
- Settings remembers its scroll position while it's open, and resets on the next open (as today).
- The boot page setting's Auto mode resumes the last page (as today).

### 13.2 Feedback

| Kind | Where | Form |
|---|---|---|
| **Status** (ongoing) | Status strip, system corner | Status dot and Wi-Fi glyph (§10.3); Battery Save glyph; progress hairline |
| **Completion** | At the control | The selected cell moves or the toggle flips on the commit present; a toast only when nothing visible changes |
| **Warning** (risk ahead) | At the metric | The pace flag `!` plus a projection; warning-coloured status glyphs |
| **Error** (something failed) | In place of the content | The error state (§13.4). Never a modal. |

### 13.3 Settings behaviour

- Every change applies immediately and persists in the background
  (`queueConfigSave` → `networkTask`). There's no Save or Cancel.
- Destructive actions use the arm pattern only.
- The list rows show current values (§11.7), so reading a setting takes zero taps.
- Order the list by frequency of use: first display (Brightness, Night mode, Rotation), then content
  (Boot page, Cat shuffle, Pace bars, Show AQI, Hourly signal, Poll progress),
  then system (Poll interval, Battery save, Pixel shift), and **destructive actions last**
  (Forget Wi-Fi, Restart). Today, Restart and Forget Wi-Fi sit in the middle of the list.

### 13.4 Empty, loading, error and offline states

| State | Form |
|---|---|
| **Loading / not yet received** | Components render with their final layout and `--` placeholders in tertiary. The status dot shows a tertiary ring. No spinners on dashboard pages. |
| **Empty** (valid, but nothing to show) | Centred in its container: a title (headline, primary), `space.sm`, a description (caption, secondary, telling you the next step). Example: "No note yet" / "Edit at :8787/note". A full-screen empty state may use `type.display` for its title, in `text.secondary`, **not accent**. |
| **Error** (something is missing or failed) | The same structure as empty, with the title in `status.error` headline. Example: "No SD card" / "Insert an SD card with /cats/ GIFs". |
| **Offline** (server unreachable for 60 s or more) | A *mode*, not an error: the media screen (cats) with the reset readout plate. The status strip isn't shown, so the corner glyphs and the reset plate carry the status. Returning online cuts back to the page you were on. |
| **Boot** (firmware-only) | The splash BMP, then a spinner. The spinner uses `text.secondary` and is the only spinner in the system. |
| **AP setup** (firmware-only) | A full-screen instruction layout: numbered steps in body; the values to type (SSID, IP) in `type.title` **primary**; waiting status in caption, secondary; `space.xxl` between steps. |

---

## 14. Hardware budget and trade-offs

| Decision | Cost on this hardware | Why it's worth it, or the rule it produces |
|---|---|---|
| Cards filled with gray.1 instead of outlined | One `fillSmoothRoundRect` per card at 1 Hz; negligible | Calmer, groups without lines, and survives night dim better than 1.5:1 outlines |
| AA round rects and circles | Per-edge alpha blending; microseconds per shape at 1 Hz | Crisp corners at 166 ppi. **Needs the simulator's `gfx` to add the smooth variants first** (parity). |
| Spring page slide | About 12 non-blocking full presents (the last ~5 move <10 px); touch is read between them | Explains where pages are, can be interrupted, and hands off velocity. It strobes less than the 6-frame cubic (141 vs 220 px largest step). |
| Vertical sheet | The same, plus a `displayPresentSlideV` variant | A distinct axis for depth. The cost equals the page slide. |
| Scrim and scroll-edge shade | A shift and mask per pixel inside the rotate copy that already runs | Apple's "dim to focus" without alpha, at no measurable cost. |
| Lean hint | One neighbour-page render into a PSRAM frame per navigation touch, plus one present | Gives page navigation a pressed state that points where it will go. The frame is reused by the slide and by a swipe. |
| Three-buffer carousel strip | +900 KB PSRAM in total (of 8 MB) | Retargeting mid-slide with no jump. The two-buffer fallback is acceptable (§12.3). |
| Backlight fades | None: PWM only, no presents | Smooth sleep, wake, night and hourly signals with no abrupt brightness jumps. |
| Reduce Motion cross-fade | About the same as a slide (blend inside the rotate copy) | A gentler equivalent, not the removal of feedback. |
| Swipe paging | +300 KB PSRAM (the neighbour page, shared with the lean), about 30 presents a second while dragging | 1:1 direct manipulation. Tap halves stay primary. |
| Momentum scroll and rubber band | Velocity ring buffer, plus about 30 presents a second while coasting (d = 0.995 keeps coasts short) | Settings is about 3.3 screens long; a hard stop at the ends reads as frozen. |
| Continuous shine sweep (**deprecated**) | About 30 presents a second forever on 3 of 6 pages, which keeps the CPU duty and bus busy | Replaced by one sweep per poll. |
| Progress hairline at 1 px/present, floored at ~30 Hz | About 24 presents a second at the default 20 s poll, same as an uncapped naive fill | Every present shows real 1 px progress -- the smoothest this hairline can be -- while the floor still protects the loop from a 5 s poll's 96 px/s. |
| Blinking status dot (**deprecated**) | A present every second | Replaced by a steady dot plus a per-poll pulse. |
| Size-specific tracking and optical sizing | No extra flash (the advances change and the font count stays the same); a `make_vlw.py` run and simulator atlas regeneration; Inter 4 `opsz` source | Apple-style type that changes shape with size. The real cost is the parity round-trip, not memory. |
| No alpha, so no translucency or shadows | — | Depth comes from surface steps; media gets opaque plates. |
| Commit on touch-up | No measurable latency (the pressed state shows on down) | Makes accidental brushes harmless. |
| `touch.rearm` 120 ms from the last up | Needs verifying against controller bounce | Rapid preset taps stop getting dropped. |

---

## 15. Audit of the current UI, and migration

### 15.1 Findings

**Colour**

1. **Cards have no surface.** `COL_SURFACE == COL_BG` (`state.h:112`), so every card is
   a 1 px `COL_BORDER` outline (`pages.cpp:460`). The screen reads as a grid of
   boxes, and at 25% night dim the grouping disappears.
2. **The accent is overloaded.** Coral carries:
   - usage data (`pages.cpp:242`, `311`, `333`, `512`, `517`)
   - current-item markers (`1106`, `1156`, `851`)
   - "server unreachable" (`147`)
   - Settings chrome and titles: close X, "SETTINGS", chevrons, Back, the detail title and the scroll thumb
     (`settings.cpp:315`, `317`, `329`, `349–353`, `340`)
   - AP setup headings (`ap_setup.cpp:39–45`)
   - the "CATS" placeholder (`gif_player.cpp:247`, `251`)

   Chrome and warnings compete with the data.
3. **No warning colour.** "Server unreachable" borrows the accent (`pages.cpp:147`),
   so it looks like data rather than status.
4. **Three track treatments:** `COL_TRACK` grey 90 for usage, `COL_TRACK_BLACK` for
   pace (`pages.cpp:521`), and `COL_SURFACE` (invisible) for project bars (`pages.cpp:310`).
5. **Harsh pure yellow** `0xFFE0` (L 0.93) is used for the sun, lightning, the Battery Save icon and
   note numbers. At 13.4:1 against a 20:1 white it's nearly as bright as text and
   pulls the eye.
6. **Placeholders use secondary instead of tertiary.** `--` looks like a real
   value (`pages.cpp:920`, `938`, `974`).

**Typography**

7. **Four header styles:**
   - `USAGE LIMITS` / `TOP PROJECTS (7D)`: 13/650 white (`pages.cpp:258`, `293`)
   - `Device Stats`: 26/650 title case (`pages.cpp:998`)
   - `SETTINGS`: 13/650 accent, right-aligned (`settings.cpp:317`)
   - detail titles: 26/650 accent uppercase (`settings.cpp:353`)
8. **All-caps Settings.** Labels, subtitles and hints are uppercase, including 13 px
   hints such as "TAP A LEVEL TO APPLY" (`settings.cpp:245–286`). Case is also mixed inside
   labels ("CPU USAGE (render loop duty cycle, live)", `pages.cpp:1001`) and between
   "resets 16:30" and "in 4h:03m" (`pages.cpp:529–530`).
9. **Countdown format** `4h:03m` / `6d:16h` (`format.cpp:35`, `50`): the colon reads as a clock time.
10. **Two lines per reset** (`resets …` plus `in …`) take 46 px of each limit card
    (`pages.cpp:527–530`).

**Spacing and layout**

11. **3 px margins and 2 px gutters** (`state.h:135`) are the CYD's rhythm scaled up. Cards
    nearly touch, and the pixel-shift orbit (≤2 px) can bring ink within 1 px of
    the edge.
12. **Arbitrary insets:** 16 in limit cards, 9 in the note pane (`pages.cpp:591`), 15 on
    full-width pages, 18 in settings rows.
13. **Arbitrary row steps:** 51 (limits), 35 (projects), 30 (5-day), 62 (settings).
14. **The page split and the layout aren't related by any rule.** The split at x 240 sits
    1 px inside the right card, which starts at 241, by accident. The system makes the
    split explicitly a screen-half rule, separate from the grid (§7.2), so the layout
    can move without changing how navigation feels.

**Touch**

15. **Undersized targets:**

    | Target | Hit box | Glyph |
    |---|---|---|
    | Settings gear | 56 × 28 (`state.h:158`) | ~18 px |
    | Device Stats | 274 × 28 (`state.h:153`) | none |
    | Sleep | 64 × 36 (`state.h:181`) | a 40 × 9 grey pill with no meaning (`pages.cpp:211`) |

16. **Invisible targets:**
    - the Weather card (no affordance)
    - the CPU/ROM/RAM line (reads as telemetry, not a button)
    - the cat middle third (`state.h:173`)
    - the mixed-page left half of the pane, which **splits one visual surface into "next cat" and
      "next page"** (`state.h:507`)
17. **Commit on touch-down with no cancel** (`main.cpp:502`). Every brush navigates.
18. **The 350 ms debounce is measured from the previous down** (`state.h:85`), which
    drops deliberate rapid taps on presets.
19. **No pressed state anywhere.** The only acknowledgement is a **blocking** 60 ms
    white border flash (`pages.cpp:69–75`, `delay(60)`).

**Icons**

20. **Stroke weights range from 2 to 4 px:** gear and chevrons 3 px, close X 4 px, weather
    2k px (`drawWideLine` r = 1.5 / 2.0 / 1.0k). There's also a mix of aliased `fillCircle`,
    `fillRoundRect` and `fillTriangle` with AA `drawWideLine`.
21. **Mixed colour logic:** the footer gear is grey (`pages.cpp:122`), but the Settings chevrons and X
    are accent. Wi-Fi down shows **only as a colour change** (`pages.cpp:112`).

**Motion and complexity**

22. **Perpetual decoration.** The shine sweep (2.6 s loop, `pages.cpp:408`) and the
    progress line (a present each time the fill grows by a pixel: ~24 Hz at a 20 s poll,
    `pages.cpp:181–191`) keep the panel re-presenting about 30 times a second on static pages.
23. **The status dot blinks at 1 Hz forever** (`pages.cpp:145`). That is noise, and it says
    nothing about causality.
24. **The spatial model is inconsistent.** Pages slide (X), but Settings, the overlays and pushes
    are cuts.
25. **Footer clutter.** CPU/ROM/RAM percentages on every page (`pages.cpp:161–167`)
    are developer diagnostics in the glanceable view.
26. **Settings order:** Restart and Forget Wi-Fi sit in positions 5–6 among everyday settings
    (`settings.cpp:257–262`).

**Fluid interaction** (added after an apple-design review)

27. **Input is locked during every page slide.** `pageTransitionRun()` loops synchronously
    for 180 ms (`pages.cpp:54–62`), so a tap during it is lost. That breaks Apple's single most
    important rule.
28. **Fixed-duration, non-interruptible curve.** The ease-out cubic can't be retargeted or
    take a velocity, and its first frame jumps 220 px (46% of the screen in 33 ms), which strobes.
29. **Hard stops and no momentum.** The Settings list clamps at its ends
    (`settings.cpp:473–474`) and stops dead on release. There's no rubber band and no flick.
30. **Only taps, and no direct manipulation** anywhere except the list: pages, sheets and
    Back can't be dragged.
31. **Hints make up for weak mapping** ("TAP A LEVEL TO APPLY", "TAP TO TOGGLE",
    `settings.cpp:245–286`), and some labels are vague: "MIXED", "SHOW COUNTDOWN",
    "PROGRESS BAR" (`settings.cpp:134`, `275`, `284`).
32. **Abrupt brightness jumps.** The hourly signal inverts the whole screen for 6 s. Sleep,
    wake and Night Mode edges are instant backlight cuts (`applyEffectiveBrightness`,
    `settings.cpp:49–59`).
33. **Stroke-only depth.** There's no sense of layers: overlays replace the page with no
    dim, no travel and no connection to what triggered them.

### 15.2 What already fits the system (keep)

- One text API with baseline-correct placement, tabular digits and measured-width
  truncation (`fonts.h`; `pages.cpp:1066`).
- The pace deadband: warnings are earned by pace, never by level (`pages.cpp:563`).
- The arm pattern for destructive actions (`settings.cpp:418–427`).
- A spatial page slide with the right direction (`pages.cpp:36–64`). The curve becomes a spring; the direction logic stays.
- Drag-to-scroll with the grab offset kept and a tap/scroll decision on release (`settings.cpp:448–504`), which is the basis for every gesture in §9.4.
- A solid backing behind overlays on media (`gif_player.cpp:232`).
- The sleep pill checked first and drawn last.
- Immediate-apply settings with background persistence.

### 15.3 Migration map

| Current | Becomes |
|---|---|
| `COL_BG` | `color.bg.canvas` |
| `COL_SURFACE` (= bg) | `color.surface.card` (gray.1) |
| `COL_BORDER` around cards | *removed*; `color.separator` only for internal dividers |
| `COL_TEXT` / `COL_TEXT2` | `color.text.primary` / `.secondary` |
| `COL_TEXT2` for `--` | `color.text.tertiary` |
| `COL_ACCENT` on usage data | `color.data.usage` |
| `COL_ACCENT` on titles or chrome | `color.text.primary` (titles) / `.secondary` (glyphs) |
| `COL_ACCENT` for server unreachable | `color.status.warning` (ring shape) |
| `COL_GOOD` | `color.status.success` / `color.data.pace` |
| `COL_GOOD_50` | `color.data.pace.wedge` |
| `COL_WARN` | `color.status.error` |
| `COL_BLUE` | `color.status.info` / `color.data.system` / `color.content.rain` / `note.code` |
| `COL_YELLOW` (sun, bolt, note numbers) | `color.content.sun` (0xFE60) |
| `COL_YELLOW` (Battery Save icon) | `color.status.warning` |
| `COL_YELLOW` (AQI Moderate) | AQI scale `0xFFE0` (unchanged) |
| `COL_TRACK`, `COL_TRACK_BLACK`, `COL_SURFACE` tracks | `color.fill.track` |
| `COL_SHINE_*` | `green.shine.*`, used by the per-poll sweep only |
| Raw `0x0000` plates | `color.plate` |
| `FONT_XL/LG/MDB/MD/SMB/SM` | `type.display/title/headline/body/label/caption` |
| `LEFT_X 3`, `LEFT_W 236`, `RIGHT_X 241`, `RIGHT_W 236` | `layout.col.left.x 8` / `.left.w 172` (fixed reservation, content ends at x 180); `layout.col.right.x 188` / `.right.w 284` |
| `MIXED_GIF_X0 240`, `MIXED_GIF_W 240`, `NOTE_TEXT_W 220` | The right column: 188 / 284; note text width 260 |
| `CONTENT_Y1 290`, `FOOTER_Y0 292` | `layout.content.y1 280`, `layout.strip.y0 288` |
| `SLEEP_BTN_*` pill | Sleep icon button in `corner.slot.a` |
| `BATTERY_ICON_*` | Battery Save glyph in `corner.slot.b` |
| `SETTINGS_HIT_*` 56 × 28 | 56 × 40 (x 424..479, y 280..319) |
| `DEVICE_HIT_*` on the CPU text | Health cluster 56 × 40 |
| `CAT_ADVANCE_*`, `MIXED_CAT_ADVANCE_*` | The media control (shuffle icon button) |
| `TOUCH_DEBOUNCE_MS 350` (from down) | `touch.rearm 120` (from up) + commit on up |
| `DRAG_TAP_PX 8` | `touch.slop 10` |
| `flashTouchCenter()` (blocking) | Pressed states; `motion.ack` only if ever needed |
| Shine loop, `SHINE_PERIOD_MS 2600` | One sweep per successful poll |
| `progressTick` every loop | Capped at `motion.progress.maxHz` |
| 1 Hz status-dot blink | Steady dot plus a per-poll pulse |
| `1 / 6` text | Page indicator dots |
| All-caps Settings strings | Sentence case; `type.label` only for section labels |
| On/Off detail screens | Toggle rows |
| `fmtCountdown` `4h:03m` | `4h 03m` |
| `SLIDE_MS 180` + ease-out cubic, blocking | `motion.spring.standard`, stepped by `loop()`, interruptible |
| List clamp at ends | Rubber band + momentum (`touch.decel`) |
| Overlays that appear as a cut | Sheets that rise from the bottom with a scrim, and can be dragged to dismiss |
| No pressed state for page navigation | `motion.lean` |
| "Tap to…" hint strings | Removed; only informative hints remain (§11.15) |
| Labels "MIXED" / "SHOW COUNTDOWN" / "PROGRESS BAR" / "HOURLY FLASH" | "Status + cats" / "Pace bars" / "Poll progress" / "Hourly signal" |
| Hourly full-screen inversion | `motion.backlight.breath` |
| Instant backlight changes | `motion.backlight.*` fades |
| Inter 3 without `opsz`; tracking 0 everywhere | Inter 4 with `opsz = size`; tracking per §5.2 |

### 15.4 Phases

Each phase is a separate change. Each one keeps the firmware and simulator in
lockstep, and ends with a simulator screenshot and a board `grab_screen.py`
shot compared pixel for pixel (`CLAUDE.md` → Commands).

1. **Tokens.** Add `src/tokens.h` and the simulator's mirror block. Alias the
   existing `COL_*` and `FONT_*` names to them. **No visual change.** Add
   `fillSmoothRoundRect` and `fillSmoothCircle` to the simulator's `gfx`.
2. **Colour and type roles.** Card surfaces, the accent restricted to its three jobs, the warning
   and sun tokens, tertiary placeholders, one track colour, sentence-case strings, and the
   countdown format. The layout stays the same.
3. **Touch model.** Commit on up with slop, pressed states, `touch.rearm`, and the icon
   buttons (sleep, gear, close, back) with the hit sizes from the tables. The shuffle
   media control replaces the invisible zones. Delete `flashTouchCenter`. Add the gesture
   recogniser that runs candidates in parallel, the velocity ring buffer, and list momentum
   with rubber-banding.
4. **Grid.** Move to the 8 / 8 margins and gutters, the 272 px content area and the new status
   strip, one page at a time. Status page first (§7.4), then Limits, Projects, Note
   and Mixed, then the overlays and Settings (modal header, toggle rows, reordering).
5. **Motion.**
   - The spring integrator replaces `pageTransitionRun`'s blocking loop, and page slides become interruptible.
   - Sheets (`displayPresentSlideV` plus the scrim) with drag-to-dismiss; swipe back in Settings; the lean hint; horizontal page swipe.
   - Backlight fades and the hourly breath.
   - The per-poll sweep and dot pulse, and the hairline cap.
   - Then the Reduce Motion and Increase Contrast settings.
6. **Clean-up.** Remove the `COL_*` aliases, and update `CLAUDE.md`'s "Layout grid" section
   to point here.

### 15.5 Where the implementation differs from the spec

- **Projects rows** use the dense single-line variant (§11.6). With label-above rows,
  four projects and a readable trend card don't both fit in 272 px.
- **Boot page grid** puts its second row (3 cells) on the 3-column width, so
  "Status + cats" (110 px of headline) fits its cell.
- **Reduce Motion** also turns off the drags that would move a surface (page swipe,
  drag to dismiss, swipe back). Each has a tap equivalent, and a cross-fade can't
  track a finger. List scrolling stays.
- **Mid-slide carousel retarget** uses the §12.3 minimum: a touch-down finishes the
  slide in flight on that present, then starts the new gesture. Sheets *are* grabbed
  mid-flight and carry on from their on-screen position.
- **Optical sizing** is not done. `opsz` needs Inter 4's `InterVariable.ttf`, which
  isn't installed. Tracking per §5.2 is done (`make_vlw.py`'s `track_px`).
- **The offline screen** shows the reset plate too (§13.4). The old rule hid it offline.
- **Toggle rows** have no detail screen (§11.7), so copy that lived on the old detail
  screens (Night Mode's "23:00–07:00, dims to 25%") is no longer shown anywhere.
- **Drawing cost:** the status page takes ~40 ms to compose (PSRAM fills and AA text,
  as before the migration). Composites take ~23 ms (page) and ~30 ms (sheet) plus the
  TE wait, so springs step at ~30 fps and a transition is ~12 frames.

**Deprecated patterns** (don't reuse them in new work):
- outline-only cards
- an accent-coloured title
- invisible tap zones
- `delay()` in feedback
- perpetual ambient motion
- colour-only state
- all-caps sentences
- 2 px gutters between cards
- fixed-duration or blocking transitions
- hard scroll stops
- "Tap to…" hints
- inverting or flashing the whole screen

---

## 16. New-screen checklist

Before a screen is finished:

- [ ] Only semantic tokens are used. No raw colours, fonts or spacing numbers.
- [ ] One obvious most-important element: the largest and brightest thing, placed top-left or as the hero.
- [ ] The layout sums exactly to the grid (§7). No ink in the 8 px margin, and no ink within 8 px of occupied corner slots.
- [ ] Text: ≥13 px (except the note's mono.s), ASCII only, sentence case (except labels), measured-width truncation, `--` in tertiary for unknown values.
- [ ] Contrast meets §4.4 for every text and background pair.
- [ ] The accent is used only for selection, the usage series or current-item markers. Status colours are used only for status. There is always a second, non-colour cue.
- [ ] Every target is ≥44 × 44 (≥40 normal to an edge) with ≥8 px visual gap, has a visible affordance, shows a pressed state and commits on up.
- [ ] Destructive actions use the arm pattern.
- [ ] Wayfinding: the screen answers where am I, where can I go, what's here and how do I get out.
- [ ] Loading, empty and error states are designed (§13.4).
- [ ] Motion: state changes are cuts. Spatial motion uses the spring tokens on the correct axis, enters and leaves by the same path, and starts from the on-screen value. Input is never locked. Nothing loops unless it's content.
- [ ] Gestures: anything draggable tracks 1:1 with the grab offset, hands its velocity to the spring on release, rubber-bands at its bounds, and has a visible tap equivalent.
- [ ] No brightness jumps: backlight changes fade, and nothing flashes or inverts the screen.
- [ ] Labels name what things do; no hint explains a control.
- [ ] Motion reviewed in slow motion (§17), and the screen glance-tested on the board at desk distance and at Night Mode brightness.
- [ ] System corner glyphs are drawn last; text over media has plates.
- [ ] The simulator is updated in the same change, and the simulator and board screenshots match.

---

## 17. Process

- **The simulator is the interactive prototype.** Design and tune interaction in
  `simulator-s3.html` first. It runs the same layout and fonts, and after migration it
  runs the same spring integrator and gesture recogniser (mirrored like everything else).
  For anything that moves, a static mock-up isn't something you can review.
- **Review motion in slow motion.** Add a debug time scale: serial key `m` on the board
  (it's free in `serialCommand`) and `?slowmo=10` in the simulator. Either one multiplies
  every motion token's time by 10 (for springs, response × 10).
  - The simulator also steps one frame per `.` keypress.
  - Check the largest per-frame step, that nothing jumps on an interrupt, that handoffs
    start from the on-screen value, and that enter and exit paths match.
- **Design interaction and visuals together.** A component isn't specified until its
  pressed, dragged, interrupted and settling states are defined alongside its resting pixels (§11).
- **Test with real fingers, in the real setting.** On the board, on the desk, at about 60 cm:
  - The **one-second glance test:** can you read the 5h % and see the pace flag in one look?
  - The same test in Night Mode at 25% brightness.
  - Brush the edges while picking the device up. Nothing should commit.
  - Flick the Settings list, and interrupt a slide.
- **Iterate with a record.** A token changes here first, with its reason, and then in the
  firmware and simulator together. This file is versioned by the same git history as the code.

---

## 18. Appendix

### 18.1 RGB565 conversions

```python
def to888(c):   # what the simulator draws (matches its // 0x.... comments)
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return round(r * 255 / 31), round(g * 255 / 63), round(b * 255 / 31)

def to565(r, g, b):
    return (round(r * 31 / 255) << 11) | (round(g * 63 / 255) << 5) | round(b * 31 / 255)

gray565 = lambda v: (v << 11) | ((2 * v) << 5) | v   # exact neutral, v = 0..31
```

`to565(lerp(to888(fg), to888(bg), a))` gives a precomputed blend (§4.5).

### 18.2 Contrast

This is the WCAG 2 relative luminance `L = 0.2126 R + 0.7152 G + 0.0722 B`, applied to linearised
sRGB channels (`c ≤ 0.04045 ? c/12.92 : ((c + 0.055)/1.055)^2.4`). The ratio is
`(L_hi + 0.05) / (L_lo + 0.05)`. Every figure in §4.4 comes from the RGB888 values in
§4.1. Dimming the backlight scales all luminances together, so the **ratios** hold at
Night Mode's 25%; only absolute brightness falls (§1).

### 18.3 Physical conversions

`px = mm × 6.54`, and `mm = px / 6.54`:

| px | 8 | 13 | 17 | 26 | 40 | 44 | 56 |
|---|---|---|---|---|---|---|---|
| mm | 1.2 | 2.0 | 2.6 | 4.0 | 6.1 | 6.7 | 8.6 |

### 18.4 Verify on the board before relying on it

| Item | Why it's open | How to settle it |
|---|---|---|
| `touch.rearm` = 120 ms | The AXS15231B's bounce and spurious-up behaviour hasn't been characterised | Log touch down/up timing (`[touch]` serial lines, `main.cpp:500`) while tapping fast; set rearm just above the longest bounce you see |
| `touch.edge.min` = 40 | Centroid accuracy near the bezel hasn't been measured | Tap the corners and the bottom edge; compare the logged `x`/`y` with the intended targets |
| Smooth primitives | **Done.** Ported to the simulator; the board-vs-simulator pixel compare differs only where live data changed between grabs | -- |
| Night-dim legibility | gray.1 against gray.0 at 25% backlight | Eyeball on the panel at night brightness; if cards vanish, the gutters still group them (§1), so no change is needed unless text suffers |
| Vertical sheet cost | **Measured** (the `[motion]` serial line): ~30 ms per sheet composite vs ~23 ms per page slide; the difference is the scrim's swap, shift and mask | -- |
| 60 Hz motion | **No.** Composites take 23–30 ms, which misses a single TE period, so springs stay at ~30 fps | Revisit only if the composite loops get faster |
| Spring feel | The response values were tuned in simulation, not by hand | Review in slow motion (§17), then at full speed on the board. Adjust only the response, keeping ζ = 1.0 |
| Tracking and `opsz` regeneration | New advances change every measured width | Re-run `make_vlw.py`, then compare simulator and board screenshots on every page, checking the truncation paths (weather condition, note pane) |
