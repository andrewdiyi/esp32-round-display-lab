#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#include "../GravityClock/Orientation.h"
#include "../GravityClock/ClockFace.h"
int main(int argc, char **argv) {
  for (float deg : {0.f, 45.f, 90.f, 179.f, -179.f, -90.f}) {
    Orientation o;
    float a = deg * 0.01745329252f;
    for (int i = 0; i < 400; i++)
      o.update({std::sin(a), std::cos(a), 0}, {0, 0, 0}, 0.01f);
    assert(std::fabs(wrapAngle(o.angle - a)) < 0.012f);
  }
  Orientation o;
  for (int i = 0; i < 200; i++)
    o.update({0, 1, 0}, {0, 0, 0}, 0.01f);
  for (int i = 0; i < 100; i++) {
    float a = (i + 1) * 1.57079632679f / 100;
    o.update({0, std::cos(a), std::sin(a)}, {90, 0, 0}, 0.01f);
  }
  for (int i = 0; i < 200; i++)
    o.update({0, 0, 1}, {0, 0, 0}, 0.01f);
  assert(!o.tracking);
  float held = o.angle;
  for (int i = 0; i < 100; i++)
    o.update({0.02f, -0.01f, 1}, {0, 0, 0}, 0.01f);
  assert(o.angle == held);
  o.update({5, 5, 0}, {0, 0, 0}, 0.01f);
  assert(o.rejected > 0);
  std::vector<uint16_t> src(480 * 480), dst(src.size());
  face::draw(src.data(), 13, 42, 28, 11, 13, 5, true);
  face::rotate(src.data(), dst.data(), 0);
  assert(src == dst);
  face::rotate(src.data(), dst.data(), 1.57079632679f);
  for (int y = 0; y < 480; y++)
    for (int x = 0; x < 480; x++)
      assert(dst[y * 480 + x] == src[(479 - x) * 480 + y]);
  // Channel-isolation and endpoint tests for the packed RGB565 arithmetic.
  for (uint16_t color : {uint16_t(0), uint16_t(0xffff), uint16_t(0xf800), uint16_t(0x07e0),
                         uint16_t(0x001f), uint16_t(0x435a)}) {
    for (unsigned weight = 0; weight <= 32; weight++) {
      assert(face::lerp565(color, color, weight) == color);
      assert(face::lerp565(color, uint16_t(~color), 0) == color);
      assert(face::lerp565(uint16_t(~color), color, 32) == color);
    }
  }
  assert(face::bilinear565(0, 0xffff, 0xffff, 0, 16, 16) == face::rgb(128, 128, 128));
  // Compare against an independent full-precision four-tap reference.
  uint32_t random = 0x12345678;
  for (int i = 0; i < 5000; i++) {
    uint16_t colors[4];
    for (auto &color : colors) {
      random = random * 1664525u + 1013904223u;
      color = uint16_t(random >> 8);
    }
    unsigned fx = i % 33, fy = (i / 33) % 33;
    uint16_t actual = face::bilinear565(colors[0], colors[1], colors[2], colors[3], fx, fy);
    for (int shift : {0, 5, 11}) {
      int mask = shift == 5 ? 63 : 31;
      int a = (colors[0] >> shift) & mask, b = (colors[1] >> shift) & mask;
      int c = (colors[2] >> shift) & mask, d = (colors[3] >> shift) & mask;
      double reference =
          (a * (32 - fx) * (32 - fy) + b * fx * (32 - fy) + c * (32 - fx) * fy + d * fx * fy) /
          1024.0;
      assert(std::abs(int((actual >> shift) & mask) - std::lround(reference)) <= 1);
    }
  }
  // A rotated edge must contain fractional coverage, without shifting its area.
  std::vector<uint16_t> edge(src.size(), 0);
  for (int y = 160; y < 320; y++)
    for (int x = 160; x < 320; x++)
      edge[y * 480 + x] = 0xffff;
  face::rotate(edge.data(), dst.data(), 17.f * 0.01745329252f);
  int partial = 0;
  double area = 0;
  for (int y = 100; y < 380; y++)
    for (int x = 100; x < 380; x++) {
      auto v = dst[y * 480 + x];
      if (v != 0 && v != 0xffff)
        partial++;
      area += ((v >> 5) & 63) / 63.0;
    }
  assert(partial > 400);
  assert(std::abs(area - 160 * 160) < 60);
  // Out-of-image taps must not create dark seams on a uniform background.
  std::fill(edge.begin(), edge.end(), face::bg);
  for (float degrees : {3.f, 17.f, 45.f, -37.f, 180.f, 270.f}) {
    face::rotate(edge.data(), dst.data(), degrees * 0.01745329252f);
    assert(dst == edge);
  }
  // Cached and uncached sampling must be pixel-identical, including tile borders.
  face::RotationCache cache;
  std::vector<uint16_t> cached(src.size());
  for (auto *input : {src.data(), edge.data()}) {
    cache.prepare(input);
    for (float degrees : {0.f, 3.f, 17.f, 45.f, -37.f, 90.f, 180.f, 270.f}) {
      face::rotate(input, dst.data(), degrees * 0.01745329252f);
      face::rotate(input, cached.data(), degrees * 0.01745329252f, &cache);
      assert(dst == cached);
    }
  }
  std::ofstream f(argc > 1 ? argv[1] : "preview.ppm", std::ios::binary);
  assert(f.is_open());
  f << "P6\n480 480\n255\n";
  for (auto v : src) {
    char b[] = {char(((v >> 11) & 31) * 255 / 31), char(((v >> 5) & 63) * 255 / 63),
                char((v & 31) * 255 / 31)};
    f.write(b, 3);
  }
  std::cout << "PASS: cardinal/diagonal angles, wraparound, flat hold, acceleration rejection, "
               "identity/90-degree rendering, bilinear channel accuracy, fractional edge coverage, "
               "borders\n";
}
