#pragma once
#include <cmath>
struct Vec3 {
  float x = 0, y = 0, z = 0;
};
inline float wrapAngle(float a) { return std::atan2(std::sin(a), std::cos(a)); }
struct Orientation {
  Vec3 up;
  bool initialized = false, tracking = false;
  float angle = 0, offset = 0, handedness = 1;
  unsigned rejected = 0;
  float rawAngle() const { return handedness * std::atan2(up.x, up.y); }
  void update(Vec3 a, Vec3 dps, float dt) {
    if (!(dt > 0 && dt < 0.25f))
      return;
    const float n = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    const bool valid = std::isfinite(n) && n > 0.80f && n < 1.20f;
    if (!initialized) {
      if (!valid)
        return;
      up = {a.x / n, a.y / n, a.z / n};
      initialized = true;
    }
    const float k = 0.01745329252f;
    // A stationary world vector has derivative -omega x vector in sensor axes.
    const Vec3 w = {dps.x * k, dps.y * k, dps.z * k};
    const Vec3 p = {up.x + (w.z * up.y - w.y * up.z) * dt, up.y + (w.x * up.z - w.z * up.x) * dt,
                    up.z + (w.y * up.x - w.x * up.y) * dt};
    const float blend = valid ? dt / (0.20f + dt) : 0;
    if (!valid)
      rejected++;
    up = {p.x * (1 - blend) + (valid ? a.x / n : 0) * blend,
          p.y * (1 - blend) + (valid ? a.y / n : 0) * blend,
          p.z * (1 - blend) + (valid ? a.z / n : 0) * blend};
    const float mag = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
    if (!(mag > 0.1f && std::isfinite(mag))) {
      initialized = false;
      return;
    }
    up.x /= mag;
    up.y /= mag;
    up.z /= mag;
    float projection = std::hypot(up.x, up.y);
    // Hysteresis: retain the last readable direction near face-up / face-down.
    if (projection < 0.22f)
      tracking = false;
    else if (projection > 0.32f)
      tracking = true;
    if (tracking) {
      const float error = wrapAngle(rawAngle() + offset - angle);
      if (std::fabs(error) > 0.006f)
        angle = wrapAngle(angle + error * dt / (0.09f + dt));
    }
  }
};
