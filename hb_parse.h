// Screen-document parsing: the JSON helpers and parse_screen, which turns a document
// (docs/screen-document.md) into an hb::Document for the views in hb_ui.h to render.
//
// This sits in its own header rather than in trainboard_logic.h because the host simulator under
// firmware/sim/ compiles it: it needs the document parser and the views, and nothing else in
// trainboard_logic.h (Wi-Fi, the HTTP task, the station table) builds off the device.
// trainboard_logic.h includes this file, so the firmware sees exactly what it always did.
#pragma once
#include <cstring>
#include <string>

#include "esphome/components/json/json_util.h"
#include "hb_ui.h"

namespace tb {

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
// Parses a document into an hb::Document. Unknown page types and unknown fields are ignored, as
// the contract requires, and every list is capped: 8 pages, 5 board rows, 60 events, 8 generic
// rows, with every string truncated by jstr. A malformed document cannot grow the heap.
inline bool parse_screen(const std::string &body, hb::Document &out) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  if (juint(root["v"]) != 1) return false;
  if (!root["pages"].is<JsonArray>()) return false;

  const size_t MAX_PAGES = 8, MAX_ROWS = 5, MAX_EVENTS = 60, MAX_GROWS = 8;
  out = hb::Document();
  out.gen = juint(root["gen"]);
  out.tz = jstr(root["tz"], 40);
  // The POSIX form of the same zone, which is what the clock component wants. An older backend
  // does not send it and an unparseable one is ignored downstream, so absent is fine.
  out.tzp = jstr(root["tzp"], 64);

  JsonArray pages = root["pages"].as<JsonArray>();
  for (JsonObject pg : pages) {
    if (out.pages.size() >= MAX_PAGES) break;
    const char *type = pg["type"].as<const char *>();
    if (!type) continue;
    hb::Page p;
    if (!strcmp(type, "board")) {
      p.type = 'b';
      p.title = jstr(pg["title"], 40);
      // "rail", "tube" or "bus". Absent on a backend older than the tube and bus modules, which
      // only ever built rail boards; BoardView reads an empty module as rail for that reason.
      p.module = jstr(pg["module"], 24);
      p.mode = jstr(pg["mode"], 4);
      JsonArray rows = pg["rows"].as<JsonArray>();
      for (JsonObject rw : rows) {
        if (p.rows.size() >= MAX_ROWS) break;
        hb::BoardRow r;
        r.time = jstr(rw["t"], 8);
        r.dest = jstr(rw["d"], 32);
        r.plat = jstr(rw["p"], 4);
        r.status = jstr(rw["s"], 96);
        r.colour = jstr(rw["c"], 1);
        r.expected = jstr(rw["e"], 8);
        r.uid = jstr(rw["id"], 48);
        p.rows.push_back(r);
      }
    } else if (!strcmp(type, "agenda")) {
      p.type = 'a';
      p.title = jstr(pg["cal"], 24);
      if (p.title.empty()) p.title = "Calendar";
      JsonArray events = pg["e"].as<JsonArray>();
      for (JsonObject e : events) {
        if (p.events.size() >= MAX_EVENTS) break;
        hb::AgendaEvent it;
        it.d = jstr(e["d"], 8);
        it.w = jstr(e["w"], 24);
        if (it.d.empty() || it.w.empty()) continue;
        it.t = jstr(e["t"], 8);
        it.u = jstr(e["u"], 10);
        it.s = jstr(e["s"], 64);
        it.l = jstr(e["l"], 48);
        it.all_day = juint(e["a"]) == 1;
        p.events.push_back(it);
      }
    } else if (!strcmp(type, "generic")) {
      p.type = 'g';
      p.title = jstr(pg["title"], 40);
      p.module = jstr(pg["module"], 24);
      JsonArray rows = pg["rows"].as<JsonArray>();
      for (JsonObject rw : rows) {
        if (p.grows.size() >= MAX_GROWS) break;
        hb::GenericRow r;
        r.icon = jstr(rw["i"], 12);
        r.value = jstr(rw["v"], 8);
        r.a = jstr(rw["a"], 40);
        r.b = jstr(rw["b"], 64);
        p.grows.push_back(r);
      }
    } else {
      continue;   // a page type this firmware does not know is skipped, not an error
    }
    p.id = jstr(pg["id"], 24);
    if (p.id.empty()) p.id = std::string(1, p.type) + std::to_string(out.pages.size());
    p.asof = juint(pg["asof"]);
    p.stale = jbool(pg["stale"]);
    out.pages.push_back(std::move(p));
  }

  // Display settings. Absent or out of range means full brightness and no night window.
  JsonObject st = root["settings"].as<JsonObject>();
  if (!st.isNull()) {
    // The household's name for this board, for the boot support line. Absent on an older backend.
    out.name = jstr(st["name"], 32);
    // A short ASCII line for the clock page, for example a payment that needs attention. Absent
    // or empty means there is nothing to say, which is how a notice is taken down again.
    out.notice = jstr(st["notice"], 40);
    if (st["brightness"].is<int>()) {
      int b = st["brightness"].as<int>();
      if (b >= 0 && b <= 100) out.settings.brightness = b;
    }
    JsonObject night = st["night"].as<JsonObject>();
    if (!night.isNull()) {
      int from = hb::hhmm_to_minutes(jstr(night["from"], 5));
      int to = hb::hhmm_to_minutes(jstr(night["to"], 5));
      if (from >= 0 && to >= 0 && from != to) {
        out.settings.night.enabled = true;
        out.settings.night.from = from;
        out.settings.night.to = to;
        int nb = night["brightness"].is<int>() ? night["brightness"].as<int>() : 20;
        out.settings.night.brightness = (nb >= 0 && nb <= 100) ? nb : 20;
      }
    }
  }
  return true;
}

}  // namespace tb
