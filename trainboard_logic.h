// Pure logic for the HallBoard display: the backend's other responses, station search, Wi-Fi
// scanning and the background HTTP task. The device talks to one host (the HallBoard backend) and
// renders the document it serves; it never parses a data provider's schema. Glue to LVGL lives in
// the YAML lambdas in hallboard.yaml. The screen document itself is parsed in hb_parse.h, which
// this file includes.
#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <algorithm>
#include "esphome/components/json/json_util.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "hb_parse.h"
#include "hb_ui.h"
#include "stations.h"
#ifdef USE_WIFI
#include <atomic>
#include <strings.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace tb {

// The page model lives in hb_ui.h: parse_screen (hb_parse.h) turns a document into an
// hb::Document and the views in hb_ui.h render it. Nothing here keeps a copy of the screen.

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
  poll = (p >= 5 && p <= 1800) ? p : 10;
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
  poll = (p >= 5 && p <= 1800) ? p : (claimed ? 60 : 10);
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

// Scheme and host of an https URL, lowercased, or "" when the URL is not one this device will
// fetch. Only https is accepted, a userinfo section ("https://user@host/") is refused outright so
// no credential-bearing or host-confusing URL can reach esp_http_client, and the port is dropped
// so the host can be compared to the backend's name exactly.
inline std::string url_host(const std::string &url) {
  // GitHub answers a release download with a signed redirect of about a thousand characters.
  if (url.size() > 2048 || url.rfind("https://", 0) != 0) return "";
  size_t start = 8;
  size_t end = url.find_first_of("/?#", start);
  std::string hostport = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
  if (hostport.empty() || hostport.find('@') != std::string::npos) return "";
  size_t colon = hostport.find(':');
  if (colon != std::string::npos) hostport.resize(colon);
  if (hostport.empty()) return "";
  for (char c : hostport) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!ok) return "";
  }
  for (auto &c : hostport) c = tolower((unsigned char) c);
  return hostport;
}

// The one host that ever receives the device credential. GitHub and any object store the manifest
// points at are public and must never see the bearer token.
inline const char *BACKEND_HOST = "api.hallboard.co.uk";

// Length-independent comparison, so a digest mismatch leaks nothing through timing.
inline bool ct_equal(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); i++) diff |= (unsigned char) (a[i] ^ b[i]);
  return diff == 0;
}

// ---------------------------------------------------------------- firmware manifest
// GET /v1/device/firmware answers {"version": null} or {version, url, sha256, size, force}.
// Every field here decides what gets written to the other app slot, so each one is bounds-checked
// before it is kept and a malformed offer is dropped whole rather than half-applied.
struct FwOffer {
  std::string version, url, sha256;
  uint32_t size = 0;
  bool force = false;
  bool valid() const { return !version.empty(); }
};

// Largest image this firmware will accept: comfortably inside the 0x3C0000 app slot.
static const uint32_t MAX_FW_SIZE = 3900000;

