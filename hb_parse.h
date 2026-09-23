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
//
// It is also filtered down to printable ASCII. The backend sends nothing else (see
// docs/screen-document.md), so a byte at 0x80 or above is either a mistake or someone trying it
// on, and either way it would draw as a hollow box or exercise a glyph the font was never built
// with. Control characters go the same way: a newline would re-flow a label and the rest are not
// text at all. Only `limit` characters are ever kept, so the cap still holds.
template<typename T> inline std::string jstr(T v, size_t limit) {
  const char *s = v.template as<const char *>();
  if (!s) return "";
  std::string o;
  for (const char *p = s; *p != '\0' && o.size() < limit; p++) {
    unsigned char c = (unsigned char) *p;
    if (c < 0x20 || c >= 0x7F) continue;
    o += (char) c;
  }
  return o;
}
template<typename T> inline uint32_t juint(T v) { return v.template as<uint32_t>(); }
template<typename T> inline bool jbool(T v) { return v.template as<bool>(); }

// ---------------------------------------------------------------- screen document (docs/screen-document.md)
// Parses a document into an hb::Document. Unknown page types and unknown fields are ignored, as
// the contract requires, and every list is capped: 8 pages, 5 board rows, 60 events, 8 generic
// rows, 6 weather hours, with every string truncated by jstr. A malformed document cannot grow
// the heap.
inline bool parse_screen(const std::string &body, hb::Document &out) {
  JsonDocument doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;
  JsonObject root = doc.as<JsonObject>();
  if (juint(root["v"]) != 1) return false;
  if (!root["pages"].is<JsonArray>()) return false;

  const size_t MAX_PAGES = 8, MAX_ROWS = 5, MAX_EVENTS = 60, MAX_GROWS = 8, MAX_HOURS = 6;
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
      // The board in one name, for the header. Absent on a backend older than 1.4.0, and absent
      // whenever the rows carry no name to shorten, so the title is still the fallback.
      p.short_title = jstr(pg["short"], 24);
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
        // Every face but the weather one ignores this: a backend older than 1.4.0 named an icon
        // for a reminder row and none of those draw one, but the weather page's first row (the
        // hero) reuses this same field for current conditions, one of the names hb_icons.h knows.
        r.icon = jstr(rw["i"], 12);
        r.value = jstr(rw["v"], 8);
        r.a = jstr(rw["a"], 40);
        r.b = jstr(rw["b"], 64);
        p.grows.push_back(r);
      }
      // The weather face's own fields, which sit beside `rows` on the weather page and are
      // absent everywhere else. Every one is optional; an empty string is how a face knows the
      // provider sent nothing.
      p.place = jstr(pg["place"], 24);
      p.temp = jstr(pg["temp"], 6);
      p.feels = jstr(pg["feels"], 6);
      p.head = jstr(pg["head"], 24);
      p.sent = jstr(pg["sent"], 64);
      // Current conditions and wind, for the clock's one-line summary under the numerals. Both
      // optional, both absent on a backend older than this field.
      p.cond = jstr(pg["cond"], 24);
      p.wind = jstr(pg["wind"], 16);
      // The weather face's wind and rain cards. Each is its own optional field, not a struct, so
      // an older backend (or a provider with nothing for one of them) can send some and not
      // others: `wdir` and `gust` in particular are routinely absent.
      p.wdir = jstr(pg["wdir"], 4);
      p.wspd = jstr(pg["wspd"], 4);
      p.gust = jstr(pg["gust"], 4);
      p.rday = -1;
      if (pg["rday"].is<int>()) {
        int rd = pg["rday"].as<int>();
        p.rday = rd < 0 ? 0 : (rd > 100 ? 100 : rd);
      }
      JsonArray hours = pg["hours"].as<JsonArray>();
      for (JsonObject hr : hours) {
        if (p.hours.size() >= MAX_HOURS) break;
        hb::HourSlot h;
        h.h = jstr(hr["h"], 2);
        h.t = jstr(hr["t"], 6);
        // One of hb_icons.h's names (sun, partly, cloud, rain, pour, snow, fog, storm, wind,
        // night). Optional: absent on a backend older than the hourly icon row, in which case
        // SkyView draws no icon row at all rather than a strip of empty cells.
        h.i = jstr(hr["i"], 8);
        int r = hr["r"].is<int>() ? hr["r"].as<int>() : 0;
        h.r = r < 0 ? 0 : (r > 100 ? 100 : r);
        p.hours.push_back(std::move(h));
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
