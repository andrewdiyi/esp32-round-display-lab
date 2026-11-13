#pragma once
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
inline void dot(uint16_t *p, float x, float y, float r, uint16_t c) {
  for (int iy = int(y - r - 1); iy <= int(y + r + 1); iy++)
    for (int ix = int(x - r - 1); ix <= int(x + r + 1); ix++)
      if (ix >= 0 && ix < W && iy >= 0 && iy < W &&
          (ix - x) * (ix - x) + (iy - y) * (iy - y) <= r * r)
        p[iy * W + ix] = c;
}
inline void line(uint16_t *p, float x1, float y1, float x2, float y2, float width, uint16_t c) {
  int n = int(std::hypot(x2 - x1, y2 - y1) * 1.5f) + 1;
  for (int i = 0; i <= n; i++)
    dot(p, x1 + (x2 - x1) * i / n, y1 + (y2 - y1) * i / n, width / 2, c);
}
inline uint16_t blend(uint16_t src, uint16_t dst, int a) {
  int r = (((src >> 11) & 31) * a + ((dst >> 11) & 31) * (255 - a) + 127) / 255;
  int g = (((src >> 5) & 63) * a + ((dst >> 5) & 63) * (255 - a) + 127) / 255;
  int b = ((src & 31) * a + (dst & 31) * (255 - a) + 127) / 255;
  return (r << 11) | (g << 5) | b;
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
// Inverse rotation in fixed point. The outer disk is rotation-invariant.
inline void rotate(const uint16_t *src, uint16_t *dst, float angle) {
  int32_t c = std::lround(std::cos(angle) * 65536), s = std::lround(std::sin(angle) * 65536);
  for (int y = 0; y < W; y++) {
    int32_t sx =
        std::lround(239.5f * 65536) + int64_t(c) * (-479) / 2 + int64_t(s) * (2 * y - 479) / 2;
    int32_t sy =
        std::lround(239.5f * 65536) - int64_t(s) * (-479) / 2 + int64_t(c) * (2 * y - 479) / 2;
    for (int x = 0; x < W; x++, sx += c, sy -= s) {
      int ix = (sx + 32768) >> 16, iy = (sy + 32768) >> 16;
      dst[y * W + x] = (ix >= 0 && ix < W && iy >= 0 && iy < W) ? src[iy * W + ix] : bg;
    }
  }
}
} // namespace face
