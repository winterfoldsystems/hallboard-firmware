// Pure logic for the standalone train board: JSON parsing, station search, time helpers.
// Mirrors trainboard.py. Included by trainboard_wifi.yaml; glue to LVGL lives in the YAML lambdas.
#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"
#include "stations.h"
#ifdef USE_WIFI
#include <esp_wifi.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace tb {

struct Row { std::string time, dest, plat, status, colour, uid, expected;
             std::string orig_sched;   // arrivals boards: booked departure from the origin (HH:MM)
             time_t orig_sched_epoch = 0; };
struct Board { std::string header; std::vector<Row> rows; bool degraded = false; };

// Last fetched board per selection and when it was fetched (kept here so lambdas can use them
// without ESPHome globals, which are declared before this header is included).
inline Board g_cache[2];
inline std::string g_stamp[2];
// Arrivals boards: actual departure time from the origin, looked up per train (one /service call,
// cached for the day) once the booked departure has passed. checked = millis() of the last lookup.
#include <map>
inline std::map<std::string, std::string> g_origin_actual;
inline std::map<std::string, uint32_t> g_origin_checked;

// Shared calendar (from the calendar-proxy worker): one item per event occurrence, sorted.
struct CalItem { std::string d, w, t, u, s, l; bool all_day = false; };
inline std::vector<CalItem> g_cal;
inline std::string g_cal_name;

// ---------------------------------------------------------------- CRS codes packed into int (restorable globals)
inline int pack_code(const std::string &c) {
  if (c.empty()) return 0;
  int v = 0;
  for (int i = 0; i < 3; i++) {
    char ch = i < (int) c.size() ? toupper((unsigned char) c[i]) : 'A';
    v = v * 27 + ((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 1 : 0);
  }
  return v;
}
inline std::string unpack_code(int v) {
  if (v <= 0) return "";
  char out[4];
  for (int i = 2; i >= 0; i--) { int d = v % 27; v /= 27; out[i] = d ? 'A' + d - 1 : '?'; }
  out[3] = 0;
  return out;
}

inline std::string upper(std::string s) { for (auto &c : s) c = toupper((unsigned char) c); return s; }
inline std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

inline std::string station_name(const std::string &code) {
  for (int i = 0; i < STATION_COUNT; i++) if (code == STATIONS[i].code) return STATIONS[i].name;
  return code;
}

// Indices into STATIONS[] ranked for a partial name or code.
inline std::vector<int> search_stations(const std::string &q_in, int limit) {
  std::string q = trim(upper(q_in));
  std::vector<int> exact, starts, words, contains;
  if (q.empty()) return exact;
  for (int i = 0; i < STATION_COUNT; i++) {
    std::string n = upper(STATIONS[i].name);
    const char *code = STATIONS[i].code;
    if (q == code) { exact.push_back(i); continue; }
    if (n.rfind(q, 0) == 0 || strncmp(code, q.c_str(), q.size()) == 0) { starts.push_back(i); continue; }
    bool word = false;
    size_t p = 0;
    while (p < n.size()) {
      size_t e = n.find_first_of(" (", p);
      if (e == std::string::npos) e = n.size();
      if (e > p && n.compare(p, q.size(), q) == 0) { word = true; break; }
      p = e + 1;
    }
    if (word) words.push_back(i);
    else if (n.find(q) != std::string::npos) contains.push_back(i);
  }
  std::vector<int> out;
  for (auto *v : {&exact, &starts, &words, &contains})
    for (int i : *v) if ((int) out.size() < limit) out.push_back(i);
  return out;
}

// Board-style abbreviations, applied only until the name fits.
inline std::string shorten(std::string name, size_t limit) {
  static const char *AB[][2] = {
    {"London ", ""}, {"International", "Intl"}, {"Harbour", "Hbr"}, {"Terminal", "T"}, {"Junction", "Jn"},
    {"Parkway", "Pkwy"}, {"Central", "Ctrl"}, {"Street", "St"}, {"Road", "Rd"}, {"Bridge", "Br"},
    {"Airport", "Apt"}, {"Temple Meads", "TM"}, {"Piccadilly", "Picc"}, {"Victoria", "Vic"}, {"Cross", "X"},
    {" Bus", " (bus)"}, {"South Western Railway", "SWR"}, {"Great Western Railway", "GWR"},
    {"Southern", "SN"}, {"Thameslink", "TL"}};
  if (name.size() <= limit) return name;
  for (auto &ab : AB) {
    size_t p;
    bool hit = false;
    while ((p = name.find(ab[0])) != std::string::npos) { name.replace(p, strlen(ab[0]), ab[1]); hit = true; }
    if (hit) { name = trim(name); if (name.size() <= limit) return name; }
  }
  return name;
}

// ---------------------------------------------------------------- ISO 8601 helpers
inline long days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned) (y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long) doe - 719468;
}

