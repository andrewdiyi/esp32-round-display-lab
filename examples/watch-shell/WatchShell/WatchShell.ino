#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include "Display_ST7701.h"
#include "Orientation.h"
#include "WatchUi.h"
#include "WeatherClient.h"

WatchState ui;
Page weatherReturnPage = Page::Apps;
Preferences settings;
Orientation orientation;
Vec3 acceleration, rotationRate;
uint8_t imuAddress = 0x6b;
bool imuOK = false, clockOK = false, touchOK = false, dirty = true, asleep = false;
uint32_t imuErrors = 0, touchErrors = 0, lastSample = 0, lastInteraction = 0, lastFrame = 0,
         lastPoll = 0;
uint32_t stopwatchBase = 0, stopwatchStarted = 0, timerDeadline = 0, timerSelected = 180000;
uint32_t connectStarted = 0, lastSensorFrame = 0, buzzerUntil = 0;
bool saveNetwork = false;
time_t ntpSyncedAt = 0;
QueueHandle_t weatherResults;
uint32_t weatherLastAttempt = 0;
volatile bool ntpPending = false, touchIRQ = false;
volatile uint8_t wifiDisconnectReason = 0;
bool touching = false, wakeTouch = false, longTouch = false, brightnessDrag = false;
float startX = 0, startY = 0, touchX = 0, touchY = 0, touchAngle = 0, displayAngle = 0,
      menuAngle = 0;
uint32_t touchStarted = 0, lastTouchEvent = 0, touchEvents = 0;
int lastGesture = 0;
float rasterMs = 0;
time_t shownSecond = -1;
uint16_t *canvas = nullptr, *framebuffers[2] = {nullptr, nullptr}, *lastPixels = nullptr;
face::RotationCache rotationCache;
SemaphoreHandle_t vsyncSignal;
int nextBuffer = 1;
String command;

bool IRAM_ATTR watchVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  BaseType_t wake = pdFALSE;
  xSemaphoreGiveFromISR(vsyncSignal, &wake);
  return wake == pdTRUE;
}
void IRAM_ATTR touchInterrupt() { touchIRQ = true; }
void timeSynchronized(struct timeval *) { ntpPending = true; }
void requestTimeSync() {
  if (!ui.connected)
    return;
  sntp_set_time_sync_notification_cb(timeSynchronized);
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  sntp_set_sync_interval(3600000);
  ui.timeSyncing = true;
  dirty = true;
}
void weatherTask(void *argument) {
  WeatherData result = fetchWeather(int(intptr_t(argument)));
  xQueueSend(weatherResults, &result, portMAX_DELAY);
  vTaskDelete(nullptr);
}
void requestWeather() {
  if (ui.weatherLoading)
    return;
  if (!ui.connected || !ntpSyncedAt) {
    strcpy(ui.weatherMessage,
           ui.connected ? "Waiting for network time" : "Connect Wi-Fi for weather");
    dirty = true;
    return;
  }
  weatherLastAttempt = millis();
  ui.weatherLoading = true;
  strcpy(ui.weatherMessage, "Fetching forecast");
  if (xTaskCreatePinnedToCore(weatherTask, "weather", 8192, (void *)intptr_t(ui.weatherCity), 1,
                              nullptr, 0) != pdPASS) {
    ui.weatherLoading = false;
    strcpy(ui.weatherMessage, "Could not start weather request");
  }
  dirty = true;
}
void weatherLoop() {
  WeatherData result;
  if (xQueueReceive(weatherResults, &result, 0) == pdTRUE) {
    ui.weatherLoading = false;
    if (result.city == ui.weatherCity) {
      if (result.valid) {
        ui.weather = result;
        ui.weatherMessage[0] = 0;
      } else {
        strlcpy(ui.weatherMessage, result.error, sizeof(ui.weatherMessage));
        strlcpy(ui.weather.error, result.error, sizeof(ui.weather.error));
      }
      dirty = true;
    }
  }
  if (ui.connected && ntpSyncedAt && !ui.weatherLoading &&
      (!weatherLastAttempt || millis() - weatherLastAttempt >= 30 * 60 * 1000UL))
    requestWeather();
}

