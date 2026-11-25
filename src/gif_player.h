// GIF player (gif_player.cpp).
#pragma once
#include <Arduino.h>

// ── GIF PLAYER (gif_player.cpp) ─────────────────────────────
// Returns true when it changed `frame` -- loop() presents once per pass.
bool gifTick(bool offline);
// Encapsulate what used to be loop() reaching directly into gif/gifOpen/
// gifNextFrameMs — narrows the extern surface to three intent-named calls
// instead of four raw globals.
void gifPlayerEnterCatMode();       // allocate the decoder on entering a cat page (or offline)
void gifPlayerExitCatMode();        // free the decoder on leaving
void gifPlayerResetForPageChange(); // force a fresh random GIF on the next tick
// Decode + draw the first frame right now (into `frame`, then presentFrame()
// -- held during a page slide), instead of waiting for the next gifTick().
void gifPlayerPrimeFrame(bool offline);
// Stage the page for a slide onto it without opening the player: the
// poster (the page's last screen, or nullptr for the plain plate) + overlays,
// into `frame`, no present. The next gifTick() after the slide lands opens
// and decodes the first frame (loop() gates it on navPageTransitionActive()).
void gifPlayerStagePage(bool offline, const uint16_t* poster);
// Re-blit the whole open GIF canvas (+ the page's static parts and
// overlays) into `frame` -- after a sheet or a cancelled lean overwrote it.
void gifPlayerRepaint(bool offline);
// Redraw just the overlays (reset plate, shuffle button, corner) and present
// -- a pressed state changed; each overlay keeps its footprint, so nothing
// under it needs restoring.
void gifPlayerRedrawOverlays(bool offline);
// Reset plate + shuffle button + corner glyphs, drawn over whichever media is
// active. Shared with movie_player.cpp: MOVIE_PAGE uses the same overlays.
void drawMediaOverlays(bool offline);