// Epoch seconds for an ISO datetime. Naive strings (no zone, which is what RTT sends: local UK
// time) are treated as UTC unless naive_is_local is set, in which case the device TZ applies.
// now_utc must then be the current epoch from ESPHome's time component: the C library clock
// (time()) is never set on this build, but localtime_r() does honour the configured TZ.
inline time_t iso_epoch(const char *s, bool *has_offset = nullptr, bool naive_is_local = false, time_t now_utc = 0) {
  int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 5) return 0;
  const char *z = strlen(s) > 16 ? strpbrk(s + 16, "Z+-") : nullptr;
  if (has_offset) *has_offset = z != nullptr;
  if (!z && naive_is_local) {
    // mktime() ignores the TZ here, but localtime_r() honours it, so take the current UTC offset
    // from the difference between local and UTC wall clocks (DST edge minutes are negligible).
    struct tm lt, gt;
    localtime_r(&now_utc, &lt);
    gmtime_r(&now_utc, &gt);
    long off = (lt.tm_hour - gt.tm_hour) * 3600L + (lt.tm_min - gt.tm_min) * 60L;
    if (off > 12 * 3600L) off -= 86400L;
    if (off < -12 * 3600L) off += 86400L;
    return (time_t) (days_from_civil(Y, M, D) * 86400L + h * 3600L + m * 60 + sec - off);
  }
  long off = 0;
  if (z && *z != 'Z') { int oh = 0, om = 0; sscanf(z + 1, "%d:%d", &oh, &om); off = (oh * 3600L + om * 60) * (*z == '-' ? -1 : 1); }
  return (time_t) (days_from_civil(Y, M, D) * 86400L + h * 3600L + m * 60 + sec - off);
}

// Local HH:MM for an ISO datetime (uses the TZ set by the time component).
inline std::string iso_hhmm(const char *s) {
  if (!s || strlen(s) < 16) return "";
  bool has_off = false;
  time_t e = iso_epoch(s, &has_off);
  if (!has_off) return std::string(s + 11, 5);
  struct tm lt;
  localtime_r(&e, &lt);
  char b[8];
  strftime(b, sizeof b, "%H:%M", &lt);
  return b;
}

// ---------------------------------------------------------------- parsers
inline bool parse_token(const std::string &body, std::string &token, std::string &valid_until) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  const char *t = doc["token"].as<const char *>();
  if (!t) return false;
  token = t;
  const char *v = doc["validUntil"].as<const char *>();
  valid_until = v ? v : "";
  return true;
}

inline std::string dest_names(JsonArray dests) {
  std::string out;
  for (JsonObject d : dests) {
    const char *n = d["location"]["description"].as<const char *>();
    if (!out.empty()) out += " & ";
    out += n ? n : "?";
  }
  return out.empty() ? "?" : out;
}