// Hardware register and clock functions are kept beside this standalone sketch.
bool readReg(uint8_t addr, uint8_t reg, uint8_t *data, size_t n) {
  return !I2C_Read(addr, reg, data, n);
}
bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) { return !I2C_Write(addr, reg, &value, 1); }
int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }
uint8_t toBcd(int v) { return (v / 10) * 16 + v % 10; }
bool readRTC(time_t &epoch) {
  uint8_t b[7];
  if (!readReg(0x51, 4, b, 7) || (b[0] & 0x80))
    return false;
  struct tm t = {};
  t.tm_sec = bcd(b[0] & 0x7f);
  t.tm_min = bcd(b[1] & 0x7f);
  t.tm_hour = bcd(b[2] & 0x3f);
  t.tm_mday = bcd(b[3] & 0x3f);
  t.tm_mon = bcd(b[5] & 0x1f) - 1;
  t.tm_year = bcd(b[6]) + 100;
  if (t.tm_year < 126 || t.tm_year > 199 || t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 ||
      t.tm_mday > 31 || t.tm_hour > 23 || t.tm_min > 59 || t.tm_sec > 59)
    return false;
  // Runtime timezone is UTC; the view adds UTC+8 explicitly.
  epoch = mktime(&t);
  return true;
}
bool setClock(time_t epoch) {
  if (epoch < 1767225600LL || epoch > 4102444799LL)
    return false;
  struct tm t;
  gmtime_r(&epoch, &t);
  uint8_t b[] = {toBcd(t.tm_sec),  toBcd(t.tm_min),     toBcd(t.tm_hour),      toBcd(t.tm_mday),
                 toBcd(t.tm_wday), toBcd(t.tm_mon + 1), toBcd(t.tm_year - 100)};
  // Stop, atomically write all fields, restart: never leave STOP set on error.
  bool stopped = writeReg(0x51, 0, 0x21);
  bool wrote = stopped && !I2C_Write(0x51, 4, b, 7);
  bool restarted = writeReg(0x51, 0, 0x01);
  struct timeval tv = {epoch, 0};
  settimeofday(&tv, nullptr);
  clockOK = true;
  dirty = true;
  return wrote && restarted;
}
void initIMU() {
  uint8_t id = 0, ctrl = 0;
  if (!readReg(imuAddress, 0, &id, 1) || id != 5) {
    imuAddress = 0x6a;
    if (!readReg(imuAddress, 0, &id, 1) || id != 5)
      return;
  }
  if (!readReg(imuAddress, 2, &ctrl, 1))
    return;
  imuOK = writeReg(imuAddress, 8, 0) && writeReg(imuAddress, 2, (ctrl | 0x40) & 0xfe) &&
          writeReg(imuAddress, 3, 0x16)    // +/-4g, 125 Hz
          && writeReg(imuAddress, 4, 0x56) // +/-512 degrees/s, 125 Hz
          && writeReg(imuAddress, 6, 0x11) // low-pass on both sensors
          && writeReg(imuAddress, 7, 0) && writeReg(imuAddress, 8, 0x43);
}
void sampleIMU() {
  uint32_t now = micros();
  if (uint32_t(now - lastSample) < 10000)
    return;
  float dt = (now - lastSample) * 1e-6f;
  lastSample = now;
  if (!imuOK)
    return;
  uint8_t b[12];
  if (!readReg(imuAddress, 0x35, b, 12)) {
    imuErrors++;
    return;
  }
  int16_t v[6];
  for (int i = 0; i < 6; i++)
    v[i] = int16_t(uint16_t(b[i * 2]) | (uint16_t(b[i * 2 + 1]) << 8));
  acceleration = {v[0] / 8192.f, v[1] / 8192.f, v[2] / 8192.f};
  rotationRate = {v[3] / 64.f, v[4] / 64.f, v[5] / 64.f};
  orientation.update(acceleration, rotationRate, dt);
}

