// Movie player entry points (movie_player.cpp); movie_player.h holds the JPEGDEC type.
#pragma once
#include <Arduino.h>

// ── MOVIE PLAYER (movie_player.cpp) ─────────────────────────
// Random .mjpeg playback on MOVIE_PAGE, the same shape as the GIF player
// above but for raw MJPEG streams from /movies/ on the SD card. Reuses
// catShuffleMs/catShuffleFixed (the "Cat Shuffle" setting) for its own
// rotation timing -- see movie_player.cpp for why. gif_player.cpp's gifTick()
// and its five sibling entry points each dispatch into this module's
// equivalents when currentPage == MOVIE_PAGE, so main.cpp/nav.cpp need no
// separate call sites for movies.
void scanMovies();
bool movieTick(bool offline);
void moviePlayerEnter();
void moviePlayerExit();
void moviePlayerResetForPageChange();
void moviePlayerPrimeFrame(bool offline);
void moviePlayerStagePage(bool offline, const uint16_t* poster);
void moviePlayerRepaint(bool offline);