inline bool fw_version_ok(const std::string &v) {
  if (v.empty() || v.size() > 16) return false;
  bool digit = false;
  for (char c : v) {
    if (c >= '0' && c <= '9') digit = true;
    else if (c != '.') return false;
  }
  return digit;
}
inline bool hex64_lower(const std::string &s) {
  if (s.size() != 64) return false;
  for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

// true when the answer was understood; out.valid() is false for "nothing on offer". Strings are
// read one step longer than the limit so a value that is too long fails validation instead of
// being silently truncated into a valid-looking one.
inline bool parse_firmware(const std::string &body, FwOffer &out) {
  out = FwOffer();
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) return false;
  if (!root["version"].is<const char *>()) return true;   // {"version": null}: nothing on offer
  FwOffer o;
  o.version = jstr(root["version"], 32);
  o.url = jstr(root["url"], 512);
  o.sha256 = jstr(root["sha256"], 128);
  o.size = juint(root["size"]);
  o.force = jbool(root["force"]);
  if (!fw_version_ok(o.version)) return false;
  if (o.url.size() > 256 || url_host(o.url).empty()) return false;
  if (!hex64_lower(o.sha256)) return false;
  if (o.size == 0 || o.size > MAX_FW_SIZE) return false;
  out = o;
  return true;
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
enum JobKind { JOB_PAIR, JOB_CONFIG, JOB_SCREEN, JOB_SETTINGS, JOB_DETAIL, JOB_FIRMWARE, JOB_OTA, JOB_FWRESULT };
enum JobMethod { M_GET, M_POST, M_PATCH };
struct Job {
  JobKind kind = JOB_SCREEN;
  JobMethod method = M_GET;
  std::string url, auth, body, inm, tag;
  int board = 0;
  std::string sha;            // JOB_OTA: the announced SHA-256, 64 lowercase hex
  uint32_t size = 0;          // JOB_OTA: the announced image size, enforced exactly
};
struct Result {
  JobKind kind = JOB_SCREEN;
  int status = -1;            // < 0: the request never completed
  std::string body, etag, tag;
  int board = 0;
  uint32_t poll_after = 0;    // seconds from the Poll-After response header, 0 if absent
  std::string err;            // JOB_OTA: short failure code, empty on success
};

// A screen document is under 8 KB by contract; the cap is generous headroom, not a target.
// It does not apply to JOB_OTA, whose cap is the announced image size.
static const size_t MAX_BODY = 65536;

// Download progress, 0 to 100, published by the fetch task and read by the main loop so the clock
// page can show it. -1 means no install is running.
inline std::atomic<int> g_ota_pct{-1};

class Fetcher {
 public:
  void start() {
    if (task_) return;
    mutex_ = xSemaphoreCreateMutex();
    // 20 KB: the TLS handshake with the certificate bundle is the deep part, and a firmware
    // install adds a 2 KB read buffer, an mbedtls SHA-256 context and the flash writes on top of
    // it. 16 KB carried 1a and 1b; the extra 4 KB is headroom for the install path.
    xTaskCreatePinnedToCore(&Fetcher::run, "hb_fetch", 20480, this, 3, &task_, 0);
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
      // The install runs here, on the fetch task, so LVGL and touch keep running for the whole
      // download. It is the one job that writes flash rather than returning a body.
      Result r = j.kind == JOB_OTA ? do_ota(j) : do_request(j);
      Lock l(self->mutex_);
      self->result_ = std::move(r);
      self->has_result_ = true;
      self->busy_ = false;
    }
  }
  // esp_http_client_get_header() reads the REQUEST header list, so response headers have to be
  // picked up from the header event instead. user_data points at the whole Result so both the
  // ETag and the Poll-After cadence hint can be captured off the same callback.
  static esp_err_t on_event(esp_http_client_event_t *e) {
    if (e->event_id != HTTP_EVENT_ON_HEADER || !e->user_data || !e->header_key || !e->header_value) {
      return ESP_OK;
    }
    auto *r = static_cast<Result *>(e->user_data);
    if (strcasecmp(e->header_key, "ETag") == 0) {
      r->etag.assign(e->header_value);
      if (r->etag.size() > 96) r->etag.resize(96);
    } else if (strcasecmp(e->header_key, "Poll-After") == 0) {
      // Digits only, and only in the range the backend is documented to send; anything else
      // is untrusted network input and is discarded rather than fed into the poll cadence.
      const char *v = e->header_value;
      bool digits = *v != '\0';
      for (const char *p = v; *p; p++) {
        if (!isdigit(static_cast<unsigned char>(*p))) { digits = false; break; }
      }
      if (digits) {
        unsigned long n = strtoul(v, nullptr, 10);
        if (n >= 15 && n <= 1800) r->poll_after = static_cast<uint32_t>(n);
      }
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
    cfg.user_data = &r;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return r;
    if (!j.auth.empty()) esp_http_client_set_header(c, "Authorization", j.auth.c_str());
    if (!g_fw.empty()) esp_http_client_set_header(c, "X-Firmware", g_fw.c_str());
    esp_http_client_set_header(c, "Accept", "application/json");
    if (!j.body.empty()) esp_http_client_set_header(c, "Content-Type", "application/json");
    if (!j.inm.empty()) esp_http_client_set_header(c, "If-None-Match", j.inm.c_str());
    // A short code on each early exit, so a status line can say why the request never completed
    // rather than only that it did not.
    if (esp_http_client_open(c, (int) j.body.size()) != ESP_OK) {
      r.err = "connect";
      esp_http_client_cleanup(c);
      return r;
    }
    if (!j.body.empty() && esp_http_client_write(c, j.body.data(), (int) j.body.size()) < 0) {
      r.err = "send";
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      return r;
    }
    if (esp_http_client_fetch_headers(c) < 0) {
      r.err = "headers";
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      return r;
    }
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

  // ---------------------------------------------------------------- firmware install
  // Only the Location header matters on the redirect hops, so the OTA download uses its own
  // event handler rather than the Result-shaped one above.
  struct OtaCtx { std::string location; };
  static esp_err_t on_ota_event(esp_http_client_event_t *e) {
    if (e->event_id != HTTP_EVENT_ON_HEADER || !e->user_data || !e->header_key || !e->header_value) {
      return ESP_OK;
    }
    auto *ctx = static_cast<OtaCtx *>(e->user_data);
    if (strcasecmp(e->header_key, "Location") == 0) {
      ctx->location.assign(e->header_value);
      if (ctx->location.size() > 2048) ctx->location.resize(2048);
    }
    return ESP_OK;
  }

  // Streams the image straight into the inactive app slot: 2 KB at a time, hashed on the way past,
  // never buffered whole. The running slot is untouched, so losing power here leaves the board on
  // the firmware it already had. Failure codes: download, size, sha256, verify, write, timeout,
  // redirect.
  static Result do_ota(const Job &j) {
    Result r;
    r.kind = JOB_OTA;
    r.tag = j.tag;              // the offered version, echoed back in the result post
    g_ota_pct.store(0);

    const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
    if (part == nullptr) { r.err = "verify"; g_ota_pct.store(-1); return r; }
    if (j.size > part->size) { r.err = "size"; g_ota_pct.store(-1); return r; }

    esp_ota_handle_t handle = 0;
    // OTA_WITH_SEQUENTIAL_WRITES erases sector by sector as the stream arrives, so the task never
    // blocks on a 3.7 MB erase up front.
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
      r.err = "write";
      g_ota_pct.store(-1);
      return r;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    const char *err = nullptr;
    std::string url = j.url;
    uint32_t got = 0;
    int hops = 0, last_pct = 0;

    for (;;) {
      std::string host = url_host(url);
      if (host.empty()) { err = "redirect"; break; }   // https only, and nothing exotic
      OtaCtx ctx;
      esp_http_client_config_t cfg = {};
      cfg.url = url.c_str();
      cfg.method = HTTP_METHOD_GET;
      cfg.timeout_ms = 30000;
      cfg.crt_bundle_attach = esp_crt_bundle_attach;
      cfg.buffer_size = 2048;
      // The request line carries the redirected URL, so the send buffer must hold it.
      cfg.buffer_size_tx = 3072;
      cfg.event_handler = &Fetcher::on_ota_event;
      cfg.user_data = &ctx;
      cfg.disable_auto_redirect = true;   // redirects are followed here, with the host re-checked
      esp_http_client_handle_t c = esp_http_client_init(&cfg);
      if (c == nullptr) { err = "download"; break; }
      // The device credential goes to the backend and nowhere else. A release asset lives on a
      // public host, so neither the bearer nor anything about this device is set for it.
      if (host == BACKEND_HOST) {
        if (!j.auth.empty()) esp_http_client_set_header(c, "Authorization", j.auth.c_str());
        if (!g_fw.empty()) esp_http_client_set_header(c, "X-Firmware", g_fw.c_str());
      }
      esp_http_client_set_header(c, "Accept", "application/octet-stream");
      if (esp_http_client_open(c, 0) != ESP_OK) {
        esp_http_client_cleanup(c);
        err = "download";
        break;
      }
      if (esp_http_client_fetch_headers(c) < 0) {
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        err = "download";
        break;
      }
      int status = esp_http_client_get_status_code(c);
      if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        std::string next = ctx.location;
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        // Absolute https only: a relative or downgraded Location is refused rather than resolved.
        if (++hops > 3 || next.empty() || url_host(next).empty()) { err = "redirect"; break; }
        url = next;
        continue;
      }
      if (status != 200) {
        ESP_LOGW("hb", "firmware download answered HTTP %d at hop %d", status, hops);
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        err = "download";
        break;
      }

      char buf[2048];
      int n;
      while ((n = esp_http_client_read(c, buf, sizeof buf)) > 0) {
        if (got + (uint32_t) n > j.size) { err = "size"; break; }
        mbedtls_sha256_update(&sha, (const unsigned char *) buf, (size_t) n);
        if (esp_ota_write(handle, buf, (size_t) n) != ESP_OK) { err = "write"; break; }
        got += (uint32_t) n;
        int pct = (int) ((uint64_t) got * 100 / j.size);
        if (pct != last_pct) { last_pct = pct; g_ota_pct.store(pct); }
      }
      if (err == nullptr && n < 0) err = "timeout";
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      break;
    }

    unsigned char digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (err == nullptr && got != j.size) err = "size";
    if (err == nullptr) {
      static const char *HEX = "0123456789abcdef";
      std::string hex;
      hex.reserve(64);
      for (unsigned char b : digest) { hex += HEX[(b >> 4) & 0xF]; hex += HEX[b & 0xF]; }
      if (!ct_equal(hex, j.sha)) err = "sha256";
    }
    if (err != nullptr) {
      esp_ota_abort(handle);
      ESP_LOGW("hb", "firmware install failed (%s) after %u bytes", err, (unsigned) got);
      r.err = err;
      g_ota_pct.store(-1);
      return r;
    }
    // esp_ota_end verifies the image header and, where signing is configured, its signature. It
    // releases the handle either way, so there is nothing left to abort after it.
    if (esp_ota_end(handle) != ESP_OK) {
      ESP_LOGW("hb", "firmware image rejected by esp_ota_end");
      r.err = "verify";
      g_ota_pct.store(-1);
      return r;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
      ESP_LOGW("hb", "could not select the new boot partition");
      r.err = "verify";
      g_ota_pct.store(-1);
      return r;
    }
    ESP_LOGI("hb", "firmware image written and verified: %u bytes", (unsigned) got);
    g_ota_pct.store(100);
    r.status = 200;
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
