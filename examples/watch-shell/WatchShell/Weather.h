#pragma once
#include <cstdint>
#include <ctime>

enum class WeatherView { Now, Forecast, Cities };
struct WeatherCity {
  const char *name;
  float latitude, longitude;
};
constexpr WeatherCity weatherCities[] = {{"Singapore", 1.3521f, 103.8198f},
                                         {"Shenzhen", 22.5431f, 114.0579f},
                                         {"Beijing", 39.9042f, 116.4074f}};
struct ForecastDay {
  time_t date = 0;
  float low = 0, high = 0;
  int code = 0, rain = 0;
};
struct WeatherData {
  bool valid = false, isDay = true;
  int city = 0, code = 0, humidity = 0;
  float temperature = 0, feelsLike = 0;
  time_t observedAt = 0, fetchedAt = 0;
  ForecastDay days[6];
  char error[48] = "";
};
inline const char *weatherDescription(int code) {
  switch (code) {
  case 0:
    return "Clear";
  case 1:
    return "Mostly clear";
  case 2:
    return "Partly cloudy";
  case 3:
    return "Overcast";
  case 45:
  case 48:
    return "Fog";
  case 51:
  case 53:
  case 55:
    return "Drizzle";
  case 56:
  case 57:
    return "Freezing drizzle";
  case 61:
  case 63:
  case 65:
    return "Rain";
  case 66:
  case 67:
    return "Freezing rain";
  case 71:
  case 73:
  case 75:
  case 77:
    return "Snow";
  case 80:
  case 81:
  case 82:
    return "Rain showers";
  case 85:
  case 86:
    return "Snow showers";
  case 95:
  case 96:
  case 99:
    return "Thunderstorms";
  default:
    return "Unknown weather";
  }
}