inline bool parse_board(const std::string &body, const std::string &from, const std::string &to, Board &out) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  const char *ln = root["query"]["location"]["description"].as<const char *>();
  out.header = shorten(ln ? ln : from, 14);
  if (!to.empty()) out.header += "  to " + shorten(station_name(to), 14);
  const char *core = root["systemStatus"]["rttCore"].as<const char *>();
  const char *nr = root["systemStatus"]["realtimeNetworkRail"].as<const char *>();
  out.degraded = (core && strcmp(core, "OK")) || (nr && strcmp(nr, "OK"));

  struct Tmp { std::string key; Row r; };
  std::vector<Tmp> tmp;
  for (JsonObject svc : root["services"].as<JsonArray>()) {
    JsonObject td = svc["temporalData"], dep = td["departure"], sm = svc["scheduleMetadata"], lm = svc["locationMetadata"];
    const char *display = td["displayAs"].as<const char *>();
    const char *booked = dep["scheduleAdvertised"].as<const char *>();
    if (!booked || !display || !strcmp(display, "PASS")) continue;
    if (sm["inPassengerService"].is<bool>() && !sm["inPassengerService"].as<bool>()) continue;
    if (dep["realtimeActual"].as<const char *>()) continue;  // already gone
    Row r;
    r.time = iso_hhmm(booked);
    r.dest = shorten(dest_names(svc["destination"].as<JsonArray>()), 17);
    const char *pa = lm["platform"]["actual"].as<const char *>(), *pp = lm["platform"]["planned"].as<const char *>();
    r.plat = pa ? pa : (pp ? pp : "-");
    const char *fc = dep["realtimeForecast"].as<const char *>();
    if (!fc) fc = dep["realtimeEstimate"].as<const char *>();
    bool cancelled = (dep["isCancelled"].is<bool>() && dep["isCancelled"].as<bool>()) || !strcmp(display, "CANCELLED") || !strcmp(display, "DIVERTED");
    // Late only when RTT says so (positive lateness), or, without a lateness figure, when the
    // forecast is after the booked time. Early running is shown as on time.
    bool has_late = dep["realtimeAdvertisedLateness"].is<int>();
    int late = has_late ? dep["realtimeAdvertisedLateness"].as<int>() : 0;
    bool running_late = fc && (has_late ? late > 0 : iso_hhmm(fc) > r.time);
    if (cancelled) { r.status = "Cancelled"; r.colour = "R"; }
    else if (running_late) { r.status = "Delayed"; r.colour = "Y"; r.expected = iso_hhmm(fc); }
    else if (fc) { r.status = "On time"; r.colour = "G"; }
    else { r.status = "Scheduled"; r.colour = "W"; }
    const char *st = td["status"].as<const char *>();
    if (st && !cancelled && (!strcmp(st, "ARRIVING") || !strcmp(st, "AT_PLATFORM") || !strcmp(st, "DEPART_PREPARING") || !strcmp(st, "DEPART_READY")))
      r.status = "At platform";
    const char *mode = sm["modeType"].as<const char *>();
    if (mode && strstr(mode, "BUS")) { r.status = "Bus • " + r.status; r.plat = "BUS"; }
    int veh = lm["numberOfVehicles"].is<int>() ? lm["numberOfVehicles"].as<int>() : 0;
    if (veh == 1) r.status += " • 1 coach";
    else if (veh > 1) r.status += " • " + std::to_string(veh) + " coaches";
    if (!to.empty()) {
      std::string to_name = station_name(to);
      for (JsonObject d : svc["destination"].as<JsonArray>()) {
        bool match = false;
        for (const char *c : d["location"]["shortCodes"].as<JsonArray>()) if (c && to == c) match = true;
        for (const char *c : d["location"]["longCodes"].as<JsonArray>()) if (c && to == c) match = true;
        const char *dn = d["location"]["description"].as<const char *>();
        if (dn && to_name == dn) match = true;
        if (!match) continue;
        const char *arr = d["temporalData"]["realtimeForecast"].as<const char *>();
        if (!arr) arr = d["temporalData"]["scheduleAdvertised"].as<const char *>();
        if (arr) r.status += " • arr " + iso_hhmm(arr);
      }
    }
    const char *hc = sm["trainReportingIdentity"].as<const char *>();
    if (hc) { r.status += " • "; r.status += hc; }
    const char *op = sm["operator"]["name"].as<const char *>();
    if (op) r.status += " • " + shorten(op, 12);
    const char *uid = sm["uniqueIdentity"].as<const char *>();
    r.uid = uid ? uid : "";
    tmp.push_back({booked, r});
  }
  std::sort(tmp.begin(), tmp.end(), [](const Tmp &a, const Tmp &b) { return a.key < b.key; });
  for (auto &t : tmp) { if (out.rows.size() >= 5) break; out.rows.push_back(t.r); }
  if (out.degraded && !out.rows.empty()) out.rows.back().status += "  (RTT data limited)";
  return true;
}

