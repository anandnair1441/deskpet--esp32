#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

extern Adafruit_SSD1306 display;
extern unsigned long now;
extern bool ntpSynced;
extern unsigned long lastNtpAttempt;


extern float temperature;
extern float feelsLike;
extern int humidity;
extern float windSpeed;
extern int windDeg;
extern int scrollX;
extern String weatherDesc;
extern String weatherIcon;
extern String city;
extern String countryCode;
extern String apiKey;

struct ForecastDay {
  String dayName;
  int temp;
  String icon;
};
extern ForecastDay fcast[3];

void fetchWeather();
void drawWeather();
void drawForcast();
String getWindDir(int deg);