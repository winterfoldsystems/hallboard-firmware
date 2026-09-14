// Pure logic for the HallBoard display: screen-document parsing, station search, Wi-Fi scanning
// and the background HTTP task. The device talks to one host (the HallBoard backend) and renders
// the document it serves; it never parses a data provider's schema. Glue to LVGL lives in the
// YAML lambdas in hallboard.yaml.
#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include "esphome/components/json/json_util.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "stations.h"
#ifdef USE_WIFI
#include <strings.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace tb {

// One row of a board page. `uid` is the document's service id, echoed back to
// GET /v1/device/service/<id> on a long press.
struct Row { std::string time, dest, plat, status, colour, uid, expected; };
// One board page. `asof` is the provider timestamp the backend built it from (unix seconds) and
// `stale` is set when the backend is serving its last good payload past the module's limit.
struct Board { std::string header; std::vector<Row> rows; uint32_t asof = 0; bool stale = false; };

// The two board pages of the last document (kept here so lambdas can use them without ESPHome
// globals, which are declared before this header is included).
inline Board g_cache[2];

// The agenda page of the last document: one item per event occurrence, already sorted.
struct CalItem { std::string d, w, t, u, s, l; bool all_day = false; };
inline std::vector<CalItem> g_cal;
inline std::string g_cal_name;
inline uint32_t g_cal_asof = 0;
inline bool g_cal_stale = false;

// Firmware version, sent as X-Firmware on every request. Set from the YAML substitution on boot.
inline std::string g_fw;

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

// ---------------------------------------------------------------- device identity
// The device secret is 64 lowercase hex characters held in a restored char[65] global. It is
// generated here on first boot and is never logged, displayed or sent anywhere but the backend.
inline bool secret_valid(const char *s) {
  if (!s) return false;
  for (int i = 0; i < 64; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return s[64] == 0;
}

#ifdef USE_WIFI
inline void fill_secret(char *out /* at least 65 bytes */) {
  uint8_t raw[32];
  esp_fill_random(raw, sizeof raw);
  static const char *HEX = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2] = HEX[(raw[i] >> 4) & 0xF];
    out[i * 2 + 1] = HEX[raw[i] & 0xF];
  }
  out[64] = 0;
}

// Lowercase colon-separated MAC, sent to pair/begin as a human-readable label only.
inline std::string mac_lower() {
  uint8_t m[6] = {};
  esphome::get_mac_address_raw(m);
  char b[18];
  snprintf(b, sizeof b, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
  return b;
}
#endif

// ---------------------------------------------------------------- JSON helpers
// Every string taken out of a document is truncated: the device renders fixed-width labels and a
// malformed or oversized document must not be able to grow the heap without bound.
template<typename T> inline std::string jstr(T v, size_t limit) {
  const char *s = v.template as<const char *>();
  if (!s) return "";
  std::string o(s);
  if (o.size() > limit) o.resize(limit);
  return o;
}
template<typename T> inline uint32_t juint(T v) { return v.template as<uint32_t>(); }
template<typename T> inline bool jbool(T v) { return v.template as<bool>(); }

// ---------------------------------------------------------------- screen document (docs/screen-document.md)
// What the last parse found, so the caller knows which pages the document actually carried.
struct ScreenInfo { bool board_seen[2] = {false, false}; bool cal_seen = false; int pages = 0; };

// Copies the first two "board" pages into g_cache[0]/[1] and the first "agenda" page into g_cal.
// Other page types and unknown fields are ignored, as the contract requires.
inline bool parse_screen(const std::string &body, ScreenInfo &info) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  if (juint(root["v"]) != 1) return false;
  if (!root["pages"].is<JsonArray>()) return false;

  const size_t MAX_PAGES = 8, MAX_ROWS = 5, MAX_EVENTS = 64;
  int board = 0;
  JsonArray pages = root["pages"].as<JsonArray>();
  for (JsonObject pg : pages) {
    if ((size_t) info.pages >= MAX_PAGES) break;
    info.pages++;
    const char *type = pg["type"].as<const char *>();
    if (!type) continue;
    if (!strcmp(type, "board") && board < 2) {
      Board nb;
      nb.header = jstr(pg["title"], 40);
      nb.asof = juint(pg["asof"]);
      nb.stale = jbool(pg["stale"]);
      JsonArray rows = pg["rows"].as<JsonArray>();
      for (JsonObject rw : rows) {
        if (nb.rows.size() >= MAX_ROWS) break;
        Row r;
        r.time = jstr(rw["t"], 8);
        r.dest = jstr(rw["d"], 32);
        r.plat = jstr(rw["p"], 4);
        r.status = jstr(rw["s"], 96);
        r.colour = jstr(rw["c"], 1);
        r.expected = jstr(rw["e"], 8);
        r.uid = jstr(rw["id"], 48);
        nb.rows.push_back(r);
      }
      g_cache[board] = nb;
      info.board_seen[board] = true;
      board++;
    } else if (!strcmp(type, "agenda") && !info.cal_seen) {
      std::vector<CalItem> items;
      JsonArray events = pg["e"].as<JsonArray>();
      for (JsonObject e : events) {
        if (items.size() >= MAX_EVENTS) break;
        CalItem it;
        it.d = jstr(e["d"], 8);
        it.w = jstr(e["w"], 24);
        if (it.d.empty() || it.w.empty()) continue;
        it.t = jstr(e["t"], 8);
        it.u = jstr(e["u"], 10);
        it.s = jstr(e["s"], 64);
        it.l = jstr(e["l"], 48);
        it.all_day = juint(e["a"]) == 1;
        items.push_back(it);
      }
      g_cal = items;
      g_cal_name = jstr(pg["cal"], 24);
      if (g_cal_name.empty()) g_cal_name = "Calendar";
      g_cal_asof = juint(pg["asof"]);
      g_cal_stale = jbool(pg["stale"]);
      info.cal_seen = true;
    }
  }
  // Pages the document no longer carries stop being displayed.
  for (int i = board; i < 2; i++) g_cache[i] = Board();
  if (!info.cal_seen) { g_cal.clear(); g_cal_name.clear(); g_cal_asof = 0; g_cal_stale = false; }
  return true;
}

