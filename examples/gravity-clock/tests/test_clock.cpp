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
  std::ofstream f(argc > 1 ? argv[1] : "preview.ppm", std::ios::binary);
  assert(f.is_open());
  f << "P6\n480 480\n255\n";
  for (auto v : src) {
    char b[] = {char(((v >> 11) & 31) * 255 / 31), char(((v >> 5) & 63) * 255 / 63),
                char((v & 31) * 255 / 31)};
    f.write(b, 3);
  }
  std::cout << "PASS: cardinal/diagonal angles, wraparound, flat hold, acceleration rejection, "
               "identity/90-degree rendering\n";
}
