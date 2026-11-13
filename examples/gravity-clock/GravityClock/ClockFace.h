#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "ClockFonts.h"

namespace face {
constexpr int W = 480;
inline uint16_t rgb(int r, int g, int b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }
const uint16_t bg = rgb(9, 15, 22), white = rgb(237, 241, 244), muted = rgb(121, 144, 158),
               accent = rgb(100, 222, 207);
inline uint16_t blend(uint16_t src, uint16_t dst, int a) {
  int r = (((src >> 11) & 31) * a + ((dst >> 11) & 31) * (255 - a) + 127) / 255;
  int g = (((src >> 5) & 63) * a + ((dst >> 5) & 63) * (255 - a) + 127) / 255;
  int b = ((src & 31) * a + (dst & 31) * (255 - a) + 127) / 255;
  return (r << 11) | (g << 5) | b;
}
// One coverage calculation per pixel avoids repeatedly blending overlapping dots.
inline void line(uint16_t *p, float x1, float y1, float x2, float y2, float width, uint16_t c) {
  const float dx = x2 - x1, dy = y2 - y1, length2 = dx * dx + dy * dy;
  const float radius = width * 0.5f, margin = radius + 0.5f;
  const int left = std::max(0, int(std::floor(std::min(x1, x2) - margin)));
  const int right = std::min(W - 1, int(std::ceil(std::max(x1, x2) + margin)));
  const int top = std::max(0, int(std::floor(std::min(y1, y2) - margin)));
  const int bottom = std::min(W - 1, int(std::ceil(std::max(y1, y2) + margin)));
  for (int y = top; y <= bottom; y++) {
    for (int x = left; x <= right; x++) {
      const float t =
          length2 > 0 ? std::clamp(((x - x1) * dx + (y - y1) * dy) / length2, 0.f, 1.f) : 0;
      const float ex = x - x1 - t * dx, ey = y - y1 - t * dy;
      const float coverage = std::clamp(margin - std::sqrt(ex * ex + ey * ey), 0.f, 1.f);
      if (coverage > 0)
        p[y * W + x] = blend(c, p[y * W + x], std::lround(coverage * 255));
    }
  }
}
inline void text(uint16_t *p, const char *s, int y, const ClockFont &font, uint16_t color,
                 int spacing = 0) {
  int width = 0;
  for (const char *c = s; *c; c++)
    if (*c >= 32 && *c < 127)
      width += font.glyphs[*c - 32].advance + spacing;
  if (width)
    width -= spacing;
  int x = (W - width) / 2;
  for (const char *c = s; *c; c++) {
    if (*c < 32 || *c >= 127)
      continue;
    const auto &g = font.glyphs[*c - 32];
    for (int yy = 0; yy < g.height; yy++)
      for (int xx = 0; xx < g.width; xx++) {
        int dx = x + xx, dy = y + yy;
        if (dx < 0 || dx >= W || dy < 0 || dy >= W)
          continue;
        uint8_t a = font.pixels[g.offset + yy * g.width + xx];
        if (a)
          p[dy * W + dx] = blend(color, p[dy * W + dx], a);
      }
    x += g.advance + spacing;
  }
}
inline void draw(uint16_t *p, int hour, int minute, int second, int month, int day, int weekday,
                 bool valid) {
  for (int i = 0; i < W * W; i++)
    p[i] = bg;
  for (int i = 0; i < 60; i++) {
    float a = i * 6.28318530718f / 60;
    float r1 = i % 5 ? 213 : 204, r2 = 220;
    uint16_t color = valid && i <= second ? accent : rgb(39, 56, 68);
    line(p, 239.5f + std::sin(a) * r1, 239.5f - std::cos(a) * r1, 239.5f + std::sin(a) * r2,
         239.5f - std::cos(a) * r2, i % 5 ? 2 : 3, color);
  }
  const char *days[] = {"SUNDAY",   "MONDAY", "TUESDAY", "WEDNESDAY",
                        "THURSDAY", "FRIDAY", "SATURDAY"};
  const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  text(p, valid ? days[weekday % 7] : "CLOCK", 132, smallFont, accent, 3);
  char b[40];
  if (valid)
    std::snprintf(b, sizeof(b), "%02d:%02d", hour, minute);
  else
    std::strcpy(b, "--:--");
  text(p, b, 190, largeFont, white);
  if (valid)
    std::snprintf(b, sizeof(b), "%02d %s", day, months[(month - 1) % 12]);
  else
    std::strcpy(b, "SYNC TIME VIA USB");
  text(p, b, 300, smallFont, muted, 2);
  if (valid)
    std::snprintf(b, sizeof(b), "%02d", second);
  else
    std::strcpy(b, "--");
  text(p, b, 345, smallFont, accent, 2);
}
// Separate R/G/B into non-overlapping lanes so 5-bit interpolation can use
// 32-bit integer arithmetic without channel bleed. Weight is in [0, 32].
inline uint16_t lerp565(uint16_t a, uint16_t b, uint32_t weight) {
  constexpr uint32_t mask = 0x07e0f81f, rounding = (16u << 21) | (16u << 11) | 16u;
  const uint32_t av = (uint32_t(a) | (uint32_t(a) << 16)) & mask;
  const uint32_t bv = (uint32_t(b) | (uint32_t(b) << 16)) & mask;
  const uint32_t result = ((av * (32 - weight) + bv * weight + rounding) >> 5) & mask;
  return uint16_t(result | (result >> 16));
}
inline uint16_t bilinear565(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint32_t fx,
                            uint32_t fy) {
  // Most of the face is a flat background or solid glyph interior.
  if (a == b && a == c && a == d)
    return a;
  return lerp565(lerp565(a, b, fx), lerp565(c, d, fx), fy);
}
inline uint16_t pixelOrBackground(const uint16_t *src, int x, int y) {
  return (x >= 0 && x < W && y >= 0 && y < W) ? src[y * W + x] : bg;
}
// Cache uniform 16x16 source tiles in internal RAM. Rebuild only when the
// unrotated clock face changes, not for each orientation update.
struct RotationCache {
  static constexpr int Tile = 16, Columns = W / Tile;
  uint16_t color[Columns * Columns] = {};
  bool solid[Columns * Columns] = {};
  void prepare(const uint16_t *src) {
    for (int ty = 0; ty < Columns; ty++) {
      for (int tx = 0; tx < Columns; tx++) {
        int index = ty * Columns + tx;
        const uint16_t *base = src + ty * Tile * W + tx * Tile;
        color[index] = base[0];
        solid[index] = true;
        for (int y = 0; y < Tile && solid[index]; y++)
          for (int x = 0; x < Tile; x++)
            if (base[y * W + x] != color[index]) {
              solid[index] = false;
              break;
            }
      }
    }
  }
};
// Inverse rotation in 16.16 fixed point. Bilinear filtering preserves glyph
// coverage at oblique angles. Always sample the original face, never a prior frame.
inline void rotate(const uint16_t *src, uint16_t *dst, float angle,
                   const RotationCache *cache = nullptr) {
  int32_t c = std::lround(std::cos(angle) * 65536), s = std::lround(std::sin(angle) * 65536);
  if (c == 65536 && s == 0) {
    std::memcpy(dst, src, W * W * sizeof(uint16_t));
    return;
  }
  for (int y = 0; y < W; y++) {
    int32_t sx =
        std::lround(239.5f * 65536) + int64_t(c) * (-479) / 2 + int64_t(s) * (2 * y - 479) / 2;
    int32_t sy =
        std::lround(239.5f * 65536) - int64_t(s) * (-479) / 2 + int64_t(c) * (2 * y - 479) / 2;
    for (int x = 0; x < W; x++, sx += c, sy -= s) {
      const int ix = sx >> 16, iy = sy >> 16;
      const uint32_t fx = ((uint32_t(sx) & 65535) + 1024) >> 11;
      const uint32_t fy = ((uint32_t(sy) & 65535) + 1024) >> 11;
      uint16_t a, b, c, d;
      if (ix >= 0 && ix < W - 1 && iy >= 0 && iy < W - 1) {
        if (cache && (ix & 15) != 15 && (iy & 15) != 15) {
          int tile = (iy >> 4) * RotationCache::Columns + (ix >> 4);
          if (cache->solid[tile]) {
            dst[y * W + x] = cache->color[tile];
            continue;
          }
        }
        const uint16_t *row = src + iy * W + ix;
        a = row[0];
        b = row[1];
        c = row[W];
        d = row[W + 1];
      } else {
        a = pixelOrBackground(src, ix, iy);
        b = pixelOrBackground(src, ix + 1, iy);
        c = pixelOrBackground(src, ix, iy + 1);
        d = pixelOrBackground(src, ix + 1, iy + 1);
      }
      dst[y * W + x] = bilinear565(a, b, c, d, fx, fy);
    }
  }
}
} // namespace face
