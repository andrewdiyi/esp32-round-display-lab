#pragma once
#include "Weather.h"
#include <Arduino.h>
#include <cmath>
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

// Validate numbers once where the external JSON enters the application.
inline bool weatherNumber(const cJSON *object, const char *name, double &value, int index = -1) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (index >= 0)
    item = cJSON_GetArrayItem(item, index);
  if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble))
    return false;
  value = item->valuedouble;
  return true;
}
inline bool parseWeather(const char *body, WeatherData &data) {
  cJSON *root = cJSON_Parse(body);
  if (!root)
    return false;
  const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
  const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
  double temperature, feels, humidity, code, isDay, observed;
  bool ok = weatherNumber(current, "temperature_2m", temperature) &&
            weatherNumber(current, "apparent_temperature", feels) &&
            weatherNumber(current, "relative_humidity_2m", humidity) &&
            weatherNumber(current, "weather_code", code) &&
            weatherNumber(current, "is_day", isDay) && weatherNumber(current, "time", observed);
  if (ok) {
    data.temperature = temperature;
    data.feelsLike = feels;
    data.humidity = humidity;
    data.code = code;
    data.isDay = isDay != 0;
    data.observedAt = observed;
  }
  for (int i = 0; ok && i < 6; i++) {
    double date, low, high, rain;
    ok = weatherNumber(daily, "time", date, i) &&
         weatherNumber(daily, "temperature_2m_min", low, i) &&
         weatherNumber(daily, "temperature_2m_max", high, i) &&
         weatherNumber(daily, "weather_code", code, i) &&
         weatherNumber(daily, "precipitation_probability_max", rain, i);
    if (ok)
      data.days[i] = {time_t(date), float(low), float(high), int(code), int(rain)};
  }
  cJSON_Delete(root);
  return ok;
}
inline WeatherData fetchWeather(int city) {
  WeatherData data;
  data.city = city;
  char url[512];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&timezone=Asia%%2FSingapore&timeformat=unixtime&forecast_days=6",
           weatherCities[city].latitude, weatherCities[city].longitude);
  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = 10000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.user_agent = "RoundLabWatch/0.3";
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    strcpy(data.error, "HTTPS initialization failed");
    return data;
  }
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    snprintf(data.error, sizeof(data.error), "HTTPS: %s", esp_err_to_name(err));
  } else if (esp_http_client_fetch_headers(client) < 0) {
    strcpy(data.error, "Weather server timed out");
  } else if (esp_http_client_get_status_code(client) != 200) {
    snprintf(data.error, sizeof(data.error), "Weather HTTP %d",
             esp_http_client_get_status_code(client));
  } else {
    // Current conditions plus six daily entries fit comfortably within 8 KiB.
    char *body = (char *)malloc(8192);
    if (!body)
      strcpy(data.error, "Not enough memory for weather");
    else {
      int length = esp_http_client_read_response(client, body, 8191);
      if (length > 0 && length < 8191) {
        body[length] = 0;
        data.valid = parseWeather(body, data);
      }
      if (data.valid)
        data.fetchedAt = time(nullptr);
      else
        strcpy(data.error, "Invalid weather response");
      free(body);
    }
  }
  esp_http_client_cleanup(client);
  return data;
}
