#pragma once
#include "ClockFace.h"
#include "Weather.h"
#include <vector>

enum class Page {
  Face,
  Apps,
  Controls,
  Settings,
  Stopwatch,
  Timer,
  Sensors,
  Info,
  Wifi,
  Weather,
  DisplaySettings
};
enum class WifiView { Status, Networks, Password };
struct WifiNetwork {
  char ssid[33];
  int rssi;
  bool open, supported;
};
struct WatchState {
  Page page = Page::Face;
  bool rotate = true, format24 = true, wifiEnabled = false, connected = false;
  bool clockValid = false, stopwatchRunning = false, timerRunning = false, timerDone = false;
  int brightness = 55, sleepSeconds = 30, hour = 0, minute = 0, second = 0, month = 1, day = 1,
      weekday = 0;
  uint32_t stopwatchMs = 0, timerMs = 180000, freeHeap = 0, freePsram = 0, uptime = 0;
  int flashMB = 16, psramMB = 8, cpuMHz = 240, rssi = 0;
  float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0, battery = 0;
  char network[33] = "", ip[24] = "";
  char wifiMessage[48] = "Choose a 2.4 GHz network";
  WifiView wifiView = WifiView::Status;
  std::vector<WifiNetwork> networks;
  int networkOffset = 0, keyboardMode = 0;
  bool scanning = false, connecting = false, connectionFailed = false, uppercase = false,
       showPassword = false, selectedOpen = false;
  char selectedSSID[33] = "", password[64] = "";
  time_t ntpSyncedAt = 0;
  bool timeSyncing = false, weatherLoading = false;
  int weatherCity = 0;
  WeatherView weatherView = WeatherView::Now;
  WeatherData weather;
  char weatherMessage[48] = "Connect Wi-Fi for weather";
};

