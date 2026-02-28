#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  char description[32]; // e.g., "Clear", "Rain"
  int temp;             // Current temperature
  int feels_like;       // Feels like temperature
  int wind_speed;       // Wind speed in km/h
  int humidity;         // Humidity in %
  char icon[8];         // Weather icon code
  bool is_valid;        // True if data was successfully fetched
} weather_info_t;

typedef struct {
  int year, month, day;
  int hour, minute, second;
  int dow; // Day of week (0-6)
  bool is_synced;
} time_info_t;
