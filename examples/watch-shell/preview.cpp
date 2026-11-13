#include "WatchShell/WatchUi.h"
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  WatchState state;
  state.clockValid = true;
  state.hour = 10;
  state.minute = 9;
  state.second = 38;
  state.month = 11;
  state.day = 13;
  state.weekday = 5;
  state.freeHeap = 190000;
  state.freePsram = 6800000;
  state.battery = 4.08f;
  state.ax = .1f;
  state.ay = -.3f;
  state.az = .95f;
  std::vector<uint16_t> pixels(480 * 480);
  const char *names[] = {"face",      "apps",          "controls",      "settings",
                         "stopwatch", "timer",         "sensors",       "info",
                         "wifi",      "wifi-networks", "wifi-keyboard", "wifi-symbols"};
  for (int i = 0; i < 12; i++) {
    state.page = i < 9 ? Page(i) : Page::Wifi;
    if (i == 9) {
      state.wifiView = WifiView::Networks;
      state.networks = {{"Home Wi-Fi", -42, false, true},
                        {"Guest network", -61, true, true},
                        {"A very long network name example", -75, false, true}};
    } else if (i >= 10) {
      state.wifiView = WifiView::Password;
      std::strcpy(state.selectedSSID, "Home Wi-Fi");
      std::strcpy(state.password, "Preview123!");
      state.keyboardMode = i == 11 ? 2 : 0;
    }
    watchui::draw(pixels.data(), state);
    std::ofstream file(std::string(argv[1]) + "/" + names[i] + ".ppm", std::ios::binary);
    file << "P6\n480 480\n255\n";
    for (int y = 0; y < 480; y++)
      for (int x = 0; x < 480; x++) {
        uint16_t value = std::hypot(x - 239.5f, y - 239.5f) < 240 ? pixels[y * 480 + x] : 0;
        char rgb[] = {char(((value >> 11) & 31) * 255 / 31), char(((value >> 5) & 63) * 255 / 63),
                      char((value & 31) * 255 / 31)};
        file.write(rgb, 3);
      }
  }
}