const char *pageName(Page page) {
  const char *names[] = {"face",    "apps", "controls", "settings", "stopwatch", "timer",
                         "sensors", "info", "wifi",     "weather",  "display"};
  return names[int(page)];
}
void setPage(Page page) {
  if (page == Page::Weather && ui.page != Page::Weather)
    weatherReturnPage = ui.page;
  if (ui.page == Page::Face && page != Page::Face)
    menuAngle = ui.rotate ? std::round(orientation.angle / (PI / 2)) * (PI / 2) : 0;
  ui.page = page;
  dirty = true;
  asleep = false;
  Set_Backlight(ui.brightness);
  lastInteraction = millis();
  if (page == Page::Wifi && !ui.connected && !ui.connecting)
    scanWifi();
  if (page == Page::Weather &&
      (!ui.weather.valid || millis() - weatherLastAttempt >= 30 * 60 * 1000UL))
    requestWeather();
  if (page != Page::Wifi && !ui.connecting) {
    memset(ui.password, 0, sizeof(ui.password));
    ui.showPassword = false;
  }
}
void back() {
  if (ui.page == Page::Weather && ui.weatherView != WeatherView::Now) {
    ui.weatherView = WeatherView::Now;
    dirty = true;
    return;
  }
  if (ui.page == Page::Wifi && ui.wifiView != WifiView::Status) {
    ui.wifiView = ui.wifiView == WifiView::Password ? WifiView::Networks : WifiView::Status;
    memset(ui.password, 0, sizeof(ui.password));
    ui.showPassword = false;
    dirty = true;
    return;
  }
  if (ui.page == Page::Weather)
    setPage(weatherReturnPage);
  else if (ui.page == Page::Sensors || ui.page == Page::Wifi || ui.page == Page::Info ||
           ui.page == Page::DisplaySettings)
    setPage(Page::Settings);
  else
    setPage(ui.page == Page::Apps || ui.page == Page::Controls ? Page::Face : Page::Apps);
}
void screenOff() {
  asleep = true;
  Set_Backlight(0);
}
void saveSettings() {
  settings.putUInt("brightness", ui.brightness);
  settings.putUInt("sleep", ui.sleepSeconds);
  settings.putBool("rotate", ui.rotate);
  settings.putBool("format24", ui.format24);
  // Flash writes suspend PSRAM access in this Arduino build. Resync RGB DMA
  // after committing settings so the panel cannot retain a displaced frame.
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(panel_handle));
}
void joinNetwork(bool saveOnSuccess) {
  WiFi.setAutoReconnect(!saveOnSuccess);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.setMinSecurity(ui.selectedOpen ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK);
  WiFi.begin(ui.selectedSSID, ui.password);
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(panel_handle));
  ui.connected = false;
  ui.connecting = true;
  ui.connectionFailed = false;
  ui.wifiView = WifiView::Status;
  saveNetwork = saveOnSuccess;
  connectStarted = millis();
  strcpy(ui.wifiMessage, "Joining selected network");
  dirty = true;
}
void startConnection() {
  String ssid = settings.getString("ssid", "");
  if (!ssid.length())
    return;
  strlcpy(ui.selectedSSID, ssid.c_str(), sizeof(ui.selectedSSID));
  strlcpy(ui.password, settings.getString("password", "").c_str(), sizeof(ui.password));
  ui.selectedOpen = !ui.password[0];
  joinNetwork(false);
}
void scanWifi() {
  if (ui.scanning)
    return;
  if (!ui.connected) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    ui.connecting = false;
    saveNetwork = false;
  }
  if (!ui.wifiEnabled) {
    ui.wifiEnabled = true;
    settings.putBool("wifi", true);
  }
  WiFi.mode(WIFI_STA);
  ui.wifiView = WifiView::Networks;
  ui.networks.clear();
  ui.networkOffset = 0;
  WiFi.scanDelete();
  ui.scanning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
  strcpy(ui.wifiMessage, ui.scanning ? "Scanning..." : "Scan failed. Tap Rescan");
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(panel_handle));
  dirty = true;
}
void toggleWifi() {
  ui.wifiEnabled = !ui.wifiEnabled;
  settings.putBool("wifi", ui.wifiEnabled);
  if (ui.wifiEnabled)
    scanWifi();
  else {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    WiFi.scanDelete();
    ui.connected = ui.connecting = ui.scanning = false;
    ui.wifiView = WifiView::Status;
    memset(ui.password, 0, sizeof(ui.password));
    saveNetwork = false;
    ui.timeSyncing = false;
  }
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(panel_handle));
  dirty = true;
}
void networkLoop() {
  if (ui.scanning) {
    int count = WiFi.scanComplete();
    if (count != WIFI_SCAN_RUNNING) {
      ui.scanning = false;
      for (int i = 0; i < count; i++) {
        WifiNetwork net = {};
        strlcpy(net.ssid, WiFi.SSID(i).c_str(), sizeof(net.ssid));
        if (!net.ssid[0])
          continue;
        net.rssi = WiFi.RSSI(i);
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        net.open = auth == WIFI_AUTH_OPEN;
        net.supported = net.open || auth == WIFI_AUTH_WPA_PSK || auth == WIFI_AUTH_WPA2_PSK ||
                        auth == WIFI_AUTH_WPA_WPA2_PSK || auth == WIFI_AUTH_WPA3_PSK ||
                        auth == WIFI_AUTH_WPA2_WPA3_PSK;
        auto duplicate =
            std::find_if(ui.networks.begin(), ui.networks.end(),
                         [&](const WifiNetwork &n) { return !strcmp(n.ssid, net.ssid); });
        if (duplicate == ui.networks.end())
          ui.networks.push_back(net);
        else if (net.rssi > duplicate->rssi)
          *duplicate = net;
      }
      std::sort(ui.networks.begin(), ui.networks.end(),
                [](const WifiNetwork &a, const WifiNetwork &b) { return a.rssi > b.rssi; });
      WiFi.scanDelete();
      strcpy(ui.wifiMessage, count < 0 ? "Scan failed. Tap Rescan" : "Use a 2.4 GHz network");
      dirty = true;
    }
  }
  bool connected = ui.wifiEnabled && WiFi.status() == WL_CONNECTED;
  if (connected != ui.connected) {
    ui.connected = connected;
    dirty = true;
    if (connected) {
      ui.connecting = false;
      ui.connectionFailed = false;
      strlcpy(ui.ip, WiFi.localIP().toString().c_str(), sizeof(ui.ip));
      strlcpy(ui.network, WiFi.SSID().c_str(), sizeof(ui.network));
      if (saveNetwork) {
        settings.putString("ssid", ui.selectedSSID);
        settings.putString("password", ui.password);
        ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(panel_handle));
        saveNetwork = false;
      }
      memset(ui.password, 0, sizeof(ui.password));
      WiFi.setAutoReconnect(true);
      requestTimeSync();
    } else if (!ui.connecting)
      strcpy(ui.wifiMessage, "Disconnected. Reconnecting...");
  }
  if (ui.connecting && millis() - connectStarted > 20000) {
    ui.connecting = false;
    if (saveNetwork) {
      WiFi.disconnect();
      ui.connectionFailed = true;
      strcpy(ui.wifiMessage, "Connection failed. Try again");
      ui.wifiView = WifiView::Password;
    } else
      strcpy(ui.wifiMessage, "Retrying saved network...");
    saveNetwork = false;
    dirty = true;
  }
  if (ntpPending) {
    ntpPending = false;
    ntpSyncedAt = time(nullptr);
    setClock(ntpSyncedAt);
    ui.ntpSyncedAt = ntpSyncedAt;
    ui.timeSyncing = false;
    dirty = true;
  }
}
void tap(float x, float y) {
  if (ui.page != Page::Face && ui.page != Page::Apps && x < 145 && y < 110) {
    back();
    return;
  }
  switch (ui.page) {
  case Page::Face:
    if (y >= 55 && y < 105 && x >= 140 && x < 340) {
      if (ui.connected)
        requestTimeSync();
      else
        setPage(Page::Wifi);
    } else if (x >= 130 && x < 350 && y >= 330 && y < 395) {
      ui.weatherView = WeatherView::Now;
      setPage(Page::Weather);
    } else
      setPage(Page::Apps);
    break;
  case Page::Apps:
    for (const auto &app : watchui::apps)
      if (std::hypot(x - app.x, y - app.y) < 58) {
        setPage(app.page);
        break;
      }
    break;
  case Page::Controls:
    if (std::hypot(x - 160, y - 172) < 65)
      setPage(Page::Wifi);
    else if (std::hypot(x - 320, y - 172) < 65) {
      ui.rotate = !ui.rotate;
      saveSettings();
      dirty = true;
    } else if (std::hypot(x - 160, y - 315) < 65) {
      ui.brightness = ui.brightness >= 100 ? 25 : std::min(100, ui.brightness + 25);
      Set_Backlight(ui.brightness);
      saveSettings();
      dirty = true;
    } else if (std::hypot(x - 320, y - 315) < 65)
      screenOff();
    break;
  case Page::Settings:
    if (std::hypot(x - 160, y - 172) < 65)
      setPage(Page::DisplaySettings);
    else if (std::hypot(x - 320, y - 172) < 65)
      setPage(Page::Wifi);
    else if (std::hypot(x - 160, y - 315) < 65)
      setPage(Page::Sensors);
    else if (std::hypot(x - 320, y - 315) < 65)
      setPage(Page::Info);
    break;
  case Page::DisplaySettings:
    if (y >= 120 && y < 200) {
      ui.brightness = std::clamp(int((x - 120) / 2.4f), 5, 100);
      Set_Backlight(ui.brightness);
    } else if (y >= 210 && y < 270)
      ui.rotate = !ui.rotate;
    else if (y >= 278 && y < 338)
      ui.format24 = !ui.format24;
    else if (y >= 346 && y < 410)
      ui.sleepSeconds = ui.sleepSeconds == 15   ? 30
                        : ui.sleepSeconds == 30 ? 60
                        : ui.sleepSeconds == 60 ? 0
                                                : 15;
    saveSettings();
    dirty = true;
    break;
  case Page::Stopwatch:
    if (y >= 340 && y <= 415) {
      if (x < 240) {
        if (ui.stopwatchRunning) {
          stopwatchBase = ui.stopwatchMs;
          ui.stopwatchRunning = false;
        } else {
          stopwatchStarted = millis();
          ui.stopwatchRunning = true;
        }
      } else {
        ui.stopwatchRunning = false;
        stopwatchBase = 0;
        ui.stopwatchMs = 0;
      }
      dirty = true;
    }
    break;
  case Page::Timer:
    if (y >= 255 && y < 325 && !ui.timerRunning) {
      int index = std::clamp(int((x - 100) / 70), 0, 3);
      int minutes[] = {1, 3, 5, 10};
      timerSelected = minutes[index] * 60000;
      ui.timerMs = timerSelected;
      ui.timerDone = false;
      dirty = true;
    }
    if (y >= 340 && y <= 415) {
      if (x < 240) {
        if (ui.timerRunning)
          ui.timerRunning = false;
        else {
          if (ui.timerMs == 0)
            ui.timerMs = timerSelected;
          timerDeadline = millis() + ui.timerMs;
          ui.timerRunning = true;
          ui.timerDone = false;
        }
      } else {
        ui.timerRunning = false;
        ui.timerDone = false;
        ui.timerMs = timerSelected;
      }
      dirty = true;
    }
    break;
  case Page::Wifi:
    if (ui.wifiView == WifiView::Networks) {
      if (ui.scanning)
        break;
      if (y >= 385 && y < 441 && x >= 152 && x <= 328) {
        scanWifi();
        break;
      }
      int row = int((y - 140) / 74);
      int index = ui.networkOffset + row;
      if (x >= 82 && x < 398 && y >= 140 && y < 354 && row < 3 && index < int(ui.networks.size())) {
        const auto &net = ui.networks[index];
        if (!net.supported)
          break;
        strlcpy(ui.selectedSSID, net.ssid, sizeof(ui.selectedSSID));
        ui.selectedOpen = net.open;
        memset(ui.password, 0, sizeof(ui.password));
        if (settings.getString("ssid", "") == ui.selectedSSID)
          strlcpy(ui.password, settings.getString("password", "").c_str(), sizeof(ui.password));
        ui.keyboardMode = 0;
        ui.uppercase = ui.showPassword = ui.connectionFailed = false;
        ui.wifiView = WifiView::Password;
        if (net.open)
          joinNetwork(true);
        dirty = true;
      }
    } else if (ui.wifiView == WifiView::Password) {
      size_t length = strlen(ui.password);
      char key = watchui::keyboardCharacter(ui, x, y);
      if (y >= 140 && y < 180 && x >= 320 && x < 390)
        ui.showPassword = !ui.showPassword;
      else if (key && length < sizeof(ui.password) - 1) {
        ui.password[length] = key;
        ui.password[length + 1] = 0;
      } else if (y >= 334 && y < 376) {
        if (x >= 64 && x < 136)
          ui.uppercase = !ui.uppercase;
        else if (x >= 145 && x < 215)
          ui.keyboardMode = (ui.keyboardMode + 1) % 3;
        else if (x >= 222 && x < 308 && length < sizeof(ui.password) - 1) {
          ui.password[length] = ' ';
          ui.password[length + 1] = 0;
        } else if (x >= 320 && x < 390 && length)
          ui.password[length - 1] = 0;
      } else if (x >= 150 && x < 330 && y >= 390 && y < 446 && (ui.selectedOpen || length >= 8))
        joinNetwork(true);
      dirty = true;
    } else if (y >= 317 && y < 373) {
      if (ui.connecting) {
        WiFi.disconnect();
        ui.connecting = false;
        saveNetwork = false;
        ui.wifiView = WifiView::Password;
        dirty = true;
      } else
        scanWifi();
    } else if (y >= 385 && y < 441)
      toggleWifi();
    break;
  case Page::Weather:
    if (ui.weatherView == WeatherView::Cities) {
      for (int i = 0; i < 3; i++) {
        if (x >= 110 && x < 370 && y >= 160 + i * 80 && y < 216 + i * 80) {
          ui.weatherCity = i;
          settings.putUInt("weatherCity", i);
          ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(panel_handle));
          ui.weather = WeatherData{};
          ui.weatherView = WeatherView::Now;
          weatherLastAttempt = 0;
          requestWeather();
          dirty = true;
          break;
        }
      }
    } else if (y >= 433 && x >= 140 && x < 340)
      requestWeather();
    else if (ui.weatherView == WeatherView::Now && y >= 377 && y < 433) {
      if (x >= 95 && x < 205)
        ui.weatherView = WeatherView::Cities;
      else if (x >= 225 && x < 375) {
        if (ui.weather.valid)
          ui.weatherView = WeatherView::Forecast;
        else if (ui.connected)
          requestWeather();
        else
          setPage(Page::Wifi);
      }
      dirty = true;
    }
    break;
  default:
    break;
  }
}
void swipe(float dx, float dy) {
  if (ui.page == Page::Weather && std::abs(dy) > std::abs(dx)) {
    if (dy < 0 && ui.weather.valid)
      ui.weatherView = WeatherView::Forecast;
    else if (dy > 0)
      requestWeather();
    dirty = true;
    return;
  }
  if (ui.page == Page::Wifi && ui.wifiView == WifiView::Networks && std::abs(dy) > std::abs(dx)) {
    int last = std::max(0, (int(ui.networks.size()) - 1) / 3 * 3);
    ui.networkOffset = std::clamp(ui.networkOffset + (dy < 0 ? 3 : -3), 0, last);
    dirty = true;
    return;
  }
  if (std::abs(dx) > std::abs(dy)) {
    if (dx > 0)
      back();
    else if (ui.page == Page::Face)
      setPage(Page::Weather);
  } else if (ui.page == Page::Face) {
    setPage(dy < 0 ? Page::Apps : Page::Controls);
  } else if (ui.page == Page::Apps && dy > 0)
    setPage(Page::Face);
}
void touchCoordinates(int x, int y, float &logicalX, float &logicalY, float angle) {
  float dx = x - 239.5f, dy = y - 239.5f, c = std::cos(angle), s = std::sin(angle);
  logicalX = 239.5f + c * dx + s * dy;
  logicalY = 239.5f - s * dx + c * dy;
}
void initTouch() {
  pinMode(16, INPUT_PULLUP);
  Set_EXIO(EXIO_PIN2, Low);
  delay(10);
  Set_EXIO(EXIO_PIN2, High);
  delay(50);
  uint8_t id = 0;
  touchOK = writeReg(0x15, 0xfe, 0xff) && readReg(0x15, 0xa7, &id, 1);
  attachInterrupt(digitalPinToInterrupt(16), touchInterrupt, FALLING);
}
void touchLoop() {
  uint32_t now = millis();
  bool irq = touchIRQ;
  if (!irq && now - lastPoll < 12)
    return;
  touchIRQ = false;
  lastPoll = now;
  uint8_t data[6];
  if (!readReg(0x15, 1, data, 6))
    return;
  int points = data[1] & 15, gesture = data[0];
  int rawX = ((data[2] & 15) << 8) | data[3], rawY = ((data[4] & 15) << 8) | data[5];
  // Reject invalid controller packets (observed as all 0xff), not a touch/release.
  if (points > 1 || (points == 1 && (rawX >= 480 || rawY >= 480))) {
    touchErrors++;
    return;
  }
  if (points) {
    if (!touching) {
      touching = true;
      touchStarted = now;
      touchAngle = displayAngle;
      longTouch = false;
      touchCoordinates(rawX, rawY, startX, startY, touchAngle);
      touchX = startX;
      touchY = startY;
      wakeTouch = asleep;
      if (asleep) {
        asleep = false;
        Set_Backlight(ui.brightness);
        dirty = true;
      }
      brightnessDrag =
          !wakeTouch && ui.page == Page::DisplaySettings && startY >= 120 && startY < 200;
    }
    touchCoordinates(rawX, rawY, touchX, touchY, touchAngle);
    lastInteraction = now;
    if (brightnessDrag) {
      int value = std::clamp(int((touchX - 120) / 2.4f), 5, 100);
      if (value != ui.brightness) {
        ui.brightness = value;
        Set_Backlight(value);
        dirty = true;
      }
    }
    if (!wakeTouch && !brightnessDrag && !longTouch &&
        !(ui.page == Page::Wifi && ui.wifiView == WifiView::Password) && now - touchStarted > 700 &&
        std::hypot(touchX - startX, touchY - startY) < 25) {
      longTouch = true;
      setPage(Page::Face);
    }
  } else if (touching) {
    touching = false;
    lastTouchEvent = now;
    touchEvents++;
    lastInteraction = now;
    if (brightnessDrag)
      saveSettings();
    else if (!wakeTouch && !longTouch) {
      float dx = touchX - startX, dy = touchY - startY;
      if (std::hypot(dx, dy) > 45)
        swipe(dx, dy);
      else
        tap(touchX, touchY);
    }
    brightnessDrag = false;
  } else if (irq && gesture && now - lastTouchEvent > 220) {
    // CST820 retains a completed hardware gesture when a short contact occurs
    // entirely during a framebuffer draw.
    lastTouchEvent = now;
    lastInteraction = now;
    touchEvents++;
    lastGesture = gesture;
    if (asleep) {
      asleep = false;
      Set_Backlight(ui.brightness);
      dirty = true;
      return;
    }
    touchCoordinates(rawX, rawY, touchX, touchY, displayAngle);
    if (gesture == 5)
      tap(touchX, touchY);
    else if (gesture == 0x0c) {
      if (ui.page == Page::Wifi && ui.wifiView == WifiView::Password)
        tap(touchX, touchY);
      else
        setPage(Page::Face);
    } else if (gesture >= 1 && gesture <= 4) {
      float dx = gesture == 3   ? -100
                 : gesture == 4 ? 100
                                : 0,
            dy = gesture == 1   ? -100
                 : gesture == 2 ? 100
                                : 0;
      float c = std::cos(displayAngle), s = std::sin(displayAngle);
      swipe(c * dx + s * dy, -s * dx + c * dy);
    }
  }
}
void status() {
  Serial.printf(
      "{\"firmware\":\"round-lab-watch-0.3\",\"page\":\"%s\",\"awake\":%s,\"clock_valid\":%s,"
      "\"epoch\":%lld,\"touch_ok\":%s,\"touch_events\":%lu,\"touch_errors\":%lu,\"touch\":[%.0f,%."
      "0f],\"imu_ok\":%s,"
      "\"imu_errors\":%lu,\"angle_deg\":%.1f,\"raster_ms\":%.1f,\"wifi_enabled\":%s,\"wifi_"
      "connected\":%s,\"wifi_view\":%d,\"scanning\":%s,\"network_count\":%u,\"connecting\":%s,"
      "\"wifi_status\":%d,\"wifi_disconnect_reason\":%u,\"saved_password_length\":%u,"
      "\"ntp_synced_at\":%lld,\"time_syncing\":%s,\"weather_valid\":%s,\"weather_loading\":%s,"
      "\"weather_city\":\"%s\",\"weather_observed_at\":%lld,\"weather_fetched_at\":%lld,"
      "\"weather_temperature\":%.1f,\"weather_error\":\"%s\",\"brightness\":%d,\"heap\":%u,"
      "\"psram\":%u}\n",
      pageName(ui.page), asleep ? "false" : "true", clockOK ? "true" : "false",
      (long long)time(nullptr), touchOK ? "true" : "false", (unsigned long)touchEvents,
      (unsigned long)touchErrors, touchX, touchY, imuOK ? "true" : "false",
      (unsigned long)imuErrors, displayAngle * RAD_TO_DEG, rasterMs,
      ui.wifiEnabled ? "true" : "false", ui.connected ? "true" : "false", int(ui.wifiView),
      ui.scanning ? "true" : "false", unsigned(ui.networks.size()),
      ui.connecting ? "true" : "false", int(WiFi.status()), unsigned(wifiDisconnectReason),
      unsigned(settings.getString("password", "").length()), (long long)ntpSyncedAt,
      ui.timeSyncing ? "true" : "false", ui.weather.valid ? "true" : "false",
      ui.weatherLoading ? "true" : "false", weatherCities[ui.weatherCity].name,
      (long long)ui.weather.observedAt, (long long)ui.weather.fetchedAt, ui.weather.temperature,
      ui.weather.error, ui.brightness, ESP.getFreeHeap(), ESP.getFreePsram());
}
void serialLoop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r')
      continue;
    if (c != '\n') {
      if (command.length() < 120)
        command += c;
      continue;
    }
    command.trim();
    if (command == "STATUS")
      status();
    else if (command == "RECONNECT") {
      startConnection();
      Serial.println("OK RECONNECT");
    } else if (command == "NTP") {
      if (ui.connected) {
        requestTimeSync();
        Serial.println("OK NTP REQUESTED");
      } else
        Serial.println("ERROR WIFI DISCONNECTED");
    } else if (command.startsWith("TIME ")) {
      Serial.println(setClock(strtoll(command.c_str() + 5, nullptr, 10)) ? "OK TIME RTC"
                                                                         : "ERROR RTC WRITE");
      dirty = true;
    } else if (command.startsWith("PAGE ")) {
      for (int i = 0; i <= int(Page::DisplaySettings); i++)
        if (command.substring(5) == pageName(Page(i))) {
          setPage(Page(i));
          Serial.println("OK PAGE");
          break;
        }
    } else if (command == "CAPTURE" && lastPixels) {
      Serial.println("RGB565 480 480 460800");
      Serial.write((uint8_t *)lastPixels, 480 * 480 * 2);
      Serial.println("\nEND CAPTURE");
      lastSample = micros();
    } else if (command == "WAKE") {
      asleep = false;
      Set_Backlight(ui.brightness);
      lastInteraction = millis();
      dirty = true;
      Serial.println("OK WAKE");
    } else
      Serial.println("ERROR UNKNOWN COMMAND");
    command = "";
  }
}
void setup() {
  Serial.setTxBufferSize(2048); // Keep the complete diagnostic JSON in one USB transmit buffer.
  Serial.begin(115200);
  command.reserve(120);
  setenv("TZ", "UTC0", 1);
  tzset();
  settings.begin("watchlab", false);
  ui.brightness = settings.getUInt("brightness", 55);
  ui.sleepSeconds = settings.getUInt("sleep", 30);
  ui.rotate = settings.getBool("rotate", true);
  ui.format24 = settings.getBool("format24", true);
  ui.wifiEnabled = settings.getBool("wifi", false);
  ui.weatherCity = settings.getUInt("weatherCity", 0);
  weatherResults = xQueueCreate(1, sizeof(WeatherData));
  assert(weatherResults);
  orientation.offset = PI / 2;
  orientation.handedness = -1;
  I2C_Init();
  TCA9554PWR_Init(0);
  Set_EXIO(EXIO_PIN8, Low);
  time_t epoch;
  if (readRTC(epoch)) {
    struct timeval tv = {epoch, 0};
    settimeofday(&tv, nullptr);
    clockOK = true;
  }
  initIMU();
  analogReadResolution(12);
  canvas = (uint16_t *)heap_caps_malloc(480 * 480 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!canvas) {
    Serial.println("FATAL CANVAS ALLOCATION");
    while (true)
      delay(1000);
  }
  LCD_Init();
  // 8 MHz leaves PSRAM bandwidth for Wi-Fi and rendering (~29 panel scans/s).
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_set_pclk(panel_handle, 8000000));
  Set_Backlight(ui.brightness);
  initTouch();
  void *fb0 = nullptr, *fb1 = nullptr;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb0, &fb1));
  framebuffers[0] = (uint16_t *)fb0;
  framebuffers[1] = (uint16_t *)fb1;
  vsyncSignal = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_vsync = watchVsync;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &callbacks, nullptr));
  ui.flashMB = ESP.getFlashChipSize() / 1048576;
  ui.psramMB = ESP.getPsramSize() / 1048576;
  ui.cpuMHz = getCpuFrequencyMhz();
  WiFi.persistent(false);
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        wifiDisconnectReason = info.wifi_sta_disconnected.reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.setHostname("round-lab");
  WiFi.setAutoReconnect(true);
  if (ui.wifiEnabled) {
    WiFi.mode(WIFI_STA);
    startConnection();
  } else
    WiFi.mode(WIFI_OFF);
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_restart(panel_handle));
  lastSample = micros();
  lastInteraction = millis();
  Serial.println("READY ROUND LAB WATCH");
  status();
}
void loop() {
  serialLoop();
  touchLoop();
  sampleIMU();
  networkLoop();
  weatherLoop();
  uint32_t now = millis();
  if (ui.stopwatchRunning)
    ui.stopwatchMs = stopwatchBase + now - stopwatchStarted;
  if (ui.timerRunning) {
    int32_t remaining = int32_t(timerDeadline - now);
    if (remaining <= 0) {
      ui.timerMs = 0;
      ui.timerRunning = false;
      ui.timerDone = true;
      setPage(Page::Timer);
      Set_EXIO(EXIO_PIN8, High);
      buzzerUntil = now + 250;
    } else
      ui.timerMs = remaining;
  }
  if (buzzerUntil && int32_t(now - buzzerUntil) >= 0) {
    Set_EXIO(EXIO_PIN8, Low);
    buzzerUntil = 0;
  }
  if (!asleep && ui.sleepSeconds && now - lastInteraction > uint32_t(ui.sleepSeconds * 1000) &&
      !(ui.page == Page::Wifi && (ui.wifiView == WifiView::Password || ui.connecting)))
    screenOff();
  time_t epoch = time(nullptr);
  if (epoch != shownSecond) {
    time_t local = epoch + 8 * 3600;
    struct tm t;
    gmtime_r(&local, &t);
    ui.hour = t.tm_hour;
    ui.minute = t.tm_min;
    ui.second = t.tm_sec;
    ui.day = t.tm_mday;
    ui.month = t.tm_mon + 1;
    ui.weekday = t.tm_wday;
    ui.clockValid = clockOK;
    ui.freeHeap = ESP.getFreeHeap();
    ui.freePsram = ESP.getFreePsram();
    ui.uptime = now / 1000;
    ui.battery = analogReadMilliVolts(4) * 3.f / 1000.f / .992857f;
    if (ui.page == Page::Face || ui.page == Page::Info)
      dirty = true;
    shownSecond = epoch;
  }
  if (now - lastSensorFrame >= 150 &&
      (ui.page == Page::Sensors || ui.page == Page::Stopwatch || ui.page == Page::Timer)) {
    ui.ax = acceleration.x;
    ui.ay = acceleration.y;
    ui.az = acceleration.z;
    ui.gx = rotationRate.x;
    ui.gy = rotationRate.y;
    ui.gz = rotationRate.z;
    dirty = true;
    lastSensorFrame = now;
  }
  float angle = ui.page == Page::Face ? (ui.rotate ? orientation.angle : 0) : menuAngle;
  if (touching)
    angle = displayAngle;
  if (!asleep && now - lastFrame >= 40 &&
      (dirty || fabsf(wrapAngle(angle - displayAngle)) > .012f)) {
    uint32_t started = micros();
    if (dirty) {
      watchui::draw(canvas, ui);
      rotationCache.prepare(canvas);
      dirty = false;
    }
    face::rotate(canvas, framebuffers[nextBuffer], angle, &rotationCache);
    rasterMs = (micros() - started) / 1000.f;
    xSemaphoreTake(vsyncSignal, 0);
    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 480, 480, framebuffers[nextBuffer]));
    xSemaphoreTake(vsyncSignal, pdMS_TO_TICKS(100));
    lastPixels = framebuffers[nextBuffer];
    nextBuffer = 1 - nextBuffer;
    displayAngle = angle;
    lastFrame = millis();
  }
  delay(1);
}