// Arrivals at `at`, optionally only trains that called at `from` earlier. Row: booked arrival, origin
// name, platform; status carries the booked departure from the origin (the actual one is filled in
// at render time from g_origin_actual).
inline bool parse_arrivals(const std::string &body, const std::string &at, const std::string &from, Board &out, time_t now_utc) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  const char *ln = root["query"]["location"]["description"].as<const char *>();
  out.header = shorten(ln ? ln : at, 12) + " arrivals";
  if (!from.empty()) out.header += " from " + shorten(station_name(from), 12);
  const char *core = root["systemStatus"]["rttCore"].as<const char *>();
  const char *nr = root["systemStatus"]["realtimeNetworkRail"].as<const char *>();
  out.degraded = (core && strcmp(core, "OK")) || (nr && strcmp(nr, "OK"));

  struct Tmp { std::string key; Row r; };
  std::vector<Tmp> tmp;
  for (JsonObject svc : root["services"].as<JsonArray>()) {
    JsonObject td = svc["temporalData"], arr = td["arrival"], sm = svc["scheduleMetadata"], lm = svc["locationMetadata"];
    const char *display = td["displayAs"].as<const char *>();
    const char *booked = arr["scheduleAdvertised"].as<const char *>();
    if (!booked || !display || !strcmp(display, "PASS")) continue;
    if (sm["inPassengerService"].is<bool>() && !sm["inPassengerService"].as<bool>()) continue;
    if (arr["realtimeActual"].as<const char *>()) continue;  // already arrived
    Row r;
    r.time = iso_hhmm(booked);
    r.dest = shorten(dest_names(svc["origin"].as<JsonArray>()), 17);
    const char *pa = lm["platform"]["actual"].as<const char *>(), *pp = lm["platform"]["planned"].as<const char *>();
    r.plat = pa ? pa : (pp ? pp : "-");
    const char *fc = arr["realtimeForecast"].as<const char *>();
    if (!fc) fc = arr["realtimeEstimate"].as<const char *>();
    bool cancelled = (arr["isCancelled"].is<bool>() && arr["isCancelled"].as<bool>()) || !strcmp(display, "CANCELLED") || !strcmp(display, "DIVERTED");
    bool has_late = arr["realtimeAdvertisedLateness"].is<int>();
    int late = has_late ? arr["realtimeAdvertisedLateness"].as<int>() : 0;
    bool running_late = fc && (has_late ? late > 0 : iso_hhmm(fc) > r.time);
    if (cancelled) { r.status = "Cancelled"; r.colour = "R"; }
    else if (running_late) { r.status = "Delayed"; r.colour = "Y"; r.expected = iso_hhmm(fc); }
    else if (fc && iso_hhmm(fc) < r.time) { r.status = "On time"; r.colour = "G"; r.expected = iso_hhmm(fc); }   // running early: show the earlier arrival
    else if (fc) { r.status = "On time"; r.colour = "G"; }
    else { r.status = "Scheduled"; r.colour = "W"; }
    const char *mode = sm["modeType"].as<const char *>();
    if (mode && strstr(mode, "BUS")) { r.status = "Bus • " + r.status; r.plat = "BUS"; }
    JsonArray origins = svc["origin"].as<JsonArray>();
    if (origins.size()) {
      const char *os = origins[0]["temporalData"]["scheduleAdvertised"].as<const char *>();
      if (os) { r.orig_sched = iso_hhmm(os); r.orig_sched_epoch = iso_epoch(os, nullptr, true, now_utc); }
    }
    int veh = lm["numberOfVehicles"].is<int>() ? lm["numberOfVehicles"].as<int>() : 0;
    if (veh == 1) r.status += " • 1 coach";
    else if (veh > 1) r.status += " • " + std::to_string(veh) + " coaches";
    const char *uid = sm["uniqueIdentity"].as<const char *>();
    r.uid = uid ? uid : "";
    tmp.push_back({booked, r});
  }
  std::sort(tmp.begin(), tmp.end(), [](const Tmp &a, const Tmp &b) { return a.key < b.key; });
  for (auto &t : tmp) { if (out.rows.size() >= 5) break; out.rows.push_back(t.r); }
  if (out.degraded && !out.rows.empty()) out.rows.back().status += "  (RTT data limited)";
  return true;
}

