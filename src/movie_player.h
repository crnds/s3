// MOVIE_PAGE: random .mjpeg playback from /movies/ on the SD card. See the
// header comment at the top of gif_player.cpp for how this module's entry
// points (movieTick, moviePlayerEnter/Exit/ResetForPageChange/PrimeFrame/
// Repaint) are wired in -- they're called from gif_player.cpp's own six
// entry points, not from nav.cpp/main.cpp directly. Declared in state.h
// alongside the GIF player; this header exists only for JPEGDEC's own type,
// kept out of state.h so not every .cpp in the sketch needs to see it.
#pragma once
#include <JPEGDEC.h>