namespace watchui {
using face::blend;
using face::line;
using face::rgb;
const uint16_t bg = face::bg, fg = face::white, dim = face::muted, card = rgb(24, 32, 42);
const uint16_t orange = rgb(255, 156, 52), green = rgb(123, 233, 82), blue = rgb(58, 163, 255),
               cyan = rgb(79, 221, 213), purple = rgb(188, 125, 255), red = rgb(255, 92, 110);
inline void label(uint16_t *p, const char *s, int x, int y, const ClockFont &f, uint16_t color,
                  bool centered = true) {
  int width = 0;
  for (const char *c = s; *c; c++)
    if (*c >= 32 && *c < 127)
      width += f.glyphs[*c - 32].advance;
  if (centered)
    x -= width / 2;
  for (const char *c = s; *c; c++) {
    if (*c < 32 || *c >= 127)
      continue;
    const auto &g = f.glyphs[*c - 32];
    for (int yy = 0; yy < g.height; yy++)
      for (int xx = 0; xx < g.width; xx++) {
        int dx = x + xx, dy = y + yy;
        if (dx < 0 || dx >= 480 || dy < 0 || dy >= 480)
          continue;
        uint8_t a = f.pixels[g.offset + yy * g.width + xx];
        if (a)
          p[dy * 480 + dx] = blend(color, p[dy * 480 + dx], a);
      }
    x += g.advance;
  }
}
inline void circle(uint16_t *p, float x, float y, float radius, uint16_t color) {
  for (int py = std::max(0, int(y - radius - 1)); py < std::min(480, int(y + radius + 2)); py++)
    for (int px = std::max(0, int(x - radius - 1)); px < std::min(480, int(x + radius + 2)); px++) {
      float distance = std::hypot(px - x, py - y);
      int coverage = std::lround(std::clamp(radius + 0.5f - distance, 0.f, 1.f) * 255);
      if (coverage)
        p[py * 480 + px] = blend(color, p[py * 480 + px], coverage);
    }
}
inline void rounded(uint16_t *p, int x, int y, int w, int h, int radius, uint16_t color) {
  for (int py = std::max(0, y); py < std::min(480, y + h); py++)
    for (int px = std::max(0, x); px < std::min(480, x + w); px++) {
      float dx = std::max(float(std::abs(px - (x + w / 2)) - (w / 2 - radius)), 0.f);
      float dy = std::max(float(std::abs(py - (y + h / 2)) - (h / 2 - radius)), 0.f);
      int a = std::lround(std::clamp(radius + 0.5f - std::hypot(dx, dy), 0.f, 1.f) * 255);
      if (a)
        p[py * 480 + px] = blend(color, p[py * 480 + px], a);
    }
}
inline void arc(uint16_t *p, float x, float y, float r, float start, float end, float width,
                uint16_t color) {
  int steps = std::max(1, int((end - start) * r / 2));
  for (int i = 0; i < steps; i++) {
    float a = start + (end - start) * i / steps, b = start + (end - start) * (i + 1) / steps;
    line(p, x + std::sin(a) * r, y - std::cos(a) * r, x + std::sin(b) * r, y - std::cos(b) * r,
         width, color);
  }
}
inline void button(uint16_t *p, const char *s, int x, int y, int w, uint16_t color) {
  rounded(p, x - w / 2, y, w, 56, 25, color);
  label(p, s, x, y + 13, smallFont, fg);
}
// Network names are external text: fit them to the round display. The bundled
// English font shows unsupported Unicode characters as '?' without changing SSID bytes.
inline void networkName(uint16_t *p, const char *name, int x, int y, int maxWidth, uint16_t color,
                        bool centered = true) {
  char text[40];
  int n = 0, width = 0;
  for (const unsigned char *c = (const unsigned char *)name; *c; c++) {
    if ((*c & 0xc0) == 0x80)
      continue;
    char ch = *c >= 32 && *c < 127 ? *c : '?';
    if (width + smallFont.glyphs[ch - 32].advance > maxWidth - 20) {
      text[n++] = '.';
      text[n++] = '.';
      break;
    }
    text[n++] = ch;
    width += smallFont.glyphs[ch - 32].advance;
  }
  text[n] = 0;
  label(p, text, x, y, smallFont, color, centered);
}
inline const char *keyboardRow(int mode, bool uppercase, int row) {
  const char *letters[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const char *capitals[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  const char *numbers[] = {"1234567890", "!@#$%^&*()", "-_=+[]{}"};
  const char *symbols[] = {"\\|;:'\",.<>", "/?`~", "-_=+[]{}"};
  return mode == 1   ? numbers[row]
         : mode == 2 ? symbols[row]
                     : (uppercase ? capitals[row] : letters[row]);
}
inline char keyboardCharacter(const WatchState &s, float x, float y) {
  for (int row = 0; row < 3; row++) {
    const char *keys = keyboardRow(s.keyboardMode, s.uppercase, row);
    int left = 240 - std::strlen(keys) * 20;
    if (y >= 190 + row * 48 && y < 232 + row * 48 && x >= left &&
        x < left + int(std::strlen(keys)) * 40)
      return keys[int(x - left) / 40];
  }
  return 0;
}
inline void heading(uint16_t *p, const char *title) {
  line(p, 116, 65, 104, 77, 3, cyan);
  line(p, 104, 77, 116, 89, 3, cyan);
  label(p, title, 250, 60, mediumFont, fg);
}
inline void toggle(uint16_t *p, int y, bool on) {
  rounded(p, 315, y, 58, 30, 15, on ? green : rgb(65, 75, 87));
  circle(p, on ? 358 : 330, y + 15, 11, fg);
}
inline void weatherIcon(uint16_t *p, int code, bool isDay, float x, float y, float scale) {
  if (code <= 2) {
    circle(p, x - 10 * scale, y - 8 * scale, 16 * scale, isDay ? orange : fg);
    if (isDay) {
      for (int i = 0; i < 8; i++) {
        float a = i * .78539816f;
        line(p, x + (std::sin(a) * 22 - 10) * scale, y + (std::cos(a) * 22 - 8) * scale,
             x + (std::sin(a) * 28 - 10) * scale, y + (std::cos(a) * 28 - 8) * scale, 3 * scale,
             orange);
      }
    } else
      circle(p, x - 2 * scale, y - 14 * scale, 14 * scale, bg);
    if (code == 0)
      return;
  }
  circle(p, x - 16 * scale, y + 5 * scale, 13 * scale, fg);
  circle(p, x, y - 2 * scale, 18 * scale, fg);
  circle(p, x + 18 * scale, y + 7 * scale, 12 * scale, fg);
  line(p, x - 16 * scale, y + 12 * scale, x + 18 * scale, y + 12 * scale, 12 * scale, fg);
  if (code >= 51) {
    for (int i = -1; i <= 1; i++) {
      float xx = x + i * 18 * scale;
      line(p, xx, y + 24 * scale, xx - 5 * scale, y + 34 * scale, 3 * scale, cyan);
      if ((code >= 71 && code <= 77) || code == 85 || code == 86)
        line(p, xx - 6 * scale, y + 26 * scale, xx + 2 * scale, y + 31 * scale, 2 * scale, fg);
    }
    if (code >= 95) {
      line(p, x + 6 * scale, y + 16 * scale, x - 3 * scale, y + 28 * scale, 5 * scale, orange);
      line(p, x - 3 * scale, y + 28 * scale, x + 8 * scale, y + 28 * scale, 5 * scale, orange);
    }
  } else if (code == 45 || code == 48)
    line(p, x - 25 * scale, y + 28 * scale, x + 25 * scale, y + 28 * scale, 3 * scale, dim);
}
inline void icon(uint16_t *p, Page page, int x, int y, uint16_t color, int radius = 43) {
  circle(p, x, y, radius, color);
  switch (page) {
  case Page::Weather:
    weatherIcon(p, 2, true, x + 3, y + 2, .65f);
    break;
  case Page::Face:
  case Page::Stopwatch:
  case Page::Timer:
    arc(p, x, y + 2, 23, 0, 6.2831853f, 3, fg);
    line(p, x, y + 2, x, y - 13, 3, fg);
    line(p, x, y + 2, x + 12, y + 10, 3, fg);
    if (page != Page::Face) {
      line(p, x - 7, y - 28, x + 7, y - 28, 3, fg);
      line(p, x, y - 28, x, y - 22, 3, fg);
    }
    break;
  case Page::Settings:
  case Page::DisplaySettings:
    for (int i = 0; i < 8; i++) {
      float a = i * 0.78539816f;
      line(p, x + std::sin(a) * 20, y - std::cos(a) * 20, x + std::sin(a) * 29,
           y - std::cos(a) * 29, 6, fg);
    }
    arc(p, x, y, 20, 0, 6.2831853f, 5, fg);
    circle(p, x, y, 7, color);
    break;
  case Page::Wifi:
    arc(p, x, y + 18, 38, -.72f, .72f, 4, fg);
    arc(p, x, y + 18, 26, -.72f, .72f, 4, fg);
    arc(p, x, y + 18, 14, -.72f, .72f, 4, fg);
    circle(p, x, y + 18, 3, fg);
    break;
  case Page::Sensors:
    line(p, x - 28, y + 5, x - 15, y + 5, 3, fg);
    line(p, x - 15, y + 5, x - 6, y - 20, 3, fg);
    line(p, x - 6, y - 20, x + 5, y + 23, 3, fg);
    line(p, x + 5, y + 23, x + 15, y - 5, 3, fg);
    line(p, x + 15, y - 5, x + 28, y - 5, 3, fg);
    break;
  case Page::Info:
    circle(p, x, y - 16, 3, fg);
    line(p, x, y - 3, x, y + 20, 5, fg);
    break;
  default:
    break;
  }
}
struct App {
  Page page;
  const char *name;
  int x, y;
  uint16_t color;
};
const App apps[] = {{Page::Face, "Clock", 175, 110, orange},
                    {Page::Weather, "Weather", 305, 110, rgb(25, 110, 165)},
                    {Page::Stopwatch, "Stopwatch", 130, 255, green},
                    {Page::Timer, "Timer", 350, 255, orange},
                    {Page::Settings, "Settings", 240, 390, rgb(95, 110, 128)}};
inline void draw(uint16_t *p, const WatchState &s) {
  std::fill(p, p + 480 * 480, bg);
  char text[80];
  if (s.page == Page::Face) {
    for (int i = 0; i < 60; i++) {
      float a = i * 6.2831853f / 60;
      int r = i % 5 ? 222 : 215;
      line(p, 239.5f + std::sin(a) * r, 239.5f - std::cos(a) * r, 239.5f + std::sin(a) * 229,
           239.5f - std::cos(a) * 229, i % 5 ? 1.5f : 3,
           s.clockValid && i <= s.second ? cyan : card);
    }
    if (s.timeSyncing)
      std::strcpy(text, "SYNCING TIME");
    else if (s.ntpSyncedAt && s.connected) {
      time_t local = s.ntpSyncedAt + 8 * 3600;
      struct tm t;
      gmtime_r(&local, &t);
      std::snprintf(text, sizeof(text), "SYNCED %02d:%02d", t.tm_hour, t.tm_min);
    } else
      std::strcpy(text, s.connected ? "ONLINE" : "RTC / OFFLINE");
    label(p, text, 240, 67, smallFont, s.connected ? green : dim);
    const char *days[] = {"SUNDAY",   "MONDAY", "TUESDAY", "WEDNESDAY",
                          "THURSDAY", "FRIDAY", "SATURDAY"};
    label(p, days[s.weekday % 7], 240, 122, mediumFont, cyan);
    int hour = s.format24 ? s.hour : ((s.hour + 11) % 12 + 1);
    if (s.clockValid)
      std::snprintf(text, sizeof(text), "%02d:%02d", hour, s.minute);
    else
      std::strcpy(text, "--:--");
    label(p, text, 240, 184, largeFont, fg);
    const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (s.clockValid)
      std::snprintf(text, sizeof(text), "%02d %s  /  %02d%s", s.day, months[s.month - 1], s.second,
                    s.format24 ? "" : (s.hour < 12 ? " AM" : " PM"));
    else
      std::strcpy(text, "Sync time in Wi-Fi");
    label(p, text, 240, 296, smallFont, dim);
    rounded(p, 137, 333, 206, 58, 26, card);
    if (s.weather.valid) {
      weatherIcon(p, s.weather.code, s.weather.isDay, 182, 360, .55f);
      std::snprintf(text, sizeof(text), "%.0f C", s.weather.temperature);
      label(p, text, 268, 345, mediumFont, fg);
    } else
      label(p, s.weatherLoading ? "Loading weather..." : "Weather --", 240, 351, smallFont, dim);
    label(p,
          s.weather.valid && (!s.connected || s.weather.error[0]) ? "Cached weather"
                                                                  : "Swipe up for apps",
          240, 405, smallFont, dim);
    return;
  }
  if (s.page == Page::Apps) {
    for (const auto &app : apps) {
      icon(p, app.page, app.x, app.y, app.color);
      label(p, app.name, app.x, app.y + 47, smallFont, fg);
    }
    return;
  }
  if (s.page == Page::Controls) {
    heading(p, "Quick settings");
    icon(p, Page::Wifi, 160, 172, s.wifiEnabled ? blue : card);
    label(p, "Wi-Fi", 160, 223, smallFont, fg);
    icon(p, Page::Face, 320, 172, s.rotate ? purple : card);
    label(p, "Rotate", 320, 223, smallFont, fg);
    circle(p, 160, 315, 43, orange);
    std::snprintf(text, sizeof(text), "%d%%", s.brightness);
    label(p, text, 160, 299, mediumFont, fg);
    label(p, "Brightness", 160, 366, smallFont, fg);
    circle(p, 320, 315, 43, card);
    arc(p, 320, 315, 19, .7f, 5.58f, 3, fg);
    line(p, 320, 290, 320, 311, 3, fg);
    label(p, "Screen off", 320, 366, smallFont, fg);
    return;
  }
  if (s.page == Page::Settings) {
    heading(p, "Settings");
    icon(p, Page::DisplaySettings, 160, 172, rgb(95, 110, 128));
    label(p, "Display & time", 160, 223, smallFont, fg);
    icon(p, Page::Wifi, 320, 172, blue);
    label(p, "Wi-Fi", 320, 223, smallFont, fg);
    icon(p, Page::Sensors, 160, 315, purple);
    label(p, "Sensors", 160, 366, smallFont, fg);
    icon(p, Page::Info, 320, 315, blue);
    label(p, "Device", 320, 366, smallFont, fg);
    return;
  }
  if (s.page == Page::DisplaySettings) {
    heading(p, "Display & time");
    rounded(p, 90, 120, 300, 76, 22, card);
    label(p, "Brightness", 108, 128, smallFont, fg, false);
    line(p, 120, 174, 360, 174, 5, dim);
    line(p, 120, 174, 120 + s.brightness * 2.4f, 174, 5, orange);
    circle(p, 120 + s.brightness * 2.4f, 174, 8, fg);
    rounded(p, 90, 210, 300, 55, 20, card);
    label(p, "Rotate face", 108, 224, smallFont, fg, false);
    toggle(p, 222, s.rotate);
    rounded(p, 90, 278, 300, 55, 20, card);
    label(p, "24-hour time", 108, 292, smallFont, fg, false);
    toggle(p, 290, s.format24);
    rounded(p, 107, 346, 266, 55, 20, card);
    label(p, "Sleep", 125, 360, smallFont, fg, false);
    if (s.sleepSeconds)
      std::snprintf(text, sizeof(text), "%ds", s.sleepSeconds);
    else
      std::strcpy(text, "Never");
    label(p, text, 334, 360, smallFont, cyan);
    return;
  }
  if (s.page == Page::Stopwatch || s.page == Page::Timer) {
    bool timer = s.page == Page::Timer;
    heading(p, timer ? "Timer" : "Stopwatch");
    uint32_t value = timer ? s.timerMs : s.stopwatchMs;
    std::snprintf(text, sizeof(text), "%02lu:%02lu", (unsigned long)(value / 60000),
                  (unsigned long)((value / 1000) % 60));
    label(p, text, 240, 153, largeFont, timer ? orange : green);
    if (timer) {
      if (s.timerDone)
        label(p, "TIME'S UP", 240, 267, mediumFont, red);
      else {
        int minutes[] = {1, 3, 5, 10};
        for (int i = 0; i < 4; i++) {
          int x = 135 + i * 70;
          circle(p, x, 289, 28, card);
          std::snprintf(text, sizeof(text), "%dm", minutes[i]);
          label(p, text, x, 275, smallFont, fg);
        }
      }
    } else {
      std::snprintf(text, sizeof(text), ".%02lu", (unsigned long)((value / 10) % 100));
      label(p, text, 240, 270, mediumFont, dim);
    }
    bool running = timer ? s.timerRunning : s.stopwatchRunning;
    button(p, running ? "Pause" : "Start", 160, 348, 130,
           timer ? rgb(160, 82, 15) : rgb(45, 110, 37));
    button(p, "Reset", 320, 348, 130, card);
    return;
  }
  if (s.page == Page::Sensors) {
    heading(p, "Motion");
    circle(p, 240, 183, 65, card);
    arc(p, 240, 183, 65, 0, 6.2831853f, 2, purple);
    line(p, 185, 183, 295, 183, 1, dim);
    line(p, 240, 128, 240, 238, 1, dim);
    circle(p, 240 + std::clamp(s.ay, -1.f, 1.f) * 48, 183 - std::clamp(s.ax, -1.f, 1.f) * 48, 10,
           purple);
    label(p, "Acceleration (g)", 240, 260, smallFont, dim);
    std::snprintf(text, sizeof(text), "X %+.2f  Y %+.2f  Z %+.2f", s.ax, s.ay, s.az);
    label(p, text, 240, 294, smallFont, fg);
    label(p, "Gyroscope (deg/s)", 240, 334, smallFont, dim);
    std::snprintf(text, sizeof(text), "%+.1f    %+.1f    %+.1f", s.gx, s.gy, s.gz);
    label(p, text, 240, 368, smallFont, purple);
    return;
  }
  if (s.page == Page::Info) {
    heading(p, "Device");
    label(p, "ESP32-S3", 240, 121, mediumFont, blue);
    std::snprintf(text, sizeof(text), "Dual core  /  %d MHz", s.cpuMHz);
    label(p, text, 240, 163, smallFont, dim);
    const char *names[] = {"Display",    "Flash / PSRAM", "Free memory",
                           "Free PSRAM", "Battery input", "Firmware"};
    char values[6][36];
    std::strcpy(values[0], "480 x 480");
    std::snprintf(values[1], 36, "%d / %d MB", s.flashMB, s.psramMB);
    std::snprintf(values[2], 36, "%lu KB", (unsigned long)(s.freeHeap / 1024));
    std::snprintf(values[3], 36, "%.1f MB", s.freePsram / 1048576.f);
    std::snprintf(values[4], 36, "%.2f V", s.battery);
    std::strcpy(values[5], "Round Lab 0.3");
    for (int i = 0; i < 6; i++) {
      int y = 211 + i * 31;
      label(p, names[i], 98, y, smallFont, dim, false);
      label(p, values[i], 315, y, smallFont, fg);
    }
    return;
  }
  if (s.page == Page::Weather) {
    heading(p, s.weatherView == WeatherView::Forecast ? "Next 5 days" : "Weather");
    if (s.weatherView == WeatherView::Cities) {
      label(p, "Choose city", 240, 112, smallFont, dim);
      for (int i = 0; i < 3; i++)
        button(p, weatherCities[i].name, 240, 160 + i * 80, 260,
               i == s.weatherCity ? rgb(20, 94, 148) : card);
      return;
    }
    label(p, weatherCities[s.weatherCity].name, 240, 105, smallFont, cyan);
    if (!s.weather.valid) {
      weatherIcon(p, 2, true, 240, 216, 1.3f);
      label(p, s.weatherLoading ? "Loading weather..." : "Weather unavailable", 240, 288,
            mediumFont, fg);
      label(p, s.connected ? s.weatherMessage : "Connect Wi-Fi to update", 240, 330, smallFont,
            dim);
    } else if (s.weatherView == WeatherView::Forecast) {
      const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      for (int i = 1; i <= 5; i++) {
        const auto &day = s.weather.days[i];
        time_t local = day.date + 8 * 3600;
        struct tm t;
        gmtime_r(&local, &t);
        int y = 142 + (i - 1) * 49;
        std::snprintf(text, sizeof(text), "%s %02d", days[t.tm_wday], t.tm_mday);
        label(p, text, 88, y, smallFont, fg, false);
        weatherIcon(p, day.code, true, 205, y + 11, .43f);
        std::snprintf(text, sizeof(text), "%.0f / %.0f", day.low, day.high);
        label(p, text, 282, y, smallFont, fg);
        std::snprintf(text, sizeof(text), "%d%%", day.rain);
        label(p, text, 366, y, smallFont, cyan);
      }
      label(p, "Low / high C    Rain chance", 240, 389, smallFont, dim);
      time_t local = s.weather.fetchedAt + 8 * 3600;
      struct tm t;
      gmtime_r(&local, &t);
      std::snprintf(text, sizeof(text), "%s %02d/%02d %02d:%02d",
                    !s.connected         ? "Offline"
                    : s.weatherLoading   ? "Updating"
                    : s.weather.error[0] ? "Update failed"
                                         : "Updated",
                    t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
      label(p, text, 240, 415, smallFont, dim);
    } else {
      weatherIcon(p, s.weather.code, s.weather.isDay, 147, 189, 1.2f);
      std::snprintf(text, sizeof(text), "%.0f", s.weather.temperature);
      label(p, text, 285, 147, largeFont, fg);
      label(p, "C", 359, 195, mediumFont, cyan);
      label(p, weatherDescription(s.weather.code), 240, 254, mediumFont, fg);
      std::snprintf(text, sizeof(text), "H %.0f / L %.0f C   Rain %d%%", s.weather.days[0].high,
                    s.weather.days[0].low, s.weather.days[0].rain);
      label(p, text, 240, 295, smallFont, dim);
      std::snprintf(text, sizeof(text), "Feels %.0f C   Humidity %d%%", s.weather.feelsLike,
                    s.weather.humidity);
      label(p, text, 240, 323, smallFont, dim);
    }
    if (s.weather.valid && s.weatherView == WeatherView::Now) {
      time_t local = s.weather.observedAt + 8 * 3600;
      struct tm t;
      gmtime_r(&local, &t);
      std::snprintf(text, sizeof(text), "%s  %02d/%02d %02d:%02d",
                    !s.connected         ? "Offline data"
                    : s.weatherLoading   ? "Updating"
                    : s.weather.error[0] ? "Update failed"
                                         : "As of",
                    t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
      label(p, text, 240, 348, smallFont, dim);
    }
    if (s.weatherView == WeatherView::Now) {
      button(p, "City", 150, 377, 110, card);
      button(p,
             s.weather.valid ? "5 days"
             : s.connected   ? "Retry"
                             : "Wi-Fi",
             300, 377, 150, rgb(20, 94, 148));
    }
    // A tap on the source/status footer refreshes; attribution stays visible offline too.
    label(p, "Open-Meteo.com", 240, 438, smallFont, dim);
    return;
  }
  if (s.page == Page::Wifi) {
    heading(p, s.wifiView == WifiView::Password ? (s.connectionFailed ? "Try again" : "Password")
                                                : "Wi-Fi");
    if (s.wifiView == WifiView::Networks) {
      if (s.scanning) {
        icon(p, Page::Wifi, 240, 210, blue);
        label(p, "Scanning...", 240, 280, mediumFont, fg);
      } else if (s.networks.empty()) {
        label(p, "No networks found", 240, 195, mediumFont, fg);
        label(p, s.wifiMessage, 240, 250, smallFont, dim);
      } else {
        std::snprintf(text, sizeof(text), "%d-%d of %d  /  Swipe to scroll", s.networkOffset + 1,
                      std::min(s.networkOffset + 3, int(s.networks.size())),
                      int(s.networks.size()));
        label(p, text, 240, 104, smallFont, dim);
        for (int row = 0; row < 3 && s.networkOffset + row < int(s.networks.size()); row++) {
          const auto &net = s.networks[s.networkOffset + row];
          int y = 140 + row * 74;
          rounded(p, 82, y, 316, 66, 18, card);
          networkName(p, net.ssid, 100, y + 7, 280, fg, false);
          std::snprintf(text, sizeof(text), "%s   %d dBm",
                        !net.supported ? "Unsupported"
                        : net.open     ? "Open"
                                       : "Password",
                        net.rssi);
          label(p, text, 100, y + 36, smallFont, net.supported ? dim : red, false);
        }
      }
      button(p, s.scanning ? "Scanning" : "Rescan", 240, 385, 176, card);
    } else if (s.wifiView == WifiView::Password) {
      networkName(p, s.selectedSSID, 240, 106, 290, cyan);
      int length = std::strlen(s.password);
      rounded(p, 92, 140, 224, 37, 12, card);
      if (!length)
        std::strcpy(text, s.selectedOpen ? "Open network" : "8-63 characters");
      else if (s.showPassword)
        std::snprintf(text, sizeof(text), "%s", s.password + std::max(0, length - 16));
      else {
        std::memset(text, '*', std::min(length, 16));
        text[std::min(length, 16)] = 0;
      }
      networkName(p, text, 204, 147, 210, length ? fg : dim);
      label(p, s.showPassword ? "Hide" : "Show", 350, 147, smallFont, cyan);
      for (int row = 0; row < 3; row++) {
        const char *keys = keyboardRow(s.keyboardMode, s.uppercase, row);
        int left = 240 - std::strlen(keys) * 20;
        for (int i = 0; keys[i]; i++) {
          rounded(p, left + i * 40 + 2, 190 + row * 48, 36, 42, 9, card);
          char key[] = {keys[i], 0};
          label(p, key, left + i * 40 + 20, 199 + row * 48, smallFont, fg);
        }
      }
      const char *controls[] = {"Shift",
                                s.keyboardMode == 0   ? "123"
                                : s.keyboardMode == 1 ? "#+="
                                                      : "ABC",
                                "Space", "Del"};
      const int centers[] = {100, 180, 265, 355}, widths[] = {72, 70, 86, 70};
      for (int i = 0; i < 4; i++) {
        rounded(p, centers[i] - widths[i] / 2, 334, widths[i], 42, 12,
                i == 0 && s.uppercase ? rgb(20, 94, 148) : card);
        label(p, controls[i], centers[i], 344, smallFont, fg);
      }
      button(p, "Connect", 240, 390, 180, s.selectedOpen || length >= 8 ? rgb(20, 94, 148) : card);
    } else {
      icon(p, Page::Wifi, 240, 151, s.connected ? blue : card);
      label(p,
            s.connected     ? "Connected"
            : s.connecting  ? "Connecting..."
            : s.wifiEnabled ? "Not connected"
                            : "Wi-Fi off",
            240, 206, mediumFont, fg);
      networkName(p, s.connected ? s.network : s.selectedSSID, 240, 247, 300, cyan);
      label(p, s.connected ? s.ip : s.wifiMessage, 240, 278, smallFont, dim);
      button(p, s.connecting ? "Cancel" : "Scan networks", 240, 317, 246, rgb(20, 94, 148));
      button(p, s.wifiEnabled ? "Turn off" : "Turn on", 240, 385, 190, card);
    }
  }
}
} // namespace watchui