// From a /service response: the actual departure time at the origin (first location), or "".
inline std::string parse_origin_actual(const std::string &body) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return "";
  JsonObject svc = doc["service"].is<JsonObject>() ? doc["service"].as<JsonObject>() : doc.as<JsonObject>();
  JsonArray locs = svc["locations"].as<JsonArray>();
  if (!locs.size()) return "";
  const char *act = locs[0]["temporalData"]["departure"]["realtimeActual"].as<const char *>();
  return act ? iso_hhmm(act) : "";
}

// Calling points for one train. Body lines separated by '\n'.
inline bool parse_detail(const std::string &body, const std::string &from, std::string &header, std::string &text) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject svc = doc["service"].is<JsonObject>() ? doc["service"].as<JsonObject>() : doc.as<JsonObject>();
  JsonObject sm = svc["scheduleMetadata"];
  JsonArray locs = svc["locations"].as<JsonArray>();
  size_t n = locs.size();
  const char *origin = n ? locs[0]["location"]["description"].as<const char *>() : nullptr;
  const char *dest = n ? locs[n - 1]["location"]["description"].as<const char *>() : nullptr;
  header = shorten(origin ? origin : "?", 16) + " to " + shorten(dest ? dest : "?", 16);
  const char *hc = sm["trainReportingIdentity"].as<const char *>();
  if (hc) { header += " • "; header += hc; }
  for (JsonObject l : locs) {
    int veh = l["locationMetadata"]["numberOfVehicles"].is<int>() ? l["locationMetadata"]["numberOfVehicles"].as<int>() : 0;
    if (veh) { header += " • " + std::to_string(veh) + " coaches"; break; }
  }
  const char *op = sm["operator"]["name"].as<const char *>();
  if (op) header += " • " + shorten(op, 12);

  std::string from_name = station_name(from);
  bool started = false;
  std::vector<std::string> lines;
  for (JsonObject l : locs) {
    JsonObject loc = l["location"], td = l["temporalData"];
    if (!started) {
      bool hit = false;
      for (const char *c : loc["shortCodes"].as<JsonArray>()) if (c && from == c) hit = true;
      for (const char *c : loc["longCodes"].as<JsonArray>()) if (c && from == c) hit = true;
      const char *dn = loc["description"].as<const char *>();
      if (dn && from_name == dn) hit = true;
      if (!hit) continue;
      started = true;
    }
    const char *display = td["displayAs"].as<const char *>();
    if (!display || !strcmp(display, "PASS")) continue;
    JsonObject dep = td["departure"], arr = td["arrival"];
    const char *t = dep["realtimeForecast"].as<const char *>();
    if (!t) t = dep["realtimeActual"].as<const char *>();
    if (!t) t = dep["scheduleAdvertised"].as<const char *>();
    if (!t) t = arr["realtimeForecast"].as<const char *>();
    if (!t) t = arr["realtimeActual"].as<const char *>();
    if (!t) t = arr["scheduleAdvertised"].as<const char *>();
    const char *dn = loc["description"].as<const char *>();
    std::string line = (t ? iso_hhmm(t) : "--:--") + "  " + shorten(dn ? dn : "?", 22);
    const char *pa = l["locationMetadata"]["platform"]["actual"].as<const char *>();
    const char *pp = l["locationMetadata"]["platform"]["planned"].as<const char *>();
    if (pa || pp) { line += "  P"; line += pa ? pa : pp; }
    bool cancelled = !strcmp(display, "CANCELLED") || (dep["isCancelled"].is<bool>() && dep["isCancelled"].as<bool>());
    if (cancelled) line += "  (cancelled)";
    else if (dest && dn && !strcmp(dest, dn)) line += "  (arr)";
    lines.push_back(line);
  }
  if (!started) {
    for (JsonObject l : locs) {
      const char *display = l["temporalData"]["displayAs"].as<const char *>();
      if (!display || !strcmp(display, "PASS")) continue;
      const char *t = l["temporalData"]["departure"]["scheduleAdvertised"].as<const char *>();
      const char *dn = l["location"]["description"].as<const char *>();
      lines.push_back((t ? iso_hhmm(t) : "--:--") + "  " + shorten(dn ? dn : "?", 22));
    }
  }
  const size_t max_lines = 13;
  if (lines.size() > max_lines) {
    size_t extra = lines.size() - (max_lines - 1);
    lines.resize(max_lines - 1);
    lines.push_back("... " + std::to_string(extra) + " more stops");
  }
  text.clear();
  for (auto &l : lines) { if (!text.empty()) text += "\n"; text += l; }
  if (text.empty()) text = "No calling point data";
  return true;
}

