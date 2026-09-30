// Cat GIFs linked into the firmware image (flash, memory-mapped). The table is
// generated at build time from cats/*.gif by tools/embed_cats.py into
// src/cats_embed.gen.cpp (gitignored) -- nothing else knows how the bytes get
// here. AnimatedGIF decodes straight from `data`; no copy, no SD.
#pragma once
#include <stdint.h>

struct EmbeddedCat {
  const uint8_t* data;
  uint32_t size;
  const char* name;   // basename, for logs
};

extern const EmbeddedCat EMBEDDED_CATS[];
extern const int EMBEDDED_CAT_COUNT;   // 0 only if cats/ had no GIFs (the script refuses to build then)
