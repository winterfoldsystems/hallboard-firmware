// Host stand-in for ESPHome's JSON helper. hb_parse.h uses one overload,
// esphome::json::parse_json(const std::string &), which returns the root JsonDocument and an
// unbound (null) document when the body does not parse. ArduinoJson itself is the real thing,
// vendored under third_party/ at the version the device build uses.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#define ARDUINOJSON_ENABLE_STD_STRING 1  // NOLINT
#define ARDUINOJSON_USE_LONG_LONG 1      // NOLINT

#include <ArduinoJson.h>

namespace esphome::json {

inline JsonDocument parse_json(const uint8_t *data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len) != DeserializationError::Ok) return JsonDocument();
  return doc;
}

inline JsonDocument parse_json(const std::string &data) {
  return parse_json(reinterpret_cast<const uint8_t *>(data.c_str()), data.size());
}

}  // namespace esphome::json