// Agenda JSON from calendar-proxy: {"cal":"...","e":[{"d":"YYYYMMDD","w":"Fri 11 Sep","t":"16:15","u":"17:00","s":"...","a":0},...]}
inline bool parse_calendar(const std::string &body, std::string &name, std::vector<CalItem> &items) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  if (!root["e"].is<JsonArray>()) return false;
  const char *cn = root["cal"].as<const char *>();
  name = cn ? cn : "Calendar";
  items.clear();
  for (JsonObject e : root["e"].as<JsonArray>()) {
    CalItem it;
    const char *d = e["d"].as<const char *>(), *w = e["w"].as<const char *>(), *t = e["t"].as<const char *>();
    const char *u = e["u"].as<const char *>(), *sm = e["s"].as<const char *>(), *l = e["l"].as<const char *>();
    if (!d || !w) continue;
    it.d = d; it.w = w; it.t = t ? t : ""; it.u = u ? u : ""; it.s = sm ? sm : ""; it.l = l ? l : "";
    it.all_day = e["a"].is<int>() && e["a"].as<int>() == 1;
    items.push_back(it);
  }
  return true;
}

#ifdef USE_WIFI
struct Net { std::string ssid; int rssi; };

// Kick off an async scan; ESPHome's event handler stores the results (keep_scan_results must be on).
inline esp_err_t wifi_scan_begin() {
  wifi_scan_config_t cfg = {};
  cfg.show_hidden = false;
  cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  cfg.scan_time.active.min = 80;
  cfg.scan_time.active.max = 150;
  return esp_wifi_scan_start(&cfg, false);
}

