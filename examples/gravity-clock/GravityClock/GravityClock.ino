#include <Arduino.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include "Display_ST7701.h"
#include "Orientation.h"
#include "ClockFace.h"

Orientation orientation;
Preferences settings;
Vec3 acceleration, rotationRate;
uint8_t imuAddress = 0x6b;
bool imuOK = false, clockOK = false, stream = false;
uint32_t imuErrors = 0, frames = 0, lastSample = 0, lastFrame = 0, lastLog = 0;
float lastAngle = 999, frameMs = 0;
time_t drawnSecond = -1;
uint16_t *canvas = nullptr, *framebuffers[2] = {nullptr, nullptr}, *lastPixels = nullptr;
int nextBuffer = 1;
SemaphoreHandle_t vsyncSignal;
String command;

bool IRAM_ATTR clockVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  BaseType_t wake = pdFALSE;
  xSemaphoreGiveFromISR(vsyncSignal, &wake);
  return wake == pdTRUE;
}
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
  drawnSecond = -1;
  if (wrote && restarted)
    settings.putBool("rtcUTC", true);
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
void status() {
  Serial.printf("{\"firmware\":\"gravity-clock-1\",\"epoch\":%lld,\"clock_valid\":%s,\"imu_ok\":%s,"
                "\"accel\":[%.4f,%.4f,%.4f],\"gyro\":[%.3f,%.3f,%.3f],\"angle_deg\":%.2f,\"raw_"
                "deg\":%.2f,\"offset_deg\":%.2f,\"handedness\":%.0f,\"tracking\":%s,\"frames\":%lu,"
                "\"render_ms\":%.1f,\"imu_errors\":%lu,\"psram_free\":%u}\n",
                (long long)time(nullptr), clockOK ? "true" : "false", imuOK ? "true" : "false",
                acceleration.x, acceleration.y, acceleration.z, rotationRate.x, rotationRate.y,
                rotationRate.z, orientation.angle * RAD_TO_DEG, orientation.rawAngle() * RAD_TO_DEG,
                orientation.offset * RAD_TO_DEG, orientation.handedness,
                orientation.tracking ? "true" : "false", (unsigned long)frames, frameMs,
                (unsigned long)imuErrors, ESP.getFreePsram());
}
void saveOrientation() {
  settings.putFloat("offset", orientation.offset);
  settings.putFloat("hand", orientation.handedness);
  lastAngle = 999;
}
void handleCommand(String s) {
  s.trim();
  if (s == "STATUS")
    status();
  else if (s.startsWith("TIME ")) {
    bool ok = setClock(strtoll(s.c_str() + 5, nullptr, 10));
    Serial.println(ok ? "OK TIME RTC" : "ERROR RTC WRITE");
  } else if (s == "ZERO") {
    if (!orientation.initialized || !orientation.tracking) {
      Serial.println("ERROR HOLD SCREEN VERTICAL");
      return;
    }
    orientation.offset = wrapAngle(-orientation.rawAngle());
    saveOrientation();
    Serial.println("OK ZERO");
  } else if (s.startsWith("OFFSET ")) {
    float d = s.substring(7).toFloat();
    if (!isfinite(d)) {
      Serial.println("ERROR OFFSET");
      return;
    }
    orientation.offset = wrapAngle(d * DEG_TO_RAD);
    saveOrientation();
    Serial.println("OK OFFSET");
  } else if (s == "HAND 1" || s == "HAND -1") {
    orientation.handedness = s.endsWith("-1") ? -1 : 1;
    saveOrientation();
    Serial.println("OK HAND");
  } else if (s == "STREAM 1" || s == "STREAM 0") {
    stream = s.endsWith("1");
    Serial.println("OK STREAM");
  } else if (s.startsWith("BRIGHTNESS ")) {
    int n = s.substring(11).toInt();
    if (n >= 5 && n <= 100) {
      Set_Backlight(n);
      settings.putUInt("light", n);
      Serial.println("OK BRIGHTNESS");
    } else
      Serial.println("ERROR BRIGHTNESS");
  } else if (s == "CAPTURE" && lastPixels) {
    Serial.println("RGB565 480 480 460800");
    Serial.write((uint8_t *)lastPixels, 480 * 480 * 2);
    Serial.println("\nEND CAPTURE");
    lastSample = micros();
  } else if (s.length())
    Serial.println("ERROR UNKNOWN COMMAND");
}
void setup() {
  Serial.begin(115200);
  command.reserve(100);
  setenv("TZ", "UTC0", 1);
  tzset();
  settings.begin("gravityclock", false);
  // Rotation mapping verified on the physical board: reverse the sensor
  // polar angle and add 90 degrees to match the display coordinates.
  orientation.offset = settings.getFloat("offset", PI / 2);
  orientation.handedness = settings.getFloat("hand", -1);
  I2C_Init();
  TCA9554PWR_Init(0);
  Set_EXIO(EXIO_PIN8, Low);
  time_t epoch;
  // Do not interpret the old demo's local RTC fields as UTC on first install.
  if (settings.getBool("rtcUTC", false) && readRTC(epoch)) {
    struct timeval tv = {epoch, 0};
    settimeofday(&tv, nullptr);
    clockOK = true;
  }
  initIMU();
  canvas = (uint16_t *)heap_caps_malloc(480 * 480 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!canvas) {
    Serial.println("FATAL CANVAS ALLOCATION");
    while (true)
      delay(1000);
  }
  LCD_Init();
  Set_Backlight(settings.getUInt("light", 55));
  void *fb0 = nullptr, *fb1 = nullptr;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb0, &fb1));
  framebuffers[0] = (uint16_t *)fb0;
  framebuffers[1] = (uint16_t *)fb1;
  vsyncSignal = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_vsync = clockVsync;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &callbacks, nullptr));
  lastSample = micros();
  Serial.println("READY GRAVITY CLOCK");
  status();
}
void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      handleCommand(command);
      command = "";
    } else if (c != '\r') {
      if (command.length() < 100)
        command += c;
      else
        command = "";
    }
  }
  sampleIMU();
  uint32_t now = millis();
  time_t epoch = time(nullptr);
  if (now - lastFrame >= 40 &&
      (drawnSecond != epoch || fabsf(wrapAngle(orientation.angle - lastAngle)) > 0.006f ||
       lastAngle == 999)) {
    uint32_t start = micros();
    if (drawnSecond != epoch) {
      time_t local = epoch + 8 * 3600;
      struct tm t;
      gmtime_r(&local, &t);
      face::draw(canvas, t.tm_hour, t.tm_min, t.tm_sec, t.tm_mon + 1, t.tm_mday, t.tm_wday,
                 clockOK);
      drawnSecond = epoch;
    }
    face::rotate(canvas, framebuffers[nextBuffer], orientation.angle);
    xSemaphoreTake(vsyncSignal, 0);
    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 480, 480, framebuffers[nextBuffer]));
    if (xSemaphoreTake(vsyncSignal, pdMS_TO_TICKS(100)) != pdTRUE)
      Serial.println("WARN VSYNC TIMEOUT");
    lastPixels = framebuffers[nextBuffer];
    nextBuffer = 1 - nextBuffer;
    lastAngle = orientation.angle;
    lastFrame = millis();
    frames++;
    frameMs = (micros() - start) / 1000.f;
  }
  if (stream && now - lastLog >= 200) {
    status();
    lastLog = now;
  }
  delay(1);
}