// POST /v1/pair/begin: {claimed:true} or {claimed:false, code, expires_in, poll}.
inline bool parse_pair(const std::string &body, bool &claimed, std::string &code,
                       uint32_t &expires_in, uint32_t &poll) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  if (!root["claimed"].is<bool>()) return false;
  claimed = jbool(root["claimed"]);
  code = jstr(root["code"], 16);
  uint32_t e = juint(root["expires_in"]);
  expires_in = (e >= 30 && e <= 3600) ? e : 900;
  uint32_t p = juint(root["poll"]);
  poll = (p >= 5 && p <= 600) ? p : 10;
  return claimed || !code.empty();
}

// GET /v1/device/config: {claimed, poll, tz}.
inline bool parse_config(const std::string &body, bool &claimed, uint32_t &poll) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  if (!root["claimed"].is<bool>()) return false;
  claimed = jbool(root["claimed"]);
  uint32_t p = juint(root["poll"]);
  poll = (p >= 5 && p <= 600) ? p : (claimed ? 60 : 10);
  return true;
}

// Path-safe subset of a service id before it is pasted into a request URL. The ids come from
// our own backend, but nothing lifted out of a document reaches a URL unchecked.
inline std::string url_token(const std::string &s) {
  std::string o;
  for (char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~')
      o += c;
    if (o.size() >= 48) break;
  }
  return o;
}

// GET /v1/device/service/<id>: {hdr, lines[]}, already formatted for the device font.
inline bool parse_service(const std::string &body, std::string &header, std::string &text) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  if (!root["lines"].is<JsonArray>()) return false;
  header = jstr(root["hdr"], 72);
  text.clear();
  int n = 0;
  JsonArray lines = root["lines"].as<JsonArray>();
  for (JsonVariant l : lines) {
    if (n++ >= 13) break;
    std::string line = jstr(l, 64);
    if (!text.empty()) text += "\n";
    text += line;
  }
  if (text.empty()) text = "No calling point data";
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
// One FreeRTOS task performs the request so the main loop (LVGL, touch) never blocks on the
// network. The main loop submits a Job, polls for the Result, and does all parsing/drawing.
enum JobKind { JOB_PAIR, JOB_CONFIG, JOB_SCREEN, JOB_SETTINGS, JOB_DETAIL };
enum JobMethod { M_GET, M_POST, M_PATCH };
struct Job {
  JobKind kind = JOB_SCREEN;
  JobMethod method = M_GET;
  std::string url, auth, body, inm, tag;
  int board = 0;
};
struct Result {
  JobKind kind = JOB_SCREEN;
  int status = -1;            // < 0: the request never completed
  std::string body, etag, tag;
  int board = 0;
};

// A screen document is under 8 KB by contract; the cap is generous headroom, not a target.
static const size_t MAX_BODY = 65536;

class Fetcher {
 public:
  void start() {
    if (task_) return;
    mutex_ = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(&Fetcher::run, "hb_fetch", 16384, this, 3, &task_, 0);
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
  // esp_http_client_get_header() reads the REQUEST header list, so the response ETag has to be
  // picked up from the header event instead. user_data points at the Result's etag string.
  static esp_err_t on_event(esp_http_client_event_t *e) {
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->user_data && e->header_key && e->header_value &&
        strcasecmp(e->header_key, "ETag") == 0) {
      auto *s = static_cast<std::string *>(e->user_data);
      s->assign(e->header_value);
      if (s->size() > 96) s->resize(96);
    }
    return ESP_OK;
  }
  static Result do_request(const Job &j) {
    Result r;
    r.kind = j.kind;
    r.board = j.board;
    r.tag = j.tag;
    esp_http_client_config_t cfg = {};
    cfg.url = j.url.c_str();
    cfg.method = j.method == M_POST ? HTTP_METHOD_POST : (j.method == M_PATCH ? HTTP_METHOD_PATCH : HTTP_METHOD_GET);
    cfg.timeout_ms = 20000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;   // TLS verified against the ESP-IDF bundle
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    cfg.event_handler = &Fetcher::on_event;
    cfg.user_data = &r.etag;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return r;
    if (!j.auth.empty()) esp_http_client_set_header(c, "Authorization", j.auth.c_str());
    if (!g_fw.empty()) esp_http_client_set_header(c, "X-Firmware", g_fw.c_str());
    esp_http_client_set_header(c, "Accept", "application/json");
    if (!j.body.empty()) esp_http_client_set_header(c, "Content-Type", "application/json");
    if (!j.inm.empty()) esp_http_client_set_header(c, "If-None-Match", j.inm.c_str());
    if (esp_http_client_open(c, (int) j.body.size()) != ESP_OK) { esp_http_client_cleanup(c); return r; }
    if (!j.body.empty() && esp_http_client_write(c, j.body.data(), (int) j.body.size()) < 0) {
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      return r;
    }
    if (esp_http_client_fetch_headers(c) < 0) { esp_http_client_close(c); esp_http_client_cleanup(c); return r; }
    r.status = esp_http_client_get_status_code(c);
    // 304 carries no body and falls straight out of the read loop.
    char buf[2048];
    int n;
    while ((n = esp_http_client_read(c, buf, sizeof buf)) > 0) {
      r.body.append(buf, n);
      if (r.body.size() > MAX_BODY) break;
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

}  // namespace tb
