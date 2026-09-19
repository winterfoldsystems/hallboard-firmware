// Host stand-in for esphome::ESPTime. Only the parts hb_ui.h touches are here: the calendar
// fields, is_valid() and strftime() returning a std::string. The field meanings and the validity
// rule are copied from ESPHome so a face renders on the same inputs as the board's.
#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

namespace esphome {

inline uint8_t days_in_month(uint8_t month, uint16_t year) {
  static const uint8_t DAYS[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) return 29;
  return (month >= 1 && month <= 12) ? DAYS[month] : 0;
}

struct ESPTime {
  uint8_t second = 0;
  uint8_t minute = 0;
  uint8_t hour = 0;
  uint8_t day_of_week = 0;   // sunday = 1
  uint8_t day_of_month = 0;
  uint16_t day_of_year = 0;
  uint8_t month = 0;
  uint16_t year = 0;
  bool is_dst = false;
  time_t timestamp = 0;

  std::string strftime(const char *format) const {
    struct tm c = this->to_c_tm();
    char buf[128];
    size_t n = ::strftime(buf, sizeof buf, format, &c);
    return n == 0 ? std::string("ERROR") : std::string(buf, n);
  }
  std::string strftime(const std::string &format) const { return this->strftime(format.c_str()); }

  bool is_valid(bool check_day_of_week = true, bool check_day_of_year = true) const {
    return this->year >= 2019 && this->fields_in_range(check_day_of_week, check_day_of_year);
  }
  bool fields_in_range(bool check_day_of_week = true, bool check_day_of_year = true) const {
    bool valid = this->second < 61 && this->minute < 60 && this->hour < 24 && this->month > 0 &&
                 this->month < 13 && this->day_of_month > 0 &&
                 this->day_of_month <= days_in_month(this->month, this->year);
    if (check_day_of_week) valid = valid && this->day_of_week > 0 && this->day_of_week < 8;
    if (check_day_of_year) valid = valid && this->day_of_year > 0 && this->day_of_year < 367;
    return valid;
  }

  struct tm to_c_tm() const {
    struct tm c = {};
    c.tm_sec = this->second;
    c.tm_min = this->minute;
    c.tm_hour = this->hour;
    c.tm_wday = this->day_of_week - 1;
    c.tm_mday = this->day_of_month;
    c.tm_yday = this->day_of_year - 1;
    c.tm_mon = this->month - 1;
    c.tm_year = this->year - 1900;
    c.tm_isdst = this->is_dst ? 1 : 0;
    return c;
  }
  static ESPTime from_c_tm(struct tm *c, time_t ts) {
    ESPTime t;
    t.second = (uint8_t) c->tm_sec;
    t.minute = (uint8_t) c->tm_min;
    t.hour = (uint8_t) c->tm_hour;
    t.day_of_week = (uint8_t) (c->tm_wday + 1);
    t.day_of_month = (uint8_t) c->tm_mday;
    t.day_of_year = (uint16_t) (c->tm_yday + 1);
    t.month = (uint8_t) (c->tm_mon + 1);
    t.year = (uint16_t) (c->tm_year + 1900);
    t.is_dst = c->tm_isdst > 0;
    t.timestamp = ts;
    return t;
  }
  // The process TZ is what makes this local, so main.cpp sets it before calling this.
  static ESPTime from_epoch_local(time_t epoch) {
    struct tm local = {};
    localtime_r(&epoch, &local);
    return from_c_tm(&local, epoch);
  }
};

}  // namespace esphome