// Strongest signal per SSID, sorted best first.
inline std::vector<Net> wifi_scan_results(size_t limit) {
  std::vector<Net> out;
  for (auto &r : esphome::wifi::global_wifi_component->get_scan_result()) {
    if (r.get_is_hidden()) continue;
    std::string ssid = r.get_ssid().str();
    if (ssid.empty()) continue;
    bool dup = false;
    for (auto &o : out) if (o.ssid == ssid) { dup = true; if (r.get_rssi() > o.rssi) o.rssi = r.get_rssi(); }
    if (!dup) out.push_back({ssid, r.get_rssi()});
  }
  std::sort(out.begin(), out.end(), [](const Net &a, const Net &b) { return a.rssi > b.rssi; });
  if (out.size() > limit) out.resize(limit);
  return out;
}

inline const char *signal_bars(int rssi) {
  return rssi >= -55 ? "||||" : rssi >= -65 ? "|||" : rssi >= -75 ? "||" : "|";
}
#endif


#ifdef USE_WIFI
// ---------------------------------------------------------------- background HTTP fetcher
// One FreeRTOS task performs GET requests so the main loop (LVGL, touch) never blocks on the
// network. The main loop submits a Job, polls for the Result, and does all parsing/drawing.
enum JobKind { JOB_TOKEN, JOB_BOARD, JOB_DETAIL, JOB_CAL, JOB_ORIGIN };
struct Job { JobKind kind = JOB_BOARD; std::string url, auth, tag; int board = 0; };
struct Result { JobKind kind = JOB_BOARD; int status = -1; std::string body, tag; int board = 0; };

class Fetcher {
 public:
  void start() {
    if (task_) return;
    mutex_ = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(&Fetcher::run, "tb_fetch", 16384, this, 3, &task_, 0);
  }
  bool busy() { Lock l(mutex_); return busy_ || has_job_; }
  bool submit(Job j) {
    Lock l(mutex_);
    if (busy_ || has_job_) return false;
    job_ = std::move(j);
    has_job_ = true;
    return true;
  }
  bool poll(Result &out) {
    Lock l(mutex_);
    if (!has_result_) return false;
    out = std::move(result_);
    has_result_ = false;
    return true;
  }

 private:
  struct Lock {
    SemaphoreHandle_t m;
    explicit Lock(SemaphoreHandle_t m) : m(m) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m); }
  };
  static void run(void *arg) {
    auto *self = static_cast<Fetcher *>(arg);
    for (;;) {
      Job j;
      bool got = false;
      {
        Lock l(self->mutex_);
        if (self->has_job_) { j = std::move(self->job_); self->has_job_ = false; self->busy_ = true; got = true; }
      }
      if (!got) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
      Result r = do_request(j);
      Lock l(self->mutex_);
      self->result_ = std::move(r);
      self->has_result_ = true;
      self->busy_ = false;
    }
  }
  static Result do_request(const Job &j) {
    Result r;
    r.kind = j.kind;
    r.board = j.board;
    r.tag = j.tag;
    esp_http_client_config_t cfg = {};
    cfg.url = j.url.c_str();
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = 20000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size = 4096;      // RTT sends many response headers
    cfg.buffer_size_tx = 2048;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return r;
    esp_http_client_set_header(c, "Authorization", j.auth.c_str());
    esp_http_client_set_header(c, "User-Agent", "trainboard/1.0");
    esp_http_client_set_header(c, "Accept", "application/json");
    if (esp_http_client_open(c, 0) != ESP_OK) { esp_http_client_cleanup(c); return r; }
    if (esp_http_client_fetch_headers(c) < 0) { esp_http_client_close(c); esp_http_client_cleanup(c); return r; }
    r.status = esp_http_client_get_status_code(c);
    char buf[2048];
    int n;
    while ((n = esp_http_client_read(c, buf, sizeof buf)) > 0) {
      r.body.append(buf, n);
      if (r.body.size() > 400000) break;
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return r;
  }

  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  Job job_;
  Result result_;
  bool has_job_ = false, has_result_ = false, busy_ = false;
};
inline Fetcher g_fetcher;
#endif

inline std::string strip_ns(const std::string &uid) {
  return uid.rfind("gb-nr:", 0) == 0 ? uid.substr(6) : uid;
}

}  // namespace tb
