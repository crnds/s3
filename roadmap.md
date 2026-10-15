# S3 Dashboard — Roadmap

The plan for taking the JC3248W535EN board from "a Claude usage dashboard"
to **the desk companion of a UX design manager**: UX/UI, new features,
performance and engineering optimisation, in one prioritised list.

| | |
|---|---|
| **Owner** | dusit.n (primary user) |
| **Status** | v0.4, 2026-10-14: O8 (CI) and O9 (OTA) shipped; §3.4 corrected (no `?client=` projection exists yet). v0.3: roadmap review — O7 and O9 pulled forward, O14 (settings backup) added, clock trust folded into P6, CYD smoke test added to §15 |
| **Horizons** | **Now** = Oct–Nov 2026 · **Next** = Dec 2026–Feb 2027 · **Later** = Mar 2027 onward |
| **Related** | `design.md` (design system, governs *what it looks like*), `CLAUDE.md` (mechanics and invariants, governs *how it's built*), `esp32-s3-spec.md` (hardware) |
| **Precedence** | This file decides *what to do next and why*. When an item is picked up, its design goes into `design.md` first and its mechanics obey `CLAUDE.md`. If this file disagrees with either one, they win and this file gets fixed. |

---

## Contents

1. [North star](#1-north-star)
2. [Where we are](#2-where-we-are-baseline-2026-10-14)
3. [Constraints that shape every item](#3-constraints-that-shape-every-item)
4. [How items are prioritised](#4-how-items-are-prioritised)
5. [Roadmap at a glance](#5-roadmap-at-a-glance)
6. [UX/UI](#6-uxui)
7. [New features](#7-new-features)
8. [Performance](#8-performance)
9. [Optimisation: engineering, tooling, process](#9-optimisation-engineering-tooling-process)
10. [Milestones](#10-milestones)
11. [Success metrics](#11-success-metrics)
12. [Risks](#12-risks)
13. [Not doing, and why](#13-not-doing-and-why)
14. [Open questions for the owner](#14-open-questions-for-the-owner)
15. [How we work through this roadmap](#15-how-we-work-through-this-roadmap)

---

## 1. North star

**Who it's for.** A UX design manager at their desk. Their day is crits, 1:1s,
reviews, a few protected blocks of deep work, and Claude Code sessions running
in the background. The board sits about 60 cm away and gets glanced at dozens of
times a day, usually mid-conversation or mid-thought.

**The job.** In **one second**, without picking anything up, the board answers
three questions:

| # | Question | Answered by (today → target) |
|---|---|---|
| 1 | **Can I keep going?** | 5h and weekly limits with the pace flag (shipped) → plus a plain-language forecast ("weekly limit Thu ~15:00 at this pace") |
| 2 | **What's next?** | Clock and calendar (shipped) → plus the next meeting, and the time left in a focus block |
| 3 | **Does anything need me?** | Status dot, offline mode (shipped) → plus "Claude needs you / Claude finished", per project |

Everything else (BTC, weather, AQI, cats, movies, the note) is **ambient**. It
can be pleasant, but it must never compete with those three for the eye.
That ranking is the tie-breaker for every layout and priority call in this file.

**The feel** stays `design.md` §2's *calm confidence*: nothing moves unless
something happened, a static screen stays still, and the device is quiet by
default and gets louder only when the user has a reason to look.

---

## 2. Where we are (baseline, 2026-10-14)

**Shipped (40 commits, Sep 24 – Oct 9 2026).** design.md v1.2 migrated in full:
tokens, 8 px grid, spring motion with velocity handoff, sheets with scrim,
swipe paging, momentum lists, Reduce Motion, Increase Contrast, Dark/Light
themes. Six pages (Status, Usage, Cats, Movies, Status + cats, Note), plus
Weather, Device Stats and Settings sheets. Speaker output with a Sound section,
screen sleep, Battery Save, Thai holidays on the calendar, AQI bar, 10-hour
weather strip, and 13 embedded cat GIFs.

**Measured numbers** (from `CLAUDE.md`, `design.md` §15.5 and commit messages):

| Metric | Today |
|---|---|
| Firmware size | ~2.97 MB (45% of the 6.25 MB app slot): embedded cats ~1.3 MB, fonts ~200 KB |
| Embedded cats | 13 GIFs, 1.3 MB of the 4 MB embed budget |
| Status page compose | ~40–54 ms, at 1 Hz |
| Idle CPU, Status page | 16% (down from 52% after the shared 8 Hz ambient tick) |
| Presents at rest, page 0 | ~7.8 / s (each one is a full ~16 ms frame) |
| Transition composite | ~23 ms page slide, ~30 ms sheet; ~12 frames at ~30 fps |
| Longest gap, slide onto a media page | ~65 ms (was a 230–330 ms stall before deferred media load) |
| Movie playback | 9.6 fps (SD 1-bit, `NO_SIMD` JPEG decode) |
| Poll / offline | 20 s poll; offline mode after a 3 min / 3-poll grace |

**Gaps found while writing this roadmap.** Each one is an item below.

| Gap | Evidence | Item |
|---|---|---|
| "Claude ding" and "Alerts" toggles do nothing. `SND_DONE`, `SND_ATTN` and `SND_ALERT` exist but only play from the test row and the serial `k` key | `src/audio.h:9-11`, `src/settings.cpp:331`, `src/main.cpp:301` | ✅ U1; F1 |
| The status-strip targets (Device Stats, Settings) are 56 × 20 since the strip shrank to 20 px. That's below `touch.edge.min` (40) | `src/state.h:144` | ✅ U2 |
| Night Mode's schedule copy ("23:00–07:00, dims to 25%") isn't shown anywhere since toggle rows lost their detail screens | `design.md` §15.5 | U5 |
| Toasts exist only inside Settings; dashboard pages have no transient feedback surface | `src/settings.cpp:585`, `src/main.cpp:511` | U7 |
| `/archive.csv` is written every poll but never read back | `src/net.cpp:343` | F6 |
| Four board-only checks are still open: touch rearm, edge accuracy, night-dim legibility, spring feel | `design.md` §18.4 | U3 |
| Optical sizing (Inter 4 `opsz`) isn't done | `design.md` §15.5 | U11 |
| Values can be up to 3 minutes stale before offline mode, with nothing on screen saying so | offline grace, commit `33d58d8` | ✅ U9 |
| The backlight breath (a channel that costs no frames) is built but unused | `src/motion.h:43` | U7 |
| The README still lists the old Projects and Limits pages and cats on the SD card; the upload port is `1101` in `platformio.ini` but the board enumerates as `1301`; `CLAUDE.md`'s layout section still gives the old 30 px strip | `README.md`, `platformio.ini`, `CLAUDE.md` | ✅ O2, O3 |
| 46 MB of video is tracked in git (`movies/morpwall.mp4` is 42 MB) | `git ls-files movies` | O1 |
| No automated tests and no CI. Parity between firmware and simulator is checked by hand, and the note tokenizer exists in six copies | `CLAUDE.md` | O4–O8 |
| Text is ASCII only, but the user is in Bangkok: Thai calendar titles or notes would show as `?` | `CLAUDE.md` (Fonts) | F11 |

---

## 3. Constraints that shape every item

These are non-negotiable. An item that can't meet them gets redesigned, not
waived.

1. **Parity.** `pages.cpp` + `settings.cpp` and `simulator-s3.html` render the
   same pixels. Every visible change lands in both, in the same commit, with a
   board grab and a sim shot compared.
2. **Tokens only.** No raw colour, font or spacing in a screen. A new value goes
   into `design.md` first, then `tokens.h` and the sim's TOKENS block.
3. **Motion costs the same at any size.** Every present streams the full
   307,200-byte frame (~16 ms). So **each feature declares its present budget**:
   how many presents per second it adds at rest, and during its motion.
4. **No TLS on the board.** Every new data source (calendar, Claude hooks, team
   tools) is a **Mac server change in `~/cyd`** plus a change to the shared
   `/api/usage` contract. New fields are **additive and optional**, so the CYD
   keeps working untouched. ArduinoJson ignores keys it isn't asked for, but it
   still spends heap on them while parsing. So large S3-only blocks (agenda,
   history) go behind a `?client=` projection, and the CYD's payload doesn't
   grow. **That projection doesn't exist yet** (checked 2026-10-14: the server
   matches the path `/api/usage` exactly, so even a query string 404s); the
   first large S3-only block adds it. Small blocks like O9's `firmware` go to
   both boards.
5. **Hardware.** 480 × 320 RGB565, no alpha, single-point touch, ~30 fps
   composites, 8 MB PSRAM, a 6.25 MB app slot, a 4 MB cat budget, and no light
   sensor (GPIO17/18 are free for an I2C add-on).
6. **Every new screen passes `design.md` §16** (the new-screen checklist), in
   both themes, with Increase Contrast and Reduce Motion on and off.

---

## 4. How items are prioritised

Each item gets three ratings:

- **Value**, judged against the three glance questions (§1):
  - **H** answers one of them, or removes a broken or misleading affordance.
  - **M** makes a daily task smoother.
  - **L** is ambient polish.
- **Effort**, for one person working part-time:
  - **S** ≤ 1 day
  - **M** 2–5 days
  - **L** 1–3 weeks
- **Surfaces** (what changes):
  - `FW`: firmware
  - `Sim`: the simulator twin
  - `Srv`: the Mac server in `~/cyd`
  - `CYD`: the sibling board
  - `Docs`
  - `Tools`

Order of work: **broken or misleading things first, then measure, then
glance-question value, then speed, then delight.** An item doesn't move to
Next until everything it depends on is done.

---

## 5. Roadmap at a glance

| | **UX/UI** | **New features** | **Performance** | **Optimisation** |
|---|---|---|---|---|
| **Now** | U1 Truthful sound settings · U2 Strip targets to 40 px · U3 Close the §18.4 board checks · U4 Glance-test protocol · U5 Night Mode context | F5a Pace forecast | P1 Instrument and baseline · P8 Memory budget | O1 Repo hygiene · O2 Docs drift · O3 Port auto-detect · O4 Native unit tests · O7 Parity fixture mode |
| **Next** | U6 Settings IA · U7 Attention pattern · U8 Theme and contrast audit · U9 Staleness and state coverage · U10 Status page hierarchy test | **F1 Claude needs you** · F2 Next meeting and agenda · F3 Focus timer · F4 Mac-aware sleep · F10 Pages and night window | P2 Static-layer cache · P5 Idle cost · P6 Boot and reconnect · P7 Lean JSON parse ✅ | O5 Tokenizer test corpus · O6 Token pipeline (+ Figma) · O8 CI ✅ · O9 OTA updates ✅ · O14 Settings backup |
| **Later** | U11 Optical sizing · U12 Break mode | F4b Presence mode · F5b Usage history · F6 Team signals · F7 Desk sensors · F8 Interactive note checklist · F11 Thai text | P3 60 fps composites · P4 Movie ≥ 15 fps | O10 Code split · O11 Media partition · O12 Toolchain review · O13 Crash telemetry |

**The single most valuable item is F1 (Claude needs you).** It answers glance
question 3, which nothing answers today. The sounds, the settings rows and the
backlight breath it needs are already built and just waiting for a trigger.

---

## 6. UX/UI

### Now

#### ✅ U1 · Make every sound setting do what it says — H · S · FW Sim
- **Why.** "Claude ding" and "Alerts" are toggles that change nothing. A
  control that does nothing breaks `design.md` §2 ("labels name what things do")
  and erodes trust in every other setting.
- **What.**
  - Wire **Alerts** to events the board already knows about:
    - a pace flag turning on for 5h or the week
    - usage crossing 90%
    - the server going offline, and coming back
  - Fire each event once per crossing, with hysteresis. Use the existing
    priority and mute rules.
  - Until F1 ships, hide the **Claude ding** row (or show it as
    "Coming soon").
- **Done when.**
  - Every row in Settings > Sound has an audible effect that you can trigger and
    observe.
  - The trigger events are listed in `design.md` §12.10.
- **Shipped (2026-10-14).** `pollAlerts` in `main.cpp` runs on the network task
  after each poll.
  - **Alerts:**
    - a 5h or weekly limit projected to run out before its reset, once 10% of
      the window has passed (rather than whenever the pace flag turns on)
    - a limit reaching 100%
    - the board going offline

    Each fires once per window or outage.
  - **Claude ding:** plays when the server's `active_now` falls from true to
    false, about 3–4 min after Claude Code's last output, so the row stays.
  - The "needs you" sound still waits on F1's Claude Code hooks.

#### ✅ U2 · Bring the status-strip targets back to the touch minimum — H · S · FW Sim
- **Why.** The Device Stats and Settings targets are 56 × 20 px (`state.h:144`).
  That's half of `touch.edge.min`, on the edge you brush when you pick the board up.
- **What.** Extend the hit boxes up through the 8 px gutter to y 280. That gives
  56 × 40, which is what `design.md` §15.3 specified. The ink doesn't move. Check
  that nothing at y 272–279 in the content above becomes ambiguous.
- **Done when.** The §18.4 edge test (tap the corners and the bottom edge, log x/y)
  hits every strip target 10/10, and the brush test commits nothing.
- **Shipped (2026-10-14).** Both targets are 56 × 40 (y 280..319). In the hit
  order they come before cards but after the shuffle button: the Weather card
  gives up its bottom-right 12 px corner, and under a Fixed shuffle button the
  gear stays 28 px. `design.md` §9.2, §9.6 and §11.12 are updated. The
  on-board edge test is still open, under U3.

#### U3 · Close the board-only checks in design.md §18.4 — M · S · FW Docs
- **What.** Settle each one on the panel:
  - **touch rearm:** log `[touch]` down/up while tapping fast
  - **edge accuracy:** tap the corners
  - **night-dim legibility:** gray.1 cards at 25% backlight
  - **spring feel:** slow motion, then full speed

  Write each result and decision back into §18.4.
- **Done when.** §18.4 has no open rows.

#### U4 · A repeatable glance-test protocol — H · S · Docs
- **Why.** The design system already defines the tests (§17), but nothing
  records their results. For a design manager this is the usability ritual that
  keeps the device honest.
- **What.**
  - A 10-minute script run at the end of each milestone:
    - the three glance questions at 60 cm, in daylight and in Night Mode
    - the brush test
    - the flick test
    - interrupting a slide
    - both themes
  - A results table per page (pass/fail + note) appended to this file or to
    `design.md`.
- **Done when.** The first run is recorded, and failures are filed as items here.

#### U5 · Put Night Mode's schedule back on screen — M · S · FW Sim
- **What.** The toggle row's value reads "On · 23–07" (sentence case, caption)
  instead of a bare On/Off. That restores the context §15.5 says was lost. It
  becomes editable with F10.

### Next

#### U6 · Settings information architecture — M · M · FW Sim Docs
- **Why.** There are 22 rows, about 3.3 screens of scrolling. Sound is already a section.
- **What.** Group the main list into one-level sections using the existing
  section pattern:
  - **Display** (Brightness, Night mode, Rotation, Theme, Pixel shift)
  - **Content** (Boot page, Pages, Cat shuffle, Pace bars, Show AQI, Hourly signal, Poll progress)
  - **Sound**
  - **Accessibility** (Reduce motion, Increase contrast)
  - **Connection** (Poll interval, Battery save)

  Destructive actions stay last. Each section row summarises its state, as
  Sound already does.
- **Done when.**
  - The main list fits in ≤ 1.5 screens.
  - Every setting is ≤ 2 taps from the gear.
  - The `pages_v2`-style NVS migration isn't needed, because keys don't change.

#### U7 · Design the attention pattern (design-first, unblocks F1 and F2) — H · M · Docs Sim
- **Why.** Today's feedback model (§13.2) has status, completion, warning and
  error. It has no place for "something outside the board needs you".
- **What.** Specify these in `design.md`, then prototype them in the sim:
  - **Where.** A status-strip chip (glyph + project name + `status.warning`;
    never colour alone).
  - **The glance-from-across-the-room cue.** The unused **backlight breath**,
    three dips, no presents.
  - **The sound.** `SND_ATTN` or `SND_DONE`, respecting the mute rules.
  - **What clears it.** Resuming the session, a tap on the chip, or a timeout.
  - **Behaviour in other states.** On media pages (opaque plate), offline,
    with a sheet open, and asleep. Asleep needs a decision: breathe the
    backlight, or stay dark?
  - **A dashboard-level toast/banner.** It reuses §11.17, so completions
    outside Settings have a home.
- **Done when.** design.md has the component spec with its pressed, settling and
  interrupted states, and the sim demo passes U4 at 60 cm.

#### U8 · Light theme and Increase Contrast audit — M · M · FW Sim Docs
- **What.**
  - **Matrix:** every page × {Dark, Light} × {Contrast off, on}.
  - **Check:** contrast against `design.md` §4.4.
  - **Known risk areas:**
    - the 3D weather glyphs
    - the AQI scale
    - the Thai-holiday rose
    - BTC change colours
    - the media overlays and plates
- **Done when.** The §4.4 table covers both themes, and every pair meets its floor.

#### 🚧 U9 · Staleness and full state coverage — H · M · FW Sim Srv
- **Why.** For up to 3 minutes before offline mode, the board shows old numbers
  with full confidence. A desk monitor that's confidently wrong is worse than
  one that's blank.
- **What.**
  - **A data-age rule.** When the last good poll is older than two intervals,
    live values drop to `text.secondary` and the strip shows a caption like
    "Updated 3m ago". Cards whose own source is stale say so per card, using
    the server's per-source age fields: BTC, weather and AQI can each fail on
    their own.
  - **A §13.4 pass on every card:** loading, empty and error.
- **Done when.** Every card has designed loading, empty, error and stale states
  in both themes, and the sim can force each one.
- **Shipped (2026-10-14): the data-age rule.**
  - `dataStale()` checks for no good live poll in max(2 intervals, 1 min), or
    data from the SD cache.
  - The strip's CPU readout becomes "Updated 2m ago" or "Saved data".
  - The limit numerals go to `text.secondary`.
  - It's twinned in the sim, whose offline threshold now matches the firmware's
    max(3 min, 3 polls).
  - **Still open:** per-source age (BTC, weather and AQI each failing on their
    own needs server fields) and the §13.4 pass on every card.

#### U10 · Test the Status page hierarchy for this persona — H · M · Sim Docs
- **Why.** The Status page grew card by card. Now ask which card earns which
  slot for a design manager's day.
- **What.** Prototype three variants in the sim, then run U4 on each:
  - **A.** Today's layout.
  - **B.** An agenda card (F2) takes the BTC card's slot; BTC moves into a sheet.
  - **C.** A "Show BTC" toggle that's off by default.

  Pick the winner on glance-question speed, not taste.
- **Done when.** The decision and its glance-test numbers are recorded in
  design.md §7.4, and the firmware follows.

### Later

#### U11 · Optical sizing with Inter 4 `opsz` — L · M · Tools FW Sim
- **What.** Install `InterVariable.ttf`, set `opsz = size` in `make_vlw.py`, and
  regenerate the fonts.
- **Check.** Re-check every truncation path: the weather condition, the note
  pane and project names. Run the full sim-vs-board compare.

#### U12 · Break mode — L · S · FW Sim
- **What.** When a focus block (F3) ends, offer the cats page as a short break
  that auto-returns to Status after N minutes. That gives the ambient media a
  job instead of competing with the data.

---

## 7. New features

### Now

#### F5a · Pace forecast in plain language — H · S · FW Sim
- **Why.** The pace flag says *that* you're ahead of pace. It doesn't say
  *when it bites*.
- **What.**
  - Use the elapsed/remaining maths the pace logic already has to project when
    you'd hit the limit at the current rate.
  - On the Week card, show "At this pace: Thu ~15:00", only when the projection
    lands before the reset.
  - Use the same rule on the Usage page.
- **Budget.** It costs no presents; it's drawn at 1 Hz with the rest of the card.
- **Depends on.** O4 — the forecast maths land with native test coverage from
  the start, not as a retrofit.

### Next

#### F1 · Claude needs you / Claude finished — H · M · Srv FW Sim
- **Why.** This is glance question 3. With several Claude Code sessions running
  while you're in a crit, the board should tell you which one is waiting on you.
- **What.**
  1. **Mac side (`~/cyd`).**
     - A Claude Code hook script runs on `Notification` (needs input or
       permission) and on `Stop` (finished).
     - It POSTs `{kind, project, session}` to `usage_server` on localhost.
       `UserPromptSubmit` clears that session.
     - The server adds an optional `attention: [{id, kind, project, age_s}]` to
       `/api/usage`.
  2. **Board, delivered by polling (ship first).**
     - It acts on every `id` above the last one it saw.
     - It shows the U7 chip, breathes the backlight and plays `SND_ATTN` or
       `SND_DONE`.
     - Latency is at most one poll interval (20 s).
  3. **Board, delivered by push (only if 20 s feels slow).**
     - A tiny LAN-only `POST /notify` on the board, with a shared secret in
       `config.h`.
     - The server calls it as soon as a hook fires, for ≤ 2 s latency.
- **Depends on.** U7 (pattern), U1 (sound plumbing).
- **Fallback.** F1 is the top-value item, so if U7 slips past the start of M2,
  ship a deliberately minimal attention v0 first — the strip dot plus
  `SND_ATTN`, no chip spec — and refine it into the full U7 pattern later.
- **Done when.**
  - A session waiting for input shows its project name on every page within
    one poll.
  - It clears on resume.
  - The "Claude ding" row works.
  - The CYD keeps working without the new field.

#### F2 · Next meeting and today's agenda — H · M · Srv FW Sim
- **What.**
  1. **Mac side.** Read the macOS Calendar store, which covers Google, Exchange
     and iCloud accounts once they're added to Calendar.app. Use a small Swift
     helper (EventKit) or `icalBuddy`. The system Python has no EventKit
     bindings.
     - Add `agenda: [{start, end, title, location?, busy}]` for today and
       tomorrow morning.
     - Apply the same `sanitize` to titles as to the note.
  2. **Status page.** The calendar card shows "Next · Design crit · in 25m"
     when there's an event in the next 4 h. The full agenda opens in a
     read-only **Agenda sheet** from the calendar card, mirroring
     Weather → Weather sheet.
  3. **Alert.** An optional chime 5 minutes before a meeting, in the alert
     category, honouring the mute rules.
- **Depends on.** U7, U10 (where it lives). Thai titles need F11.
- **Done when.** The next event and its countdown are readable in one glance at
  60 cm, the sheet lists the day, and an all-day or empty day has a designed state.

#### F3 · Focus timer — M · M · FW Sim
- **What.**
  - Tap the clock card to open a **Focus sheet**: an option grid of 25 / 50 /
    90 min, plus "Until next meeting" when F2 is live.
  - While a focus block runs, the analog clock shows its remaining wedge, and
    the strip shows "Focus · 32m left".
  - The end plays `SND_DONE` and breathes the backlight.
  - Optionally, during focus, mute "Claude finished" but keep "Claude needs you".
- **Design decision needed.** The timer wedge already shows the 5h reset window
  (`drawTimerWedge`). Decide whether focus replaces it while a block runs or
  takes the outer ring.
- **Budget.** It runs inside the existing 1 Hz render, so it adds no presents.

#### F4 · Mac-aware sleep — M · S · Srv FW
- **What.**
  - The server adds `mac: {locked, idle_s}`: HID idle time from `ioreg`, and the
    lock state.
  - A **Sleep with Mac** toggle puts the board to sleep when the Mac locks or
    stays idle more than 10 minutes, and wakes it when the Mac unlocks.
  - The existing sleep path (DISPOFF, 80 MHz, modem sleep) does the rest.
- **Why.** The board goes dark when you leave the desk. That saves the panel
  and adds no noise to an empty desk.

#### F10 · Choose your pages, set your night window — M · M · FW Sim
- **What.**
  - **A Pages section** with one toggle per carousel page. Movies is hidden
    automatically with no SD card. At least one page always stays on.
  - **Night mode window:** start, end and dim level, as option grids.
  - **Boot page** respects the hidden pages.
- **Depends on.** U6.

### Later

#### F4b · Presence mode — M · M · FW Sim
- **What.**
  - When a calendar event is busy, or a focus block is running, an optional
    full-screen state shows big type: "In a crit · until 14:30" or
    "Focusing · 25m".
  - It's readable from 2–3 m, for teammates walking up to your desk.
  - It returns to normal automatically.
- **Cost.** One more display-size font, roughly 20–40 KB of flash; estimate it
  in a spike.

#### F5b · Usage history and insight — M · L · Srv FW Sim
- **What.**
  - The server aggregates 30 days of usage (it owns the data; the board's
    `/archive.csv` is the fallback).
  - It sends a compact `history` block.
  - A History sheet opens from the Usage page:
    - 5h and weekly peaks per day
    - busiest hours
    - weeks that hit the limit
- **Why.** A manager budgeting AI usage needs the trend, not just the current gauge.

#### F6 · Team and design-ops signals (validate before building) — M · L · Srv FW Sim
- **What.**
  - Show counts only, and only when they're non-zero:
    - Figma comments that mention you
    - reviews waiting on you
    - tickets in "Design review"
  - The server holds the tokens. The board shows one row, at most.
- **Risk.** Notification noise. That runs against §1's calm principle.
- **Gate.** Run a one-week diary first: would these counts have changed what
  you did?

#### F7 · Desk environment sensors (hardware add-on) — L · M · FW Sim
- **What.**
  - Use the free GPIO17/18 I2C pair (spec, "Connectors"):
    - a BH1750 for **auto-brightness**
    - an SCD40 for **CO₂, temperature and humidity**, with a gentle
      "Open a window" nudge above ~1200 ppm
  - It's optional; with no sensor, nothing changes.

#### F8 · Tappable checklist on the Note page — L · M · Srv FW Sim
- **What.**
  - `- [ ]` lines render as checkboxes, and a tap toggles them.
  - The board POSTs the change to the server. This is the first write path
    from board to Mac, so design auth and conflict handling once and reuse
    them.
  - It touches the six-copy tokenizer (O5 first).

#### F11 · Thai text for agenda titles and notes — M · L · Tools FW Sim Srv
- **What.**
  - Add a Thai glyph subset to `make_vlw.py` for the body and caption sizes.
  - Handle above- and below-base marks (vowels and tone marks), which the VLW
    path doesn't position today.
  - Measure the flash cost.
- **Spike first.** It's worth doing only if F2 shows that Thai titles are common.

---

## 8. Performance

Rule: **measure → change one thing → re-measure.** Every item records before
and after in §11.

### Now

#### P1 · Instrument and baseline — H · S · FW Tools
- **What.** Extend the `[timing]` and `[motion]` serial lines into a `[perf]`
  summary:
  - boot to first live data
  - compose time per page
  - presents per second per page at rest
  - the slowest composite
  - touch-down to pressed-state present latency
  - low-water marks for internal SRAM and PSRAM

  Also measure power draw with a USB power meter: active, at rest, asleep.
- **Done when.** §11's "today" column is filled with numbers measured on the board.

#### P8 · Memory budget table — M · S · Docs FW
- **What.**
  - List every PSRAM buffer:
    - the frame, `prevFrame` and `behindFrame` (300 KB each)
    - three posters (900 KB)
    - the neighbour page
    - `gifCanvas`
    - the movie buffers
  - List the internal-SRAM DMA strips.
  - Add a headroom line, so that F2, F5b and F11 can be priced before they're
    built.

### Next

#### P2 · Static-layer cache for compose — M · M · FW
- **Why.** The Status page spends ~40–54 ms composing every second. Most of it
  is the same pixels: card surfaces, labels, the month grid. The full clear
  alone is ~8 ms, and anti-aliased text is ~1 ms a string.
- **What.**
  - Render the static layer into a PSRAM base frame once per minute or day, or
    on a theme change.
  - Each second, `esp_async_memcpy` the base, then draw only the dynamic text
    and hands.
  - Invalidate the base on theme, layout or data-shape changes.
- **Done when.**
  - The Status page composes in ≤ 20 ms.
  - The output is pixel-identical: verify with the O7 parity diff before and
    after.
- **Twin impact.** None; this is a firmware-only optimisation.

#### P5 · Lower the idle cost — M · S · FW
- **What.** Profile the ~7.8 presents per second at rest:
  - the hairline
  - the status-dot pulse
  - the shine

  Present on the ambient tick only when its pixels actually change, and fold
  the hairline into the 1 Hz render where the step is under 1 px.
- **Target.**
  - Status page at rest: ≤ 10% CPU and ≤ 4 presents per second, with Poll
    progress on.
  - Lower measured power (P1).

#### P6 · Boot and reconnect — M · S · FW
- **What.**
  - Draw the SD-cached usage the moment the panel is up. It's marked stale
    (U9).
  - **Clock trust.** The clock seeds only from live polls today, so if the
    Mac is unreachable at boot the board runs on a wrong clock until it
    returns. Add an NTP fallback (or the server's time inside the poll) plus
    a drift check — F2, F3, F4b and F5a all assume the clock is right.
  - Cache the last resolved IP for the `.local` host, and fall back to it when
    mDNS fails after the Mac sleeps.
  - Measure WiFi rejoin time.
- **Target.** A useful screen in ≤ 3 s after power-on, and no offline flaps
  across a Mac sleep/wake.

#### ✅ P7 · Lean JSON parse — L · S · FW
- **What.**
  - Use an ArduinoJson filter document so only the fields the board uses are
    parsed.
  - Matters more as F1, F2 and F5b grow the payload.
- **Measure.** `fetchUsage()` µs and heap use (already logged).
- **Already in place.** `net.cpp`'s `usageFilter()` was there before this
  roadmap; the first draft missed it. What's left is keeping the filter in step
  as new fields arrive.

### Later

#### P3 · 60 fps transitions — M · L · FW
- **Why.** Composites take 23–30 ms, which misses one TE period (16.5 ms), so
  springs step at ~30 fps.
- **What.** Profile `fillStrip`'s rotate copy, then try in order:
  1. 32-bit, two-pixel transposes.
  2. Branch-free scrim and fade inner loops.
  3. Overlapping the rotate of strip n+1 with the DMA of strip n.
  4. S3 PIE/SIMD for the transpose.
  5. A QSPI clock above 40 MHz. It's last because it's untested on this board
     (spec, "Display frame rate").
- **Exit rule.** If composites can't reach ≤ 16 ms, record why in design.md
  §18.4 and stop. 30 fps springs are acceptable.

#### P4 · Movies at ≥ 15 fps — M · M · FW
- **What.** In order:
  1. Re-validate JPEGDEC's S3 SIMD path for RGB565 against the C path,
     pixel-exact. The known bug (#80) is in RGB8888. If they match, drop
     `NO_SIMD`.
  2. Pipeline the SD read and the decode across cores.
  3. Try PSRAM at 120 MHz, per the seller's notes; check it's stable.
- **Note.** SD 1-bit wiring can't change, so short clips could live in flash
  (O11).

---

## 9. Optimisation: engineering, tooling, process

### Now

#### O1 · Repo hygiene — M · S · Tools
- **What.**
  - Stop tracking `movies/morpwall.mp4` (42 MB source). Keep it in the
    gitignored `docs/`, or move media to Git LFS.
  - Add `movies/*.mp4` to `.gitignore`.
  - Decide `movies/morpwall-2x.mjpeg` the same way — it's tracked too. Keep
    the SD card's working copy, but out of git (ignore `movies/*.mjpeg` or
    move it to `docs/`).
- **Caution.** The file stays in history. Rewriting history to shrink clones is
  a separate, destructive decision for the owner.

#### ✅ O2 · Fix documentation drift — M · S · Docs
- **What.**
  - **README:** the current six pages; cats are embedded, not on the SD card;
    the Movies page.
  - **`CLAUDE.md` layout section:** the current 20 px strip geometry.
  - **design.md §11.12:** check that it matches the strip as built.
- **Shipped (2026-10-14).**
  - **README:** rewritten.
  - **`CLAUDE.md`:**
    - the layout grid
    - the build size (2.97 MB, not 1.55 MB)
    - the offline threshold (3 min / 3 polls, not 60 s)
    - the sleep pill
    - the 328 px cat pane
    - the pio/Python setup
    - new notes on stale data and the sound alerts
  - **`design.md` §11.12:** strip box, glyph centre y 309, caption top y 301.

#### ✅ O3 · Upload port auto-detect — M · S · Tools
- **What.**
  - Drop the hardcoded `/dev/cu.usbmodem1101` from `platformio.ini` (the board
    is on `1301` now).
  - Let PlatformIO auto-detect, or add a small `pre:` script that globs
    `/dev/cu.usbmodem*`.
  - Change `grab_screen.py`'s default the same way.
- **Shipped (2026-10-14).**
  - `platformio.ini` sets no port; PlatformIO matches the board's 303A:1001.
  - `grab_screen.py` uses the only `/dev/cu.usbmodem*`, or asks for `--port`
    when there are several.

#### O4 · Native unit tests — H · M · FW Tools
- **What.**
  - Add an `[env:native]` PlatformIO environment covering:
    - `format.cpp` (countdowns, numbers)
    - the pace deadband and F5a's forecast
    - the offline and staleness state machine
    - the spring integrator in `motion.cpp`
    - the note tokenizer
  - Pure logic first. Refactor only what's needed to separate it from hardware.
- **Done when.** `pio test -e native` runs in seconds and covers every function
  above.

#### O7 · Parity fixture mode — H · M · FW Sim Tools
- **What.**
  - **Board:** a serial command that loads a canned `/api/usage` JSON and
    a fixed clock, and freezes polling.
  - **Sim:** `?fixture=<name>`.
  - **`tools/parity.py`:** grabs every page on both, diffs them pixel for
    pixel, and writes a diff PNG and a report.
- **Why.** Every change gets checked the way `CLAUDE.md` requires, in one
  command instead of by hand. P2 needs it to prove the cache is pixel-identical.

### Next

#### O5 · One test corpus for the six-copy note tokenizer — H · S · Tools
- **What.**
  - Add `tests/note_tokens.json`: input lines mapped to the expected colour
    role per word.
  - Run it against:
    - the C++ copy (native env)
    - `simulator-s3.html` and `note.html` (Node)
    - the `~/cyd` copies
- **Why.** A parity rule enforced by memory becomes a failing test.

#### O6 · Token pipeline, with a Figma bridge — H · M · Tools Docs
- **What.**
  - One `tokens.json`, using design.md's dotted names, generates:
    - `src/tokens.h` (RGB565)
    - the sim's TOKENS block (RGB888 with the `// 0x....` comment)
    - the design.md colour tables
  - That's the same idea as `make_vlw.py` for fonts.
  - Also export **Figma Variables**, so mock-ups use the exact RGB565-safe
    palette and type scale. A design manager can then hand a teammate a Figma
    file that can't drift from the device.
- **Done when.** Changing one value in `tokens.json` and running the generator
  updates the firmware, the sim and Figma, with no hand edits.

#### ✅ O8 · CI — M · M · Tools
- **What.** GitHub Actions on every push:
  - `pio run`, failing on unplanned growth or past **75% of the slot**. It's
    45% today, and the embedded cats are most of the growth.
  - `pio test -e native`
  - the O5 corpus
  - a headless sim check that renders every page with fixtures and compares
    against golden PNGs

  The board-vs-sim compare stays local, because it needs the board.
- **Shipped (2026-10-14).** `.github/workflows/ci.yml`, on every push and PR:
  - **firmware:** `pio run` with `config.example.h`, then `tools/size_check.py`.
    It fails past 75% of the slot, or more than 32 KB over
    `tools/size_baseline.json`; planned growth moves the baseline in the same
    commit.
  - **simulator:** `tools/sim_check.mjs` (Playwright 1.63.0, pinned). It
    renders all 7 pages and the Weather, Device and Settings sheets in both
    themes from `tests/fixtures/default.json`, and compares 20 goldens pixel
    for pixel. This added the sim half of O7: `?fixture=<name>` freezes the
    clock and data. The board half (a serial fixture command) and
    `tools/parity.py` are still O7.
  - **Waiting on their items:** `pio test -e native` (O4) and the O5 corpus
    get steps when they exist.

#### ✅ O9 · OTA updates — H · M · FW Srv Tools
- **What.**
  - Pull firmware over HTTP from the Mac server (`/firmware.bin`, LAN only).
  - Trigger from a Settings row, or when the server announces a newer build.
  - It uses the dual 6.25 MB app slots that already exist.
  - Call `esp_ota_mark_app_valid` only after the first good poll, so a bad
    build rolls back on its own.
- **Why.** The board can be mounted on the desk with no cable to reach, and
  no BOOT/RST dance.
- **Note.** Independent of O8 (CI). Pull it to the front of M2: every M2
  feature is slower to iterate on over a USB cable.
- **Shipped (2026-10-14).**
  - **Publish:** `pio run -t publish` copies the build to
    `~/.cyd_firmware/s3.bin`. A plain `pio run` never reaches the board.
  - **Server:** announces `firmware.s3.{sha256,size}` and serves
    `/firmware/s3.bin` to this Mac and to boards that poll it.
  - **Identity:** the sha256 is esptool's appended image digest, compared with
    the running partition's, so no version numbers.
  - **Board:** Settings > Firmware (state, **Auto update** on by default,
    **Update now**). Auto installs only a build it hasn't acted on, so a USB
    dev build isn't replaced by an older publish.
  - **Probation:** a new build stays `PENDING_VERIFY` until its first good
    poll. A crash first, or 10 min without one, rolls back, and a rolled-back
    build isn't auto-installed again.
  - **Verified on the board:** install over WiFi (~20 s for 2.9 MB), confirm,
    and the bootloader rollback of a build that crashes before its first poll.

#### O14 · Settings backup and restore — M · S · FW Srv
- **Why.** The settings blob lives in NVS, and F10 adds page toggles plus an
  editable night window on top of it. One corrupt blob or bad migration and
  22 settings get re-entered by hand. A desk companion shouldn't lose its
  personality to a flash hiccup.
- **What.**
  - The board POSTs its settings to the Mac server (piggyback on an existing
    write path; auth is designed once here and reused by F8).
  - The server keeps the last known-good copy; a fresh boot with a missing or
    invalid blob restores it instead of defaults.
- **Done when.** Pulling the settings partition (or corrupting it on purpose)
  and rebooting restores the previous configuration.

### Later

#### O10 · Split the big files — M · M · FW Sim
- **What.**
  - `pages.cpp` (84 KB) becomes one unit per page: status, usage, note,
    device, weather.
  - `state.h` (35 KB) becomes `layout.h` plus `state.h`.
  - Mirror the split in the sim's sections.
- **When.** Only after O4 and O7 exist, so the refactor is verifiably a no-op.

#### O11 · A media partition — L · M · FW Tools
- **What.**
  - Move the embedded cats (~1.3 MB of today's 2.97 MB image) out of the app
    and into a LittleFS or raw data partition, together with short movie clips.
  - The app then drops to ~1.7 MB, so a custom `partitions.csv` with two
    ~3 MB app slots plus ~9 MB of media fits.
  - Run a `gifsicle -O3 --lossy` pass over the GIFs first.
- **Trade.** It lowers the app ceiling, and cats then need a data-partition
  flash step. Movies no longer need the SD card.

#### O12 · Toolchain review — L · S · Tools
- **What.** Stay pinned to pioarduino 51.03.07 (core 3.0.7, IDF 5.1). Evaluate a
  newer core in a branch **only** if a roadmap item needs it, for example
  esp_lcd fixes or power management.
- **Cost of moving.** Re-verify the vendored AXS15231B driver and the
  `esp_lcd_panel_io.h` include fix.

#### O13 · Crash and health telemetry — M · S · FW Srv
- **What.**
  - Add a core-dump partition, and log the reset reason to `/diag_log.csv`.
  - Show uptime, the last reset reason and the RSSI trend in Device Stats.
  - Piggyback a heartbeat on the poll's query string
    (`/api/usage?up=…&rr=…`), so the Mac's `server.html` can show the board's
    health with no new endpoint.

---

## 10. Milestones

Each milestone ends with the U4 glance test and an O7 parity run, and both
results are recorded.

| Milestone | Theme | Contents | Exit criteria |
|---|---|---|---|
| **M1 · Trustworthy** (Now) | Nothing on the device lies or is hard to hit | U1–U5, F5a, P1, P8, O1–O4, O7 | Every control works, or is hidden with a "Coming soon" label until F1. Strip targets are ≥ 40 px. §18.4 is closed. Baselines are measured. Native tests run. Parity is one command (O7). |
| **M2 · Needs you** (Next) | Glance questions 2 and 3 get answers | U7, U9, U10, F1, F2, F3, F4, O5, O9, O14 | A waiting Claude session shows its project name within 20 s on every page. The next meeting is readable at 60 cm. Stale data looks stale. Updates go over OTA. |
| **M3 · Calm and quick** (Next) | Less cost, less clutter | U6, U8, F10, P2, P5, P6, P7, O6, O8 | Status page compose ≤ 20 ms, ≤ 10% idle CPU. Settings ≤ 1.5 screens. CI is green. |
| **M4 · Insight** (Later) | Trends and the team | F4b, F5b, F6–F8, F11, U11, U12, P3, P4, O10–O13 | Each item is gated by its own spike or validation step. |

---

## 11. Success metrics

Measured on the board unless noted. "Today" values marked † come from
`CLAUDE.md` or commits; P1 re-measures all of them.

| Metric | Today | Target | How measured |
|---|---|---|---|
| One-second glance test, 3 questions × all pages, day and night | Not recorded | 100% pass | U4 protocol |
| Accidental commits in the brush test | Not recorded | 0 in 20 pickups | U4 protocol |
| Touch targets below the minimum | 0 ✅ (was 2: the strip, 56 × 20) | 0 | `state.h` audit + U3 |
| Status page compose | ~40–54 ms † | ≤ 20 ms | `[timing]` |
| Idle CPU, Status page | 16% † | ≤ 10% | Device Stats / `[timing]` |
| Presents at rest, page 0 | ~7.8/s † | ≤ 4/s | `[timing]` |
| Transition composite | 23 / 30 ms † | ≤ 16 ms (stretch) | `[motion]` |
| Movie playback | 9.6 fps † | ≥ 15 fps | movie player log |
| Boot to useful screen | Unmeasured | ≤ 3 s | P1 |
| "Claude needs you" latency | No such feature | ≤ 20 s polled, ≤ 2 s pushed | Hook timestamp vs. board log |
| Firmware size | 2.97 MB (45%), measured | No unplanned growth; ≤ 75% of the slot | CI gate |
| Power at rest / asleep | Unmeasured | Recorded (P1), then −20% at rest | USB power meter; the −20% is owned by P5 |
| Parity check | Manual | One command; 0 unexplained diffs | O7 |
| Settings that do nothing | 0 ✅ (was 2) | 0 | U1 |

---

## 12. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| **Feature creep breaks the calm.** Every new signal competes for the eye | High | Every item answers a glance question or ships off by default. F6 is gated on a diary study, and U10 decides the hierarchy by test, not taste |
| **Parity tax grows** with every feature | High | O5, O6 and O7 first. Firmware-only optimisations (P2–P5) are pixel-identical by definition, and that's proven by O7 |
| **Contract drift with the CYD**, or a bigger payload exhausting its heap | Medium | Fields are additive and optional. Large S3-only blocks go behind a `?client=s3` projection, beside the existing `?client=cyd` (§3.4). Run one CYD smoke test per contract change |
| **Present budget creep** (each animated feature adds full-frame presents) | Medium | Each item declares its presents per second. P5's target is a hard ceiling, checked in CI against `[timing]` |
| **Memory pressure** from agenda, history and Thai fonts | Medium | P8's budget table is the gate; price each feature before building it |
| **Calendar access** on the Mac (TCC permission prompts, the Python helper) | Medium | A Swift helper with its own Calendar permission; `icalBuddy` as the fallback. Decided in `~/cyd` |
| **Sound in an office** annoys neighbours | Medium | Defaults: alerts on, chime off. "Mute at night" and "Mute when asleep" already exist; F3 adds "Mute in focus" |
| **OTA bricking** | Low | Rollback via `esp_ota_mark_app_valid` after the first good poll; USB flashing always remains |

---

## 13. Not doing, and why

| Idea | Why not |
|---|---|
| TLS on the board, or direct API calls from it | The contract and the Mac's battery-aware fetch cadence are shared with the CYD; the Mac does the internet (`CLAUDE.md`) |
| LVGL or another UI toolkit | The token, glyph and parity pipeline *is* the UI framework; LVGL would break pixel parity with the sim |
| Partial-window updates | The panel can't address partial windows in QSPI mode; every present is a full frame (`CLAUDE.md`) |
| Multi-touch gestures | The controller reports a single point; two fingers read as no touch |
| Translucency, blur, shadows | RGB565 with no alpha; depth comes from surface steps and the scrim (`design.md` §8.4) |
| The CYD's full 120-cat library | It's ~83 MB against a 4 MB embed budget; a curated subset, or O11 |
| Upgrading the toolchain for its own sake | The vendored panel driver is tied to core 3.0.7; move only for a concrete need (O12) |

---

## 14. Open questions for the owner

1. **Calendar.** Which calendar holds work meetings: Google Workspace,
   Microsoft 365, or iCloud? Is it already in macOS Calendar.app? This decides
   F2's server approach. **Answer by:** the next monthly review — it gates F2.
2. **Team tools.** Which tools would F6 draw from: Figma, Jira, Linear, GitHub,
   Slack? Would counts actually change what you do? That's the F6 gate.
3. **Visibility.** Do teammates see the board? This decides whether F4b
   (presence mode) is worth its font.
4. **Sound.** Is a chime acceptable at your desk, or should attention be
   backlight-only by default? **Answer by:** the next monthly review — it sets
   the U7/F1 sound defaults.
5. **Lockstep.** Should new contract fields also appear on the CYD, or may the
   S3 get features the CYD doesn't? **Answer by:** the next monthly review —
   it shapes every future contract change (§3.4).
6. **History.** Rewrite git history to drop the 42 MB video (O1), or just stop
   tracking it? For a single-author repo, `git filter-repo` is a 10-minute job
   now and a painful one after more history accrues — leaning yes, but it's
   destructive, so it's the owner's call.
7. **Thai.** How often are meeting titles or notes in Thai? This sizes F11.

---

## 15. How we work through this roadmap

**For each item:**
1. **Design first.** Add or adjust tokens and components in `design.md`,
   following its §17 process.
2. **Prototype in the simulator.** Anything that moves gets reviewed in slow
   motion (`?slowmo=10`) before it touches firmware.
3. **Build both twins** in the same change, following `CLAUDE.md`'s
   invariants.
4. **Verify.**
   - Sim and board screenshots compared (O7, once it exists).
   - A CYD smoke test whenever the `/api/usage` contract changed.
   - The `design.md` §16 checklist.
   - Both themes.
   - `[timing]` and `[motion]` numbers recorded against §11.
5. **Glance-test it** on the desk at 60 cm (U4).
6. **Update this file.** Mark the item done, move the metric, and note
   anything learned.

**Cadence.**
- **Monthly:** a 30-minute review. Re-score Value against §1, move items
  between horizons, and close what's done.
- **Each milestone:** a full U4 run.

**Status key** (prefix an item's heading when it changes): ✅ done ·
🚧 in progress · ⏸ paused · ❌ dropped (with a one-line reason).
