// Runtime UI for HallBoard. Every content page on screen is built here from the screen document
// (docs/screen-document.md) by one of three templates (board, agenda, generic), plus a clock page
// that is always first and a pairing page shown while the device is unclaimed. ui.yaml owns a
// single ESPHome page, `content_page`; everything inside it is created and deleted by PageHost,
// so a household adding a module is a backend release and not a firmware one.
//
// Horizontal paging has two implementations. HB_CAROUSEL 1 lays the pages out side by side in one
// snap-scrolling container and lets LVGL do the swipe; 0 stacks them at the same position and
// slides them with an lv_anim from the gesture callback. Both compile; flip the switch if the RGB
// panel misbehaves with the carousel.
#pragma once

// Decided on the wall board on 14 September 2026: the snap carousel needs a drag past the
// midpoint and cannot wrap, so the gesture fallback (a short flick, wrapping) is the one shipped.
#define HB_CAROUSEL 0

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <lvgl.h>
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
#include "hb_tokens.h"

namespace hb {

// ---------------------------------------------------------------- the document
// A parsed screen document. trainboard_logic.h fills this in; nothing here ever touches JSON.

// One row of a board page. `uid` is the document's service id, echoed back to
// GET /v1/device/service/<id> on a long press.
struct BoardRow {
  std::string time, dest, plat, status, colour, expected, uid;
};

// One event occurrence on an agenda page, already sorted by the backend.
struct AgendaEvent {
  std::string d, w, t, u, s, l;
  bool all_day = false;
};

// One row of a generic page: an icon name from the compiled set, a large value and two text lines.
struct GenericRow {
  std::string icon, value, a, b;
};

// One two-hourly slot of the weather page's strip: the hour, the temperature at it and the
// chance of rain as a percentage.
struct HourSlot {
  std::string h, t;
  int r = 0;
};

// A page of any type. The three row vectors are a union in spirit: only the one matching `type`
// is ever filled, and so are the weather fields, which only a generic page carries.
struct Page {
  char type = 0;  // 'b' board, 'a' agenda, 'g' generic
  std::string id, title, module, mode;
  // A board in one name, for a header with no room for the whole title. Empty when the backend
  // had nothing to shorten, in which case the title is what a face shows.
  std::string short_title;
  uint32_t asof = 0;
  bool stale = false;
  std::vector<BoardRow> rows;
  std::vector<AgendaEvent> events;
  std::vector<GenericRow> grows;
  // The weather face's own fields, all optional. S7 draws them; the clock uses `temp`.
  std::string place, temp, feels, head, sent;
  std::vector<HourSlot> hours;
};

// Night dimming window. `from` and `to` are local "HH:MM"; from > to wraps past midnight.
struct NightWindow {
  bool enabled = false;
  int from = 0, to = 0, brightness = 20;
};

struct Settings {
  int brightness = 100;
  NightWindow night;
};

struct Document {
  uint32_t gen = 0;
  std::string tz;    // the household's IANA zone, informational
  std::string tzp;   // the same zone as a POSIX TZ string, which is what the clock wants
  std::string name;  // the household's name for this board, shown on the boot support line
  // A short line the backend wants the household to read (a payment problem, say), shown on the
  // clock page. It lasts until a document says otherwise, so an empty string is how it clears.
  std::string notice;
  std::vector<Page> pages;
  Settings settings;
};

// "HH:MM" to minutes past midnight, or -1 when it is not a time.
inline int hhmm_to_minutes(const std::string &s) {
  if (s.size() != 5 || s[2] != ':') return -1;
  for (int i : {0, 1, 3, 4})
    if (s[i] < '0' || s[i] > '9') return -1;
  int h = (s[0] - '0') * 10 + (s[1] - '0');
  int m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h > 23 || m > 59) return -1;
  return h * 60 + m;
}

// ---------------------------------------------------------------- fonts and icons
// The design system's type scale: Figtree for text, IBM Plex Mono for times, codes and meta.
// Set once on boot from the `font:` entries in ui.yaml (hidden anchor labels there are what
// compiles each one in); the host simulator fills the same members from TTFs.
struct FontSet {
  const lv_font_t *clock132 = nullptr;   // Figtree 600, the clock face
  const lv_font_t *hero88 = nullptr;     // Figtree 600, the temperature hero
  const lv_font_t *sans600_30 = nullptr;
  const lv_font_t *sans600_20 = nullptr;
  const lv_font_t *sans500_22 = nullptr;
  const lv_font_t *sans500_20 = nullptr;
  const lv_font_t *sans500_18 = nullptr;
  const lv_font_t *sans500_16 = nullptr;
  const lv_font_t *sans400_18 = nullptr;
  const lv_font_t *mark50 = nullptr;     // Figtree 700, the two letters of the mark
  const lv_font_t *mono64 = nullptr;     // IBM Plex Mono 400, the pairing code
  const lv_font_t *mono20 = nullptr;
  const lv_font_t *mono16 = nullptr;
  const lv_font_t *mono15 = nullptr;
  const lv_font_t *mono14 = nullptr;
  const lv_font_t *icon = nullptr;       // 48 px Material Design Icons, until S7 replaces them
};
inline FontSet g_fonts;

// A font slot that has not been filled yet draws in whatever ui.yaml made the default, which is
// readable at any size rather than a screen of missing-glyph boxes.
inline const lv_font_t *F(const lv_font_t *f) { return f != nullptr ? f : LV_FONT_DEFAULT; }

// Letter spacing, in whole pixels. The mono sizes need a little air, the display sizes need
// taking in; the amounts are the design's.
inline void tracked(lv_obj_t *o, int px) { lv_obj_set_style_text_letter_space(o, px, 0); }

// The icon names the document may use, in the order docs/screen-document.md lists them, mapped to
// Material Design Icons codepoints as UTF-8. An unknown name renders as nothing.
struct IconEntry {
  const char *name;
  const char *glyph;
};
inline const IconEntry ICONS[] = {
    {"sun", "\xF3\xB0\x96\x99"},       // U+F0599 weather-sunny
    {"partly", "\xF3\xB0\x96\x95"},    // U+F0595 weather-partly-cloudy
    {"cloud", "\xF3\xB0\x96\x90"},     // U+F0590 weather-cloudy
    {"rain", "\xF3\xB0\x96\x97"},      // U+F0597 weather-rainy
    {"pour", "\xF3\xB0\x96\x96"},      // U+F0596 weather-pouring
    {"snow", "\xF3\xB0\x96\x98"},      // U+F0598 weather-snowy
    {"fog", "\xF3\xB0\x96\x91"},       // U+F0591 weather-fog
    {"storm", "\xF3\xB0\x96\x93"},     // U+F0593 weather-lightning
    {"wind", "\xF3\xB0\x96\x9D"},      // U+F059D weather-windy
    {"night", "\xF3\xB0\x96\x94"},     // U+F0594 weather-night
    {"bell", "\xF3\xB0\x82\x9C"},      // U+F009C bell-outline
    {"flag", "\xF3\xB0\x88\xBD"},      // U+F023D flag-outline
    {"calendar", "\xF3\xB0\x83\xAE"},  // U+F00EE calendar-blank
    {"train", "\xF3\xB0\x94\xAC"},     // U+F052C train
    {"tube", "\xF3\xB0\x93\x9F"},      // U+F04DF subway-variant
    {"bus", "\xF3\xB0\x83\xA7"},       // U+F00E7 bus
    {"clock", "\xF3\xB0\x85\x90"},     // U+F0150 clock-outline
    {"alert", "\xF3\xB0\x97\x96"},     // U+F05D6 alert-circle-outline
    {"wifi", "\xF3\xB0\x96\xA9"},      // U+F05A9 wifi
    {"link", "\xF3\xB0\x8C\xB9"},      // U+F0339 link-variant
};

inline const char *icon_glyph(const std::string &name) {
  for (const auto &e : ICONS)
    if (name == e.name) return e.glyph;
  return "";
}

// ---------------------------------------------------------------- shared look
// The document's one-letter row colour, as a token. W (and anything unknown) is not a state, so
// it reads as ordinary text rather than a colour the household has to decode.
inline Tok row_colour(const std::string &c) {
  if (c == "G") return T_OK;
  if (c == "R") return T_OFF;
  if (c == "Y") return T_LATE;
  return T_CHALK70;
}

// The UI never calls ESPHome scripts directly: hallboard.yaml installs a callback on boot and the
// widgets post the same messages the 1a YAML pages posted.
using EventFn = void (*)(const std::string &);
inline EventFn g_event = nullptr;
inline void emit(const std::string &msg) {
  if (g_event != nullptr) g_event(msg);
}

// Local time as the views need it, refreshed once a second by PageHost::tick.
inline std::string g_today;  // "YYYYMMDD", empty until SNTP has synced
inline std::string g_nowhm;  // "HH:MM"
// The same instant as a Unix time, 0 until SNTP has synced. The stale rule reads this rather than
// time(nullptr) so the host simulator's pinned clock decides the stamp too.
inline uint32_t g_now_epoch = 0;

// A plain, unstyled object. lv_obj_create would pick up LVGL's default theme, so every panel here
// starts from lv_container_create and sets what it needs.
inline lv_obj_t *mk_obj(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *o = esphome::lvgl::lv_container_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_outline_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

// A filled panel: a card, a dot, a bar. mk_obj sets bg_opa locally, and a local style beats an
// added one, so the opacity is set here and only the colour comes from the shared style.
inline lv_obj_t *mk_panel(lv_obj_t *parent, int x, int y, int w, int h, Tok bg, int radius) {
  lv_obj_t *o = mk_obj(parent, x, y, w, h);
  lv_obj_add_style(o, &g_bg[bg], 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  return o;
}

// The design's hairline: one pixel, not two.
inline lv_obj_t *mk_rule(lv_obj_t *parent, int x, int y, int w, Tok colour = T_LINE) {
  return mk_panel(parent, x, y, w, 1, colour, 0);
}

inline lv_obj_t *mk_label(lv_obj_t *parent, int x, int y, int w, int h, const lv_font_t *font, Tok tok,
                          const char *text = "") {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_pos(l, x, y);
  if (w > 0) lv_obj_set_width(l, w);
  if (h > 0) lv_obj_set_height(l, h);
  lv_obj_set_style_pad_all(l, 0, 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_add_style(l, &g_text[tok], 0);
  lv_label_set_text(l, text);
  return l;
}

// Recolour a label that was built with mk_label. The old style has to come off first, and which
// one it was is not recorded, so every one is asked to leave; removing a style an object does not
// have is a walk over a list of two or three entries and nothing more.
inline void set_tok(lv_obj_t *o, Tok tok) {
  for (int i = 0; i < T_COUNT; i++) lv_obj_remove_style(o, &g_text[i], 0);
  lv_obj_add_style(o, &g_text[tok], 0);
}

inline void label_text(lv_obj_t *l, const std::string &text) { lv_label_set_text(l, text.c_str()); }
inline void set_hidden(lv_obj_t *o, bool hidden) {
  if (hidden)
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// A card: the dark rounded panel boards and the agenda rest their rows on. The design has no
// coloured left edge; a row's state is in its status colour.
inline lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h) {
  return mk_panel(parent, x, y, w, h, T_CARD, 16);
}

// True when what is on screen is older than the household should read as current: the document
// said so, or nothing has been fetched for five minutes. Before the clock is set there is no way
// to tell, and a board that says LIVE too long is better than one that cries stale on every boot.
inline bool stale_now(uint32_t asof, bool stale) {
  if (stale) return true;
  if (asof == 0 || g_now_epoch == 0) return false;
  return g_now_epoch > asof + 300;
}

// A Unix time as local "HH:MM", for the stamp and the empty card's footer.
inline std::string hhmm_of(uint32_t when) {
  time_t t = (time_t) when;
  struct tm lt;
  localtime_r(&t, &lt);
  char buf[8];
  strftime(buf, sizeof buf, "%H:%M", &lt);
  return buf;
}

// The stamp in the corner of every content face: "LIVE · 08:41", "SHOWING 08:12", "LOADING".
inline std::string stamp_text(uint32_t asof, bool stale) {
  if (asof == 0) return "LOADING";
  // U+00B7, the middle dot the design separates with.
  return stale_now(asof, stale) ? "SHOWING " + hhmm_of(asof) : "LIVE \xC2\xB7 " + hhmm_of(asof);
}

// Uppercase an ASCII string, for the strip's left label and the agenda's day headings. Document
// text is ASCII by the time the parser is done with it, so there is nothing else to fold.
inline std::string upper(std::string s) {
  for (auto &c : s) c = (char) toupper((unsigned char) c);
  return s;
}

// The weekday with the day of the month, uppercase: "FRIDAY 18", or "FRI 18" when the face has
// not the room for the whole word. Empty before the clock has been set, which is the only time
// the diary has no day to name.
inline std::string long_day(const esphome::ESPTime &now, bool shortened = false) {
  static const char *DAYS[] = {"SUNDAY",   "MONDAY", "TUESDAY", "WEDNESDAY",
                               "THURSDAY", "FRIDAY", "SATURDAY"};
  int dow = (int) now.day_of_week - 1;
  if (!now.is_valid() || dow < 0 || dow > 6) return "";
  std::string day = DAYS[dow];
  if (shortened) day = day.substr(0, 3);
  return day + " " + std::to_string((int) now.day_of_month);
}

// The full weekday behind a day heading such as "Sat 19 Sept", uppercase. A heading the table
// does not recognise is used as it stands, which is at least the right day.
inline std::string long_weekday(const std::string &w) {
  static const char *ABBR[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char *FULL[] = {"SUNDAY",   "MONDAY", "TUESDAY", "WEDNESDAY",
                               "THURSDAY", "FRIDAY", "SATURDAY"};
  std::string u = upper(w);
  if (u.size() >= 3)
    for (int i = 0; i < 7; i++)
      if (u.compare(0, 3, ABBR[i]) == 0) return FULL[i];
  return u;
}

// "YYYYMMDD" a day later. Midday, so no daylight-saving shift can move the date.
inline std::string day_after(const std::string &ymd) {
  if (ymd.size() != 8) return "";
  int n[8];
  for (int i = 0; i < 8; i++) {
    if (ymd[i] < '0' || ymd[i] > '9') return "";
    n[i] = ymd[i] - '0';
  }
  struct tm t = {};
  t.tm_year = n[0] * 1000 + n[1] * 100 + n[2] * 10 + n[3] - 1900;
  t.tm_mon = n[4] * 10 + n[5] - 1;
  t.tm_mday = n[6] * 10 + n[7] + 1;
  t.tm_hour = 12;
  t.tm_isdst = -1;
  if (mktime(&t) == (time_t) -1) return "";
  char buf[12];
  strftime(buf, sizeof buf, "%Y%m%d", &t);
  return buf;
}

// How long an event has left before it starts, as the raised diary row says it. Empty when
// either time is unreadable, which is how an event with no start says it has nothing to count.
inline std::string starts_in(const std::string &t) {
  int at = hhmm_to_minutes(t), now = hhmm_to_minutes(g_nowhm);
  if (at < 0 || now < 0) return "";
  int m = at - now;
  if (m <= 0) return "now";
  if (m == 1) return "in 1 minute";
  if (m < 60) return "in " + std::to_string(m) + " minutes";
  int h = (m + 30) / 60;
  return h <= 1 ? "in 1 hour" : "in " + std::to_string(h) + " hours";
}

// "30m", "1h 15m", "2h", for a resting diary row. Empty when either end is not a time, which is
// what an event ending on another day (`u` is "Sat 09:00") comes in as.
inline std::string duration_text(const std::string &t, const std::string &u) {
  int a = hhmm_to_minutes(t), b = hhmm_to_minutes(u);
  if (a < 0 || b < 0 || b <= a) return "";
  int m = b - a, h = m / 60, rest = m % 60;
  if (h == 0) return std::to_string(m) + "m";
  return rest == 0 ? std::to_string(h) + "h" : std::to_string(h) + "h " + std::to_string(rest) + "m";
}

// True when a document's big value really is a number, which is what lets an older weather page
// stand in for a missing `temp`.
inline bool is_number(const std::string &v) {
  size_t i = (!v.empty() && v[0] == '-') ? 1 : 0;
  if (i >= v.size()) return false;
  for (; i < v.size(); i++)
    if (v[i] < '0' || v[i] > '9') return false;
  return true;
}

// Swap a panel between a filled card and nothing at all, which is what tells a raised row from a
// resting one. The old fill has to come off first and which one it was is not recorded.
inline void set_fill(lv_obj_t *o, Tok tok, bool filled, int radius) {
  for (int i = 0; i < T_COUNT; i++) lv_obj_remove_style(o, &g_bg[i], 0);
  lv_obj_set_style_radius(o, filled ? radius : 0, 0);
  if (!filled) {
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return;
  }
  lv_obj_add_style(o, &g_bg[tok], 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

// OfflineCard: the one card a face shows when it has nothing to draw, or nothing to draw it
// from. A mono label, a sentence which is what sets the card's height, and a mono footer.
class EmptyCard {
 public:
  void build(lv_obj_t *parent, int x, int y, int w) {
    if (root_ != nullptr) return;
    root_ = mk_panel(parent, x, y, w, TEXT_Y + 56, T_DONEBG, 20);
    label_ = mk_label(root_, PAD, PAD, w - 2 * PAD, 20, F(g_fonts.mono15), T_CHALK50, "");
    tracked(label_, 1);
    text_ = mk_label(root_, PAD, TEXT_Y, w - 2 * PAD, 0, F(g_fonts.sans400_18), T_CHALK70, "");
    lv_label_set_long_mode(text_, LV_LABEL_LONG_MODE_WRAP);
    foot_ = mk_label(root_, PAD, TEXT_Y + 36, w - 2 * PAD, 20, F(g_fonts.mono15), T_CHALK50, "");
    tracked(foot_, 1);
    set_hidden(root_, true);
  }

  // The card's height follows its sentence, which is why the footer is placed after the text has
  // been laid out and not before.
  void set(const std::string &label, const std::string &text, const std::string &foot) {
    if (root_ == nullptr) return;
    lv_label_set_text(label_, label.c_str());
    lv_label_set_text(text_, text.c_str());
    lv_label_set_text(foot_, foot.c_str());
    set_hidden(foot_, foot.empty());
    set_hidden(root_, false);
    lv_obj_update_layout(root_);
    int th = lv_obj_get_height(text_);
    lv_obj_set_pos(foot_, PAD, TEXT_Y + th + 12);
    lv_obj_set_height(root_, TEXT_Y + th + (foot.empty() ? 0 : 32) + PAD);
  }
  void hide() {
    if (root_ != nullptr) set_hidden(root_, true);
  }

 private:
  // 24 of padding, the 20 px label and the design's 12 px gap.
  static const int PAD = 24, TEXT_Y = 56;

  lv_obj_t *root_ = nullptr, *label_ = nullptr, *text_ = nullptr, *foot_ = nullptr;
};

// ---------------------------------------------------------------- chrome
// The band across the top of every face: a short line on the left, optionally with a hue dot in
// front of it, and a stamp or a time on the right. 28 px tall, inset by the face's margin.
class StatusStrip {
 public:
  void build(lv_obj_t *parent, int margin) {
    if (root_ != nullptr) return;
    int w = 480 - 2 * margin;
    root_ = mk_obj(parent, margin, margin, w, 28);
    dot_ = mk_panel(root_, 0, 10, 9, 9, T_LIFT, LV_RADIUS_CIRCLE);
    set_hidden(dot_, true);
    // The right label takes the last 160 px, so the left one stops ten pixels short of it.
    left_ = mk_label(root_, 0, 4, w - 170, 22, F(g_fonts.mono15), T_CHALK70, "");
    tracked(left_, 1);
    lv_label_set_long_mode(left_, LV_LABEL_LONG_MODE_DOTS);
    right_ = mk_label(root_, w - 160, 4, 160, 22, F(g_fonts.mono15), T_CHALK70, "");
    tracked(right_, 1);
    lv_obj_set_style_text_align(right_, LV_TEXT_ALIGN_RIGHT, 0);
  }

  // A face that wants its title here rather than a meta line swaps the left font once.
  void set_left_font(const lv_font_t *f) {
    if (left_ == nullptr) return;
    lv_obj_set_style_text_font(left_, f, 0);
    tracked(left_, 0);
  }
  // The colour is only swapped when it has really changed: the right label is rewritten once a
  // second and a style swap is a walk over every token.
  void set_left(const std::string &text, Tok tok = T_CHALK70) {
    if (left_ == nullptr) return;
    lv_label_set_text(left_, text.c_str());
    if (tok == left_tok_) return;
    left_tok_ = tok;
    set_tok(left_, tok);
  }
  void set_right(const std::string &text, Tok tok = T_CHALK70) {
    if (right_ == nullptr) return;
    lv_label_set_text(right_, text.c_str());
    if (tok == right_tok_) return;
    right_tok_ = tok;
    set_tok(right_, tok);
  }
  // A hue in front of the left line, or none at all. The left line shifts to make room for it.
  void set_dot(Tok tok, bool shown = true) {
    if (dot_ == nullptr) return;
    set_hidden(dot_, !shown);
    if (shown) {
      for (int i = 0; i < T_COUNT; i++) lv_obj_remove_style(dot_, &g_bg[i], 0);
      lv_obj_add_style(dot_, &g_bg[tok], 0);
      lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);
    }
    lv_obj_set_x(left_, shown ? 18 : 0);
    set_breathing(shown && breathing_);
  }
  void clear_dot() { set_dot(T_LIFT, false); }

  // The live dot breathes over four seconds. A stale board stops it: nothing about that screen
  // is happening now.
  void set_breathing(bool on) {
    breathing_ = on;
    if (dot_ == nullptr) return;
    lv_anim_delete(dot_, anim_bg_opa_cb_);
    lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);
    if (!on) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot_);
    lv_anim_set_exec_cb(&a, anim_bg_opa_cb_);
    lv_anim_set_duration(&a, 2000);
    lv_anim_set_playback_duration(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_values(&a, LV_OPA_COVER, 110);
    lv_anim_start(&a);
  }

  lv_obj_t *root() const { return root_; }

 private:
  static void anim_bg_opa_cb_(void *var, int32_t v) {
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(var), (lv_opa_t) v, 0);
  }

  lv_obj_t *root_ = nullptr, *dot_ = nullptr, *left_ = nullptr, *right_ = nullptr;
  Tok left_tok_ = T_CHALK70, right_tok_ = T_CHALK70;
  bool breathing_ = false;
};

// The page indicator along the bottom: one dot per face, the current one a capsule. Hidden until
// something the household did brings it up, then gone again two seconds later.
class FaceDots {
 public:
  void build(lv_obj_t *parent) {
    if (root_ != nullptr) return;
    root_ = mk_obj(parent, 0, 480 - 16 - DOT, 480, DOT);
    lv_obj_set_style_opa(root_, LV_OPA_TRANSP, 0);
    set_hidden(root_, true);
  }
  void destroy() {
    root_ = nullptr;   // the parent deleted it with everything else hanging off the host
    dots_.clear();
    active_ = 0;
  }

  void rebuild(size_t count) {
    if (root_ == nullptr) return;
    while (dots_.size() > count) {
      lv_obj_delete(dots_.back());
      dots_.pop_back();
    }
    while (dots_.size() < count) dots_.push_back(mk_panel(root_, 0, 0, DOT, DOT, T_FIELD, LV_RADIUS_CIRCLE));
    if (active_ >= dots_.size()) active_ = 0;
    layout_();
  }
  void set_active(size_t i) {
    if (i >= dots_.size()) return;
    active_ = i;
    layout_();
  }

  // Full opacity now, then a fade that starts two seconds from now. Any earlier fade is dropped,
  // so a second swipe restarts the two seconds rather than fading half way through.
  void show() {
    if (root_ == nullptr || dots_.size() < 2) return;
    lv_anim_delete(root_, anim_opa_cb_);
    set_hidden(root_, false);
    lv_obj_set_style_opa(root_, LV_OPA_COVER, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root_);
    lv_anim_set_exec_cb(&a, anim_opa_cb_);
    lv_anim_set_delay(&a, 2000);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_start(&a);
  }
  void raise() {
    if (root_ != nullptr) lv_obj_move_foreground(root_);
  }

 private:
  static const int DOT = 5, GAP = 8, WIDE = 22;

  void layout_() {
    int total = 0;
    for (size_t i = 0; i < dots_.size(); i++) total += (i == active_ ? WIDE : DOT) + (i ? GAP : 0);
    int x = (480 - total) / 2;
    for (size_t i = 0; i < dots_.size(); i++) {
      int w = i == active_ ? WIDE : DOT;
      lv_obj_set_pos(dots_[i], x, 0);
      lv_obj_set_width(dots_[i], w);
      for (int t = 0; t < T_COUNT; t++) lv_obj_remove_style(dots_[i], &g_bg[t], 0);
      lv_obj_add_style(dots_[i], &g_bg[i == active_ ? T_LIFT : T_FIELD], 0);
      lv_obj_set_style_bg_opa(dots_[i], LV_OPA_COVER, 0);
      x += w + GAP;
    }
  }
  static void anim_opa_cb_(void *var, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(var), (lv_opa_t) v, 0);
  }

  lv_obj_t *root_ = nullptr;
  std::vector<lv_obj_t *> dots_;
  size_t active_ = 0;
};

// ---------------------------------------------------------------- views
class PageView {
 public:
  PageView(lv_obj_t *parent, std::string id, char type)
      : id_(std::move(id)), type_(type), margin_(type == 'b' ? 36 : 24) {
    root_ = mk_panel(parent, 0, 0, 480, 480, T_NIGHT, 0);
  }
  PageView(const PageView &) = delete;
  PageView &operator=(const PageView &) = delete;
  virtual ~PageView() {
    if (root_ != nullptr) lv_obj_delete(root_);
  }

  virtual void apply(const Page &pg) {}
  virtual void tick(esphome::ESPTime now) {}

  // A transient line (Wi-Fi, backend errors). A content face shows problems only: an
  // informational status is dropped, because the stamp already says how old what is on screen
  // is, which is all such a face would gain from one. The clock and the pairing page, which have
  // no stamp, override this.
  virtual void set_status(const std::string &msg, bool problem) {
    if (!problem) return;
    show_problem_(msg);
  }

  lv_obj_t *root() const { return root_; }
  const std::string &id() const { return id_; }
  char type() const { return type_; }
  // The document's module id, kept so PageHost can tell a weather page from a to-do list when it
  // reconciles: both are generic pages and they are not the same face.
  const std::string &module() const { return module_; }
  void set_module(const std::string &m) { module_ = m; }

 protected:
  // The chrome the generic template shares with the board: the strip, with the page title on the
  // left and the stamp (or, until one arrives, the clock) on the right.
  void build_header_(const char *title) {
    strip_.build(root_, margin_);
    strip_.set_left_font(F(g_fonts.sans600_20));
    strip_.set_left(title, T_CHALK);
    build_problem_();
  }
  // A problem worth support seeing, on one line just above the page dots, as the board has it.
  void build_problem_() {
    if (status_ != nullptr) return;
    status_ = mk_label(root_, margin_, 424, 480 - 2 * margin_, 20, F(g_fonts.mono15), T_CHALK50, "");
    tracked(status_, 1);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(status_, status_text_.c_str());
    set_hidden(status_, status_text_.empty());
  }
  void show_problem_(const std::string &msg) {
    if (status_text_ == msg) return;
    status_text_ = msg;
    if (status_ == nullptr) return;
    lv_label_set_text(status_, msg.c_str());
    set_hidden(status_, msg.empty());
  }
  void set_title_(const std::string &title) { strip_.set_left(title, T_CHALK); }
  // The freshness stamp owns the right of the strip once a document has been through the page.
  // Live is the quieter tone of the two, as it is on the board: a stamp that has gone to
  // SHOWING is the one worth reading.
  void set_stamp_(uint32_t asof, bool stale) {
    stamp_ = stamp_text(asof, stale);
    strip_.set_right(stamp_, stale_now(asof, stale) ? T_CHALK70 : T_CHALK50);
  }
  void tick_header_(const esphome::ESPTime &now) {
    if (!stamp_.empty()) return;   // a stamp says more than the time does
    strip_.set_right(now.is_valid() ? g_nowhm : std::string("--:--"), T_CHALK70);
  }

  lv_obj_t *root_ = nullptr;
  lv_obj_t *status_ = nullptr;
  StatusStrip strip_;
  std::string id_, module_, status_text_, stamp_;
  char type_;
  int margin_;
};

// ---- clock: always page one. The short date and a breathing dot on the strip, the time and the
// long date in the middle, and at the foot the next thing in the diary with the temperature.
//
// Geometry, from ClockFace in the design system: 24 px of padding, a 28 px strip at the top and
// a foot row that ends 16 px above the page dots. The hairline sits at 403, the foot row runs
// 419 to 443, and the middle block is centred between the strip (ending at 52) and the hairline.
// The numerals stand 102 px tall and start 20 px below their label's top, which is what puts the
// clock label at 146 and the long date under it at 274.
class ClockView : public PageView {
 public:
  explicit ClockView(lv_obj_t *parent) : PageView(parent, "__clock", 'c') {
    // Nothing goes on the right of the strip: the clock underneath is the time. The lilac dot
    // breathes over four seconds, which is the whole of this face's movement.
    strip_.build(root_, margin_);
    strip_.set_dot(T_LIFT);
    strip_.set_breathing(true);
    strip_.set_left("", T_CHALK70);

    // The clock font holds digits and a colon and nothing else, so the label starts empty rather
    // than showing "--:--": four missing glyphs would draw as four boxes.
    time_ = mk_label(root_, 0, 146, 480, 0, F(g_fonts.clock132), T_CHALK, "");
    lv_obj_set_style_text_align(time_, LV_TEXT_ALIGN_CENTER, 0);
    tracked(time_, -8);
    date_ = mk_label(root_, 24, 274, 432, 28, F(g_fonts.sans500_20), T_CHALK70, WAITING);
    lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(date_, LV_LABEL_LONG_MODE_DOTS);

    rule_ = mk_rule(root_, 24, 403, 432, T_RAISED);
    dot_ = mk_panel(root_, 24, 426, 9, 9, T_CALENDAR, LV_RADIUS_CIRCLE);
    line_ = mk_label(root_, 45, 419, 339, 24, F(g_fonts.sans400_18), T_TIME2, "");
    lv_label_set_long_mode(line_, LV_LABEL_LONG_MODE_DOTS);
    temp_ = mk_label(root_, 396, 421, 60, 20, F(g_fonts.mono15), T_CHALK50, "");
    tracked(temp_, 1);
    lv_obj_set_style_text_align(temp_, LV_TEXT_ALIGN_RIGHT, 0);
    refresh_foot_();
  }

  void tick(esphome::ESPTime now) override {
    if (!now.is_valid()) {
      lv_label_set_text(time_, "");
      lv_label_set_text(date_, WAITING);
      strip_.set_left("", T_CHALK70);
      last_hm_.clear();
      return;
    }
    if (g_nowhm == last_hm_) return;
    last_hm_ = g_nowhm;
    lv_label_set_text(time_, short_time(g_nowhm).c_str());
    lv_label_set_text(date_, date_text(now).c_str());
    strip_.set_left(short_date(now), T_CHALK70);
  }

  // 24 hour without a leading zero, as the design has it: "8:41", "17:05".
  static std::string short_time(const std::string &hm) {
    return (hm.size() == 5 && hm[0] == '0') ? hm.substr(1) : hm;
  }

  // "Monday 14 September". Built by hand: newlib's strftime has no day-without-padding format.
  static std::string date_text(const esphome::ESPTime &now) {
    static const char *DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};
    int dow = (int) now.day_of_week - 1;
    int mon = (int) now.month - 1;
    if (dow < 0 || dow > 6 || mon < 0 || mon > 11) return "";
    return std::string(DAYS[dow]) + " " + std::to_string((int) now.day_of_month) + " " + MONTHS[mon];
  }

  // "FRI 18 SEP" for the strip, from the same tick.
  static std::string short_date(const esphome::ESPTime &now) {
    static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    int dow = (int) now.day_of_week - 1;
    int mon = (int) now.month - 1;
    if (dow < 0 || dow > 6 || mon < 0 || mon > 11) return "";
    return std::string(DAYS[dow]) + " " + std::to_string((int) now.day_of_month) + " " + MONTHS[mon];
  }

  // The foot row's two halves, both worked out by PageHost from the document: the next thing in
  // the diary and the temperature. Either may be empty, and when both are the row goes.
  void set_line(const std::string &next, const std::string &temp) {
    if (next == next_text_ && temp == temp_text_) return;
    next_text_ = next;
    temp_text_ = temp;
    refresh_foot_();
  }
  // A firmware notice ("Updating firmware 12%", "Updated to 1.3.0") takes the foot row for as
  // long as it is set, then hands it back. Nothing else interrupts the clock.
  void set_notice(const std::string &msg) {
    if (notice_ == msg) return;
    notice_ = msg;
    refresh_foot_();
  }
  // The document's own notice, from `settings.notice`. Unlike a problem it survives the next
  // document: only another document (or an empty notice in one) takes it away.
  void set_doc_notice(const std::string &msg) {
    if (doc_notice_ == msg) return;
    doc_notice_ = msg;
    refresh_foot_();
  }
  // Informational statuses are dropped: the clock page stays a clock. A problem is worth the
  // foot row, cleared by clear_problem() as soon as a document arrives.
  void set_status(const std::string &msg, bool problem) override {
    if (!problem) return;
    if (problem_text_ == msg) return;
    problem_text_ = msg;
    refresh_foot_();
  }
  void clear_problem() {
    if (problem_text_.empty()) return;
    problem_text_.clear();
    refresh_foot_();
  }

 private:
  static constexpr const char *WAITING = "Setting the clock.";

  // One row at the foot of the face, and three things that want it. A firmware update is the
  // loudest, then the document's notice, then a live problem; while any of them applies the
  // diary dot and the temperature go and what is left is a grey line.
  void refresh_foot_() {
    const std::string &msg = !notice_.empty()       ? notice_
                             : !doc_notice_.empty() ? doc_notice_
                                                    : problem_text_;
    if (!msg.empty()) {
      lv_obj_set_pos(line_, 24, 419);
      lv_obj_set_width(line_, 432);
      lv_label_set_text(line_, msg.c_str());
      set_tok(line_, T_CHALK70);
      set_hidden(line_, false);
      set_hidden(dot_, true);
      set_hidden(temp_, true);
      set_hidden(rule_, false);
      return;
    }
    lv_obj_set_pos(line_, 45, 419);
    lv_obj_set_width(line_, 339);
    lv_label_set_text(line_, next_text_.c_str());
    set_tok(line_, T_TIME2);
    set_hidden(line_, next_text_.empty());
    set_hidden(dot_, next_text_.empty());
    lv_label_set_text(temp_, temp_text_.c_str());
    set_hidden(temp_, temp_text_.empty());
    set_hidden(rule_, next_text_.empty() && temp_text_.empty());
  }

  lv_obj_t *time_ = nullptr, *date_ = nullptr, *rule_ = nullptr, *dot_ = nullptr;
  lv_obj_t *line_ = nullptr, *temp_ = nullptr;
  std::string last_hm_, next_text_, temp_text_, notice_, problem_text_, doc_notice_;
};

// ---- pairing: the only page besides the clock while the device is unclaimed. The QR encodes the
// short-lived code and nothing about the device.
//
// Geometry, from PairFace in the design system: 40 px of padding and a centred column with a 24 px
// gap, the code above the QR tile above the caption. The tile is 200 px rather than the design's
// 160, so the code still scans from the far side of a hall. There is no title: the mark is on the
// boot screen and the caption names the site. The column is placed as a block, which is what keeps
// the waiting state (no code, no tile) centred rather than stranded at the foot of the page.
class PairingView : public PageView {
 public:
  explicit PairingView(lv_obj_t *parent) : PageView(parent, "__pair", 'p') {
    code_ = mk_label(root_, PAD, 0, 480 - 2 * PAD, 0, F(g_fonts.mono64), T_CHALK, "");
    lv_obj_set_style_text_align(code_, LV_TEXT_ALIGN_CENTER, 0);
    // 0.12em at 64 px, and no inserted space: the tracking does the separating. LVGL trims the
    // trailing letter space before it centres a line (lv_text.c), so the design's compensating
    // left padding is not wanted here, it would push the code half a tracking unit right.
    tracked(code_, 8);
    // The quiet zone is part of the tile: 12 px of chalk around the code, inside 200 px of it.
    tile_ = mk_panel(root_, (480 - TILE) / 2, 0, TILE, TILE, T_CHALK, 16);
    qr_ = lv_qrcode_create(tile_);
    lv_qrcode_set_size(qr_, TILE - 2 * QUIET);
    lv_qrcode_set_dark_color(qr_, col(T_NIGHT));
    lv_qrcode_set_light_color(qr_, col(T_CHALK));
    lv_obj_set_pos(qr_, QUIET, QUIET);
    caption_ = mk_label(root_, (480 - CAPTION_W) / 2, 0, CAPTION_W, 0, F(g_fonts.sans500_22),
                        T_CHALK70, "");
    lv_obj_set_style_text_align(caption_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(caption_, LV_LABEL_LONG_MODE_WRAP);
    set_code("");
  }

  // The pairing URL is the contract the portal implements. Nothing but the code goes into it.
  void set_code(const std::string &raw) {
    // Only the pairing alphabet may reach the URL: uppercase letters and digits, six of them.
    // The backend is trusted, but nothing lifted from a response goes into a URL unchecked.
    std::string code;
    for (char c : raw) {
      if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) code += c;
      if (code.size() >= 6) break;
    }
    if (code_shown_ == code) return;
    code_shown_ = code;
    if (code.size() != 6) {
      qr_url_.clear();
      set_hidden(tile_, true);
      set_hidden(code_, true);
      lv_label_set_text(code_, "");
    } else {
      // The code rides in the fragment: the portal reads it in the browser and it never
      // reaches a server log line.
      qr_url_ = "https://www.hallboard.co.uk/pair#code=" + code;
      lv_qrcode_update(qr_, qr_url_.c_str(), (uint32_t) qr_url_.size());
      set_hidden(tile_, false);
      set_hidden(code_, false);
      lv_label_set_text(code_, code.c_str());
    }
    refresh_caption_();
  }

  // A problem takes the caption for as long as it lasts, which is where the Wi-Fi line this page
  // used to carry has gone. An informational status is dropped, as it is on a content face.
  void set_status(const std::string &msg, bool problem) override {
    if (!problem || problem_ == msg) return;
    problem_ = msg;
    refresh_caption_();
  }

  // What the QR carries. The host simulator prints it so the fragment contract stays checked;
  // nothing on the device reads it, and it is never logged.
  const std::string &qr_url() const { return qr_url_; }

 private:
  static const int PAD = 40, TILE = 200, QUIET = 12, GAP = 24;
  // The design's 360, widened to the full content width. LVGL breaks a line at a full stop as
  // readily as at a space, and at 360 that split hallboard.co.uk across two lines.
  static const int CAPTION_W = 400;
  static constexpr const char *SCAN = "Scan, or type it at hallboard.co.uk/pair";
  static constexpr const char *WAITING = "Getting a pairing code.";

  // One slot, and three things that want it: a problem first, then the caption for whichever of
  // the two states the page is in.
  void refresh_caption_() {
    const char *text = WAITING;
    if (!problem_.empty())
      text = problem_.c_str();
    else if (code_shown_.size() == 6)
      text = SCAN;
    lv_label_set_text(caption_, text);
    relayout_();
  }

  // The column is centred as a block, so what is on screen is what decides where it starts. Both
  // the code and the caption are as tall as their text, which is why the heights are read back.
  void relayout_() {
    lv_obj_update_layout(root_);
    bool has_code = !lv_obj_has_flag(code_, LV_OBJ_FLAG_HIDDEN);
    int total = lv_obj_get_height(caption_);
    if (has_code) total += lv_obj_get_height(code_) + GAP + TILE + GAP;
    int y = (480 - total) / 2;
    if (has_code) {
      lv_obj_set_y(code_, y);
      y += lv_obj_get_height(code_) + GAP;
      lv_obj_set_y(tile_, y);
      y += TILE + GAP;
    }
    lv_obj_set_y(caption_, y);
  }

  lv_obj_t *tile_ = nullptr, *qr_ = nullptr, *code_ = nullptr, *caption_ = nullptr;
  std::string code_shown_ = "\x01";  // never a valid code, so the first set_code always draws
  std::string problem_, qr_url_;
};

// ---- boot: what the board shows from power-on until it is ready. Not a PageView and never in
// views_: it is an opaque black overlay owned by PageHost that sits on top of the carousel, so
// nothing behind it is visible while the screen settles, the wordmark rises, and the four steps
// tick off in order. Deleted the tick after its fade completes, which puts the LVGL object count
// back where it was.
//
// Every event only sets a flag; the state machine runs from the 100 ms tick and writes at most one
// line per pass, each write holding the next transition for 600 ms. That is what makes the steps
// readable when SNTP finishes before the fetch does, which on a warm boot it usually does.
//
// Strings here are ASCII plus the middle dot U+00B7, which is in the text fonts' glyph list: no
// ellipsis, no em dash. Three dots is three dots.
class BootView {
 public:
  BootView(lv_obj_t *parent, std::string fw) : fw_(std::move(fw)) {
    // The boot screen sits on bezel, a shade under the page, so the panel's edge disappears.
    root_ = mk_panel(parent, 0, 0, 480, 480, T_BEZEL, 0);
    // Swallow touches: nothing behind the overlay should react while it is up, and the gesture
    // must not bubble to the host or a swipe would page the carousel underneath.
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // The design's column: the 108 px mark, a 36 px gap, then the four steps 12 px apart, centred
    // on the face. The three lines below it are not in the design's BootFace, so the block is
    // nudged up by however much the two-line help slot wants, which on this font set is 18 px.
    const int step_h = lv_font_get_line_height(F(g_fonts.mono16));
    const int list_h = STEPS * step_h + (STEPS - 1) * STEP_GAP;
    const int meta_h = lv_font_get_line_height(F(g_fonts.mono15));
    const int help_h = 2 * lv_font_get_line_height(F(g_fonts.sans400_18));
    const int support_y = 480 - 16 - meta_h;
    const int detail_y = support_y - 4 - meta_h;
    const int help_y = detail_y - 8 - help_h;
    int top = (480 - (MARK + COL_GAP + list_h)) / 2;
    const int over = top + MARK + COL_GAP + list_h + 12 - help_y;
    if (over > 0) top -= over;
    const int list_y = top + MARK + COL_GAP;

    logo_ = make_logo_(root_, top);
    // The firmware notice keeps its slot between the mark and the steps.
    notice_ = mk_label(root_, 40, top + MARK + 6, 400, 0, F(g_fonts.sans400_18), T_CHALK, "");
    lv_obj_set_style_text_align(notice_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(notice_, LV_LABEL_LONG_MODE_DOTS);
    for (int i = 0; i < STEPS; i++) {
      steps_[i] = mk_label(root_, 24, list_y + i * (step_h + STEP_GAP), 432, step_h,
                           F(g_fonts.mono16), T_PENDING, "");
      lv_obj_set_style_text_align(steps_[i], LV_TEXT_ALIGN_CENTER, 0);
      tracked(steps_[i], 1);
      lv_label_set_long_mode(steps_[i], LV_LABEL_LONG_MODE_DOTS);
    }
    help_ = mk_label(root_, 40, help_y, 400, help_h, F(g_fonts.sans400_18), T_CHALK70, "");
    lv_obj_set_style_text_align(help_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(help_, LV_LABEL_LONG_MODE_WRAP);
    detail_ = mk_label(root_, 24, detail_y, 432, meta_h, F(g_fonts.mono15), T_CHALK50, "");
    lv_obj_set_style_text_align(detail_, LV_TEXT_ALIGN_CENTER, 0);
    tracked(detail_, 1);
    lv_label_set_long_mode(detail_, LV_LABEL_LONG_MODE_DOTS);
    support_ = mk_label(root_, 24, support_y, 432, meta_h, F(g_fonts.mono15), T_CHALK50, "");
    lv_obj_set_style_text_align(support_, LV_TEXT_ALIGN_CENTER, 0);
    tracked(support_, 1);
    lv_label_set_long_mode(support_, LV_LABEL_LONG_MODE_DOTS);
    refresh_support_();
    s_fade_done = false;
  }
  BootView(const BootView &) = delete;
  BootView &operator=(const BootView &) = delete;
  ~BootView() {
    if (root_ != nullptr) lv_obj_delete(root_);
  }

  // The mark: a violet tile carrying the two letters the 50 px face holds and the lilac bar under
  // them, at the design's proportions on a 108 px tile. Opacity is inherited, so fading the tile
  // fades its two children with it. It rises 24 px and fades in over 400 ms, which is the whole
  // of this screen's movement.
  static lv_obj_t *make_logo_(lv_obj_t *parent, int y) {
    lv_obj_t *tile = mk_panel(parent, (480 - MARK) / 2, y + 24, MARK, MARK, T_VIOLET, 27);
    const lv_font_t *f = F(g_fonts.mark50);
    // The design sets the letters at 49.68 px with -0.05em of tracking, which puts their baseline
    // 69 px down the tile. LVGL hangs a label from the top of its line box, so that is what the
    // offset is worked back from rather than being written down as a top edge.
    lv_obj_t *letters = mk_label(tile, 0, 69 - (lv_font_get_line_height(f) - f->base_line), MARK, 0,
                                 f, T_WHITE, "HB");
    lv_obj_set_style_text_align(letters, LV_TEXT_ALIGN_CENTER, 0);
    tracked(letters, -3);
    mk_panel(tile, 25, 78, 58, 8, T_LIFT, LV_RADIUS_CIRCLE);
    lv_obj_set_style_opa(tile, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, tile);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_opa_cb_);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_start(&a);
    lv_anim_set_exec_cb(&a, anim_y_cb_);
    lv_anim_set_values(&a, y + 24, y);
    lv_anim_start(&a);
    return tile;
  }

  lv_obj_t *root() const { return root_; }
  void raise() {
    if (root_ != nullptr) lv_obj_move_foreground(root_);
  }
  // Set by the fade's completion callback; PageHost deletes us on the next tick.
  bool finished() const { return s_fade_done; }

  // ---- events. These only record what happened; the tick decides what is on screen.
  void on_network(const std::string &ssid) {
    have_net_ = true;
    if (!ssid.empty() && ssid_ != ssid) {
      ssid_ = ssid;
      refresh_support_();
    }
  }
  void on_document(const std::string &name) {
    have_doc_ = true;
    if (!name.empty() && name_ != name) {
      name_ = name;
      refresh_support_();
    }
  }
  void on_unpaired(const std::string &code) {
    have_doc_ = true;
    unpaired_ = true;
  }
  void on_time_valid() { have_time_ = true; }
  void set_notice(const std::string &msg) {
    if (notice_text_ == msg) return;
    notice_text_ = msg;
    lv_label_set_text(notice_, msg.c_str());
  }
  void on_status(const std::string &msg, bool problem) {
    status_ = msg;
    if (!problem) return;
    problems_++;
    if (step_ <= S_WIFI) {
      // Whatever the Wi-Fi trouble is, the status string already says what to do about it.
      set_help_(msg);
    } else if (problems_ >= 2) {
      set_help_("Cannot reach hallboard.co.uk. Trying again.");
      set_detail_(msg);
    }
  }

  // ---- the state machine, one text write per pass
  void tick_fast(uint32_t now) {
    if (root_ == nullptr || s_fade_done) return;
    last_now_ = now;
    if (t0_ == 0) t0_ = now;
    if (step_ == S_FADING) return;
    if ((int32_t) (now - hold_until_) < 0) return;
    switch (step_) {
      case S_WIFI:
        if (!started_) return begin_step_(0, "Connecting to Wi-Fi");
        if (have_net_) {
          finish_step_(0, "Connected to Wi-Fi");
          return advance_(S_CONTENT);
        }
        // Twenty seconds without a network and the household needs telling how to fix it. A
        // status string that already said so wins: it is more specific than this one.
        if (help_text_.empty() && (int32_t) (now - t0_) > 20000)
          set_help_("Still looking for Wi-Fi. Hold the side button.");
        return;
      case S_CONTENT:
        if (!started_) return begin_step_(1, "Fetching your pages");
        if (unpaired_) {
          finish_step_(1, "Pages loaded");
          return advance_(S_PAIR);
        }
        if (have_doc_) {
          finish_step_(1, "Pages loaded");
          content_at_ = now;
          return advance_(S_TIME);
        }
        return;
      case S_TIME:
        if (!started_) return begin_step_(2, "Setting the clock");
        if (have_time_) {
          finish_step_(2, "Clock set");
          return advance_(S_READY);
        }
        // SNTP is not worth waiting on: the clock page carries the waiting line instead.
        if ((int32_t) (now - content_at_) > 30000) {
          set_help_("The clock is not set yet.");
          return advance_(S_READY);
        }
        return;
      case S_READY:
        if (!started_) return begin_step_(3, "Ready");
        return start_fade_();
      case S_PAIR:
        if (!started_) return begin_step_(3, "Pair this board");
        return start_fade_();
      default:
        return;
    }
  }

 private:
  enum Step { S_WIFI = 0, S_CONTENT, S_TIME, S_READY, S_PAIR, S_FADING };
  static const int STEPS = 4;
  static const int MARK = 108, COL_GAP = 36, STEP_GAP = 12;
  static const uint32_t HOLD_MS = 600;

  // Each of these writes exactly one line and holds the next transition for 600 ms. The three
  // states are told apart by colour alone, as the design has them: there is no tick glyph, and
  // the one that belonged to Montserrat is not in the mono face the steps are set in.
  void begin_step_(int i, const char *text) {
    set_tok(steps_[i], T_TITLE2);
    lv_label_set_text(steps_[i], text);
    started_ = true;
    hold_until_ = last_now_ + HOLD_MS;
  }
  void finish_step_(int i, const char *text) {
    set_tok(steps_[i], T_CHALK50);
    lv_label_set_text(steps_[i], text);
    hold_until_ = last_now_ + HOLD_MS;
  }
  void advance_(Step next) {
    step_ = next;
    started_ = false;
  }
  void set_help_(const std::string &msg) {
    if (help_text_ == msg) return;
    help_text_ = msg;
    lv_label_set_text(help_, msg.c_str());
    hold_until_ = last_now_ + HOLD_MS;
  }
  void set_detail_(const std::string &msg) {
    if (detail_text_ == msg) return;
    detail_text_ = msg;
    lv_label_set_text(detail_, msg.c_str());
  }
  // "v1.4.0 · Home Wi-Fi · Hallway", with only the parts that are known yet.
  void refresh_support_() {
    std::string s;
    if (!fw_.empty()) s = "v" + fw_;
    if (!ssid_.empty()) s += (s.empty() ? "" : " \xC2\xB7 ") + ssid_;
    if (!name_.empty()) s += (s.empty() ? "" : " \xC2\xB7 ") + name_;
    lv_label_set_text(support_, s.c_str());
  }
  // Opacity is an inherited style, so fading the root fades every child with it.
  void start_fade_() {
    step_ = S_FADING;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root_);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_exec_cb(&a, anim_opa_cb_);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_completed_cb(&a, fade_done_cb_);
    lv_anim_start(&a);
  }

  static void anim_opa_cb_(void *var, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(var), (lv_opa_t) v, 0);
  }
  static void anim_y_cb_(void *var, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t *>(var), v); }
  // There is only ever one boot view, and it is gone for good once this fires.
  static void fade_done_cb_(lv_anim_t *a) { s_fade_done = true; }
  static inline bool s_fade_done = false;

  lv_obj_t *root_ = nullptr, *logo_ = nullptr, *notice_ = nullptr, *help_ = nullptr;
  lv_obj_t *detail_ = nullptr, *support_ = nullptr;
  lv_obj_t *steps_[STEPS] = {nullptr, nullptr, nullptr, nullptr};
  std::string fw_, ssid_, name_, status_, help_text_, detail_text_, notice_text_;
  Step step_ = S_WIFI;
  bool started_ = false, have_net_ = false, have_doc_ = false, have_time_ = false, unpaired_ = false;
  int problems_ = 0;
  // last_now_ is the millis the last tick ran at: the helpers above set their hold from it, and
  // an event arriving between ticks is at most 100 ms out, which nothing here cares about.
  uint32_t t0_ = 0, hold_until_ = 0, content_at_ = 0, last_now_ = 0;
};

// ---- board: the departures or arrivals template, laid out as BoardFace in the design system.
//
// 36 px of padding, a header whose title and stamp share one baseline, then four rows. The
// design's 22 px gap between rows would put the fourth one under the page dots, so the gap is 12
// and only the header keeps its 22. That gives rows at 94 (raised, 76 tall), 182, 264 and 346
// (70 tall each, ending at 416), the problem line at 424 and the dots at 459.
//
// The parser keeps five rows because the document may carry five; the fifth is not drawn, and
// nothing can long-press a row that is not on screen.
class BoardView : public PageView {
 public:
  static const int ROWS_SHOWN = 4;

  BoardView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'b') {
    // The header is two labels rather than a StatusStrip: the design sets the title in 30 px
    // Figtree and sits the mono stamp on its baseline, which the strip's one band cannot do.
    const lv_font_t *tf = F(g_fonts.sans600_30), *sf = F(g_fonts.mono15);
    int stamp_y = HEAD_Y + (lv_font_get_line_height(tf) - tf->base_line) -
                  (lv_font_get_line_height(sf) - sf->base_line);
    title_ = mk_label(root_, PAD, HEAD_Y, 266, 40, tf, T_CHALK, "");
    lv_label_set_long_mode(title_, LV_LABEL_LONG_MODE_DOTS);
    stamp_lbl_ = mk_label(root_, 480 - PAD - 130, stamp_y, 130, 20, sf, T_CHALK50, "LOADING");
    tracked(stamp_lbl_, 1);
    lv_obj_set_style_text_align(stamp_lbl_, LV_TEXT_ALIGN_RIGHT, 0);

    for (int i = 0; i < ROWS_SHOWN; i++) build_row_(rows_[i], i);
    build_card_();

    // A problem worth support seeing, just above the dots. Informational statuses never reach it.
    build_problem_();

    // A tap anywhere refreshes; the row hit rects sit on top and add the long press. Neither is
    // scrollable, so a horizontal drag still reaches the carousel.
    lv_obj_t *tap = mk_obj(root_, 0, 0, 480, 480);
    lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap, touch_cb_, LV_EVENT_SHORT_CLICKED, nullptr);
    for (int i = 0; i < ROWS_SHOWN; i++) {
      lv_obj_t *hit = mk_obj(root_, 0, row_y(i), 480, row_h(i));
      lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(hit, (void *) (intptr_t) i);
      lv_obj_add_event_cb(hit, touch_cb_, LV_EVENT_SHORT_CLICKED, nullptr);
      lv_obj_add_event_cb(hit, long_cb_, LV_EVENT_LONG_PRESSED, nullptr);
    }
  }

  void apply(const Page &pg) override {
    arrivals_ = pg.mode == "arr";
    // The module decides the empty-state wording. A document from a backend that does not send
    // `module` is rail, which is all there was before 1.4.0.
    module_ = pg.module;
    // The header has room for a name, not a sentence, which is what `short` is for.
    const std::string &head = pg.short_title.empty() ? pg.title : pg.short_title;
    if (!head.empty()) lv_label_set_text(title_, head.c_str());
    asof_ = pg.asof;
    stale_ = pg.stale;
    show_problem_("");   // a document is the answer to whatever the last problem was about
    uids_.clear();
    int n = (int) pg.rows.size();
    if (n > ROWS_SHOWN) n = ROWS_SHOWN;
    for (int i = 0; i < ROWS_SHOWN; i++) {
      Row &r = rows_[i];
      if (i >= n) {
        set_hidden(r.box, true);
        continue;
      }
      const BoardRow &d = pg.rows[i];
      label_text(r.time, d.time);
      label_text(r.dest, d.dest);
      label_text(r.plat, d.plat);
      // The status string arrives fully composed (delay, coaches, operator, and for an arrivals
      // board the origin's booked and actual departure). `e` is the expected time on rail and
      // the wait on TfL, and goes in front of it.
      label_text(r.status, d.expected.empty() ? d.status : d.expected + " \xC2\xB7 " + d.status);
      set_tok(r.status, row_colour(d.colour));
      // A cancelled service is dimmed where it stands. It is never dropped or moved: the
      // household needs to see that the train they were going to catch is not running.
      lv_obj_set_style_opa(r.box, d.colour == "R" ? LV_OPA_60 : LV_OPA_COVER, 0);
      set_hidden(r.box, false);
      uids_.push_back(d.uid);
    }
    set_hidden(card_, n > 0);
    if (n == 0) fill_card_(pg);
    refresh_stamp_();
  }

  // The stamp is rebuilt once a minute, because a board that stops being fetched crosses the
  // five-minute line on its own and has to say so.
  void tick(esphome::ESPTime now) override {
    if (g_nowhm == last_hm_) return;
    last_hm_ = g_nowhm;
    refresh_stamp_();
  }

  const std::string &uid_at(int i) const {
    static const std::string none;
    if (i < 0 || i >= (int) uids_.size()) return none;
    return uids_[i];
  }

 private:
  // The face's geometry, from DepartureRow: rows 408 wide inside a 36 px margin, the first one
  // raised and a little taller for its heavier type.
  static const int PAD = 36, HEAD_Y = 36, ROW_X = 36, ROW_W = 408;
  static const int ROW0_Y = 94, ROW0_H = 76, ROW_H = 70, ROW_GAP = 12;

  static int row_y(int i) {
    return i == 0 ? ROW0_Y : ROW0_Y + ROW0_H + ROW_GAP + (ROW_H + ROW_GAP) * (i - 1);
  }
  static int row_h(int i) { return i == 0 ? ROW0_H : ROW_H; }

  struct Row {
    lv_obj_t *box = nullptr, *time = nullptr, *dest = nullptr, *status = nullptr, *plat = nullptr;
  };

  // Columns inside a row, measured from the row's own left edge: 16 px of padding, a 62 px time,
  // a 14 px gap, then the destination over its status, with the platform right-aligned at the
  // far end. The platform sits on the destination's line rather than the row's middle, which
  // leaves the status the full width: the composed status strings are long and the design's own
  // are not.
  void build_row_(Row &r, int i) {
    int h = row_h(i), y = row_y(i);
    bool first = i == 0;
    int pad = first ? 14 : 12, dest_h = first ? 26 : 24;
    r.box = first ? mk_panel(root_, ROW_X, y, ROW_W, h, T_RAISED, 16)
                  : mk_obj(root_, ROW_X, y, ROW_W, h);
    // The third and fourth rows carry a hairline along the top, as the design has them.
    if (i >= 2) mk_rule(r.box, 0, 0, ROW_W, T_LINE);
    // No tracking on the time, as the design has it: 62 px holds "08:47" at mono 20 and not a
    // pixel more, which is what makes the column line up down the face.
    r.time = mk_label(r.box, 16, (h - 26) / 2, 62, 26, F(g_fonts.mono20), first ? T_CHALK : T_TIME2);
    r.dest = mk_label(r.box, 92, pad, 226, dest_h, F(first ? g_fonts.sans600_20 : g_fonts.sans500_18),
                      first ? T_CHALK : T_TITLE2);
    lv_label_set_long_mode(r.dest, LV_LABEL_LONG_MODE_DOTS);
    r.status = mk_label(r.box, 92, pad + dest_h + 2, 300, 20, F(g_fonts.mono15), T_CHALK70);
    tracked(r.status, 1);
    lv_label_set_long_mode(r.status, LV_LABEL_LONG_MODE_DOTS);
    r.plat = mk_label(r.box, 332, pad + (dest_h - 20) / 2, 60, 20, F(g_fonts.mono15),
                      first ? T_CHALK70 : T_CHALK50);
    tracked(r.plat, 1);
    lv_obj_set_style_text_align(r.plat, LV_TEXT_ALIGN_RIGHT, 0);
    set_hidden(r.box, true);
  }

  // The one card an empty or unreachable board shows, where the first row would be. Its height
  // follows its sentence, which is why the footer is positioned after the text has been laid out.
  void build_card_() {
    card_ = mk_panel(root_, ROW_X, ROW0_Y, ROW_W, CARD_TEXT_Y + 24 + 24, T_DONEBG, 20);
    set_hidden(card_, true);
    card_label_ = mk_label(card_, 24, 24, ROW_W - 48, 20, F(g_fonts.mono15), T_CHALK50, "");
    tracked(card_label_, 1);
    card_text_ = mk_label(card_, 24, CARD_TEXT_Y, ROW_W - 48, 0, F(g_fonts.sans400_18), T_CHALK70, "");
    lv_label_set_long_mode(card_text_, LV_LABEL_LONG_MODE_WRAP);
    card_foot_ = mk_label(card_, 24, CARD_TEXT_Y + 36, ROW_W - 48, 20, F(g_fonts.mono15), T_CHALK50, "");
    tracked(card_foot_, 1);
  }

  void fill_card_(const Page &pg) {
    // asof is 0 only when the backend could not build this board at all. Say so rather than
    // claiming there is nothing due.
    bool missing = pg.asof == 0;
    // A stop has no window to promise: only a rail board's adapter asks for one.
    bool stop = module_ == "bus" || module_ == "tube";
    const char *sentence = missing     ? "Can't reach the timetable."
                           : stop      ? "Nothing due at this stop."
                           : arrivals_ ? "Nothing arriving in the next two hours."
                                       : "Nothing due in the next two hours.";
    lv_label_set_text(card_label_, missing ? "OFFLINE" : "NOTHING DUE");
    lv_label_set_text(card_text_, sentence);
    std::string foot = missing ? std::string() : "SHOWING " + hhmm_of(pg.asof);
    lv_label_set_text(card_foot_, foot.c_str());
    set_hidden(card_foot_, foot.empty());
    set_hidden(card_, false);
    lv_obj_update_layout(card_);
    int th = lv_obj_get_height(card_text_);
    lv_obj_set_pos(card_foot_, 24, CARD_TEXT_Y + th + 12);
    lv_obj_set_height(card_, CARD_TEXT_Y + th + (foot.empty() ? 0 : 32) + 24);
  }

  void refresh_stamp_() {
    bool old = stale_now(asof_, stale_);
    lv_label_set_text(stamp_lbl_, stamp_text(asof_, stale_).c_str());
    if (old == stamp_stale_) return;
    stamp_stale_ = old;
    set_tok(stamp_lbl_, old ? T_CHALK70 : T_CHALK50);
  }

  static void touch_cb_(lv_event_t *e) { emit("TOUCH"); }
  static void long_cb_(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target_obj(e);
    emit("LONG|" + std::to_string((int) (intptr_t) lv_obj_get_user_data(o)));
  }

  // 24 of padding, the 20 px label and the design's 12 px gap.
  static const int CARD_TEXT_Y = 56;

  Row rows_[ROWS_SHOWN];
  lv_obj_t *title_ = nullptr, *stamp_lbl_ = nullptr;
  lv_obj_t *card_ = nullptr, *card_label_ = nullptr, *card_text_ = nullptr, *card_foot_ = nullptr;
  std::vector<std::string> uids_;
  std::string last_hm_;
  uint32_t asof_ = 0;
  bool stale_ = false, stamp_stale_ = false, arrivals_ = false;
};

// ---- day: the diary, laid out as DayFace in the design system.
//
// 24 px of padding, the strip at the top carrying the day and how much of it is left, then a
// vertical scroller from 68 to 464 holding seven days of rows. The scroller takes vertical drags
// only, so a horizontal flick chains up to the carousel instead of being eaten here.
//
// Today's own heading is not drawn: the strip already names the day, and the face reads better
// opening on the next thing than on a word. Every other day keeps its heading.
//
// A row is 60 tall, the raised one 80 for its second line, with 8 between them. The raised row is
// the next thing still to come today; everything already over is dropped, as it always was.
class AgendaView : public PageView {
 public:
  static const int MAX_CARDS = 60, MAX_HEADS = 8;

  AgendaView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'a') {
    strip_.build(root_, margin_);
    list_ = mk_obj(root_, 0, LIST_Y, 480, LIST_BOTTOM - LIST_Y);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(list_, touch_cb_, LV_EVENT_SHORT_CLICKED, nullptr);
    empty_.build(root_, margin_, LIST_Y, ROW_W);
    build_problem_();
  }

  void apply(const Page &pg) override {
    events_ = pg.events;
    asof_ = pg.asof;
    stale_ = pg.stale;
    render();
  }

  // The whole face is rebuilt on the minute: which event is next, how long it is until it starts
  // and how much of today is left all move with the clock, not with the document.
  void tick(esphome::ESPTime now) override {
    if (g_nowhm == last_hm_ && day_ == long_day(now)) return;
    last_hm_ = g_nowhm;
    day_ = long_day(now);
    short_day_ = long_day(now, true);
    render();
  }

  void render() {
    for (auto &c : cards_) set_hidden(c.box, true);
    for (auto &h : heads_) {
      set_hidden(h.text, true);
      set_hidden(h.rule, true);
    }
    tomorrow_ = day_after(g_today);
    // The next thing today: the first timed event still to come, which is the one the face
    // raises. An all-day event is never it; it has no time to count down to.
    const AgendaEvent *next = nullptr;
    int today = 0;
    for (const auto &it : events_) {
      if (spent_(it) || it.d != g_today) continue;
      today++;
      if (next == nullptr && !it.all_day) next = &it;
    }
    strip_.set_left(head_text_(today), T_CALENDAR);
    set_stamp_(asof_, stale_);

    int card = 0, head = 0, y = 0, shown = 0;
    bool after_row = false;   // a hairline goes between two resting rows and nowhere else
    std::string last_day;
    for (const auto &it : events_) {
      if (card >= MAX_CARDS) break;
      if (spent_(it)) continue;
      if (it.w != last_day) {
        if (!last_day.empty()) y += HEAD_GAP;
        if (it.d != g_today) {
          if (head >= MAX_HEADS) break;
          Head &h = ensure_head_(head);
          place_label_(h.text, margin_, y, ROW_W, heading_(it));
          lv_obj_set_pos(h.rule, margin_, y + 26);
          set_hidden(h.rule, false);
          head++;
          y += HEAD_H;
        }
        last_day = it.w;
        after_row = false;
      }
      Card &c = ensure_card_(card);
      bool raised = &it == next;
      fill_row_(c, it, raised, y, raised || !after_row);
      y += (raised ? RAISED_H : ROW_H) + GAP;
      card++;
      shown++;
      after_row = !raised;
    }
    if (shown > 0) {
      empty_.hide();
      return;
    }
    // Seven days with nothing in them is a result, not a failure. The card says so in the same
    // shape the board's empty state uses.
    empty_.set("NOTHING PLANNED", "Nothing in the diary this week. Enjoy it.",
               asof_ == 0 ? std::string() : "SHOWING " + hhmm_of(asof_));
  }

 private:
  // The face's geometry, from DayFace and AgendaRow: rows 432 wide inside a 24 px margin, 16 of
  // padding in the raised one and 12/16 in the rest, a 52 px time gutter and a 14 px gap after it.
  static const int LIST_Y = 68, LIST_BOTTOM = 464, ROW_W = 432, ROW_H = 60, RAISED_H = 80;
  static const int GAP = 8, HEAD_H = 38, HEAD_GAP = 8, PAD = 16, TIME_W = 52, TEXT_X = 82;
  static const int DUR_W = 84;

  struct Card {
    lv_obj_t *box = nullptr, *rule = nullptr, *time = nullptr, *title = nullptr, *meta = nullptr;
  };
  struct Head {
    lv_obj_t *text = nullptr, *rule = nullptr;
  };

  // Today's timed events that have already ended are not the diary any more.
  static bool spent_(const AgendaEvent &e) {
    return !e.all_day && e.d == g_today && !g_nowhm.empty() && e.u.size() == 5 && e.u <= g_nowhm;
  }

  // "FRIDAY 18 · 4 EVENTS", or what is left of it before the clock has been set. The strip's
  // left half holds 26 mono characters; past that the day goes to its short form rather than
  // the line being cut, which is only ever a long weekday with nothing on it.
  std::string head_text_(int today) const {
    std::string count = today == 0   ? "NOTHING TODAY"
                        : today == 1 ? "1 EVENT"
                                     : std::to_string(today) + " EVENTS";
    if (day_.empty()) return count;
    std::string line = day_ + " \xC2\xB7 " + count;
    // The middle dot is two bytes and one character, so the count is off by one either way.
    if (line.size() - 1 > 26) line = short_day_ + " \xC2\xB7 " + count;
    return line;
  }
  std::string heading_(const AgendaEvent &e) const {
    if (!tomorrow_.empty() && e.d == tomorrow_) return "TOMORROW";
    return long_weekday(e.w);
  }

  // One row, in either of its two shapes. The objects are the same four either way; what changes
  // is the fill, the type and where the second line goes.
  void fill_row_(Card &c, const AgendaEvent &e, bool raised, int y, bool hide_rule) {
    lv_obj_set_pos(c.box, margin_, y);
    lv_obj_set_size(c.box, ROW_W, raised ? RAISED_H : ROW_H);
    set_fill(c.box, T_RAISED, raised, 16);
    set_hidden(c.box, false);
    set_hidden(c.rule, hide_rule);
    int time_y = ((raised ? RAISED_H : ROW_H) - 21) / 2;
    if (raised) {
      // The time keeps the gutter it has in a resting row so the column runs straight down the
      // face, and the second line says how long there is rather than repeating the end time.
      place_label_(c.time, PAD, PAD + 4, TIME_W, e.all_day ? "" : e.t);
      set_tok(c.time, T_TIME2);
      place_label_(c.title, TEXT_X, PAD, ROW_W - TEXT_X - PAD, e.s);
      lv_obj_set_style_text_font(c.title, F(g_fonts.sans600_20), 0);
      set_tok(c.title, T_CHALK);
      std::string sub = e.all_day ? std::string("All day") : starts_in(e.t);
      if (!e.l.empty()) sub += (sub.empty() ? "" : " \xC2\xB7 ") + e.l;
      place_label_(c.meta, TEXT_X, PAD + 28, ROW_W - TEXT_X - PAD, sub);
      lv_obj_set_style_text_font(c.meta, F(g_fonts.sans500_16), 0);
      lv_obj_set_style_text_align(c.meta, LV_TEXT_ALIGN_LEFT, 0);
      set_tok(c.meta, T_CHALK70);
      return;
    }
    place_label_(c.time, PAD, time_y, TIME_W, e.all_day ? "" : e.t);
    set_tok(c.time, T_HOUR);
    place_label_(c.title, TEXT_X, time_y - 2, ROW_W - TEXT_X - PAD - DUR_W - 8, e.s);
    lv_obj_set_style_text_font(c.title, F(g_fonts.sans500_18), 0);
    set_tok(c.title, T_TITLE2);
    place_label_(c.meta, ROW_W - PAD - DUR_W, time_y, DUR_W,
                 e.all_day ? std::string("All day") : duration_text(e.t, e.u));
    lv_obj_set_style_text_font(c.meta, F(g_fonts.mono15), 0);
    lv_obj_set_style_text_align(c.meta, LV_TEXT_ALIGN_RIGHT, 0);
    set_tok(c.meta, T_CHALK50);
  }

  Card &ensure_card_(int i) {
    while ((int) cards_.size() <= i) {
      Card c;
      c.box = mk_obj(list_, margin_, 0, ROW_W, ROW_H);
      set_hidden(c.box, true);
      c.rule = mk_rule(c.box, 0, 0, ROW_W, T_LINE);
      c.time = mk_label(c.box, PAD, 0, TIME_W, 22, F(g_fonts.mono16), T_HOUR);
      c.title = mk_label(c.box, TEXT_X, 0, 200, 26, F(g_fonts.sans500_18), T_TITLE2);
      lv_label_set_long_mode(c.title, LV_LABEL_LONG_MODE_DOTS);
      c.meta = mk_label(c.box, TEXT_X, 0, 200, 22, F(g_fonts.mono15), T_CHALK50);
      lv_label_set_long_mode(c.meta, LV_LABEL_LONG_MODE_DOTS);
      cards_.push_back(c);
    }
    return cards_[i];
  }
  Head &ensure_head_(int i) {
    while ((int) heads_.size() <= i) {
      Head h;
      h.text = mk_label(list_, margin_, 0, ROW_W, 22, F(g_fonts.mono15), T_CHALK50);
      tracked(h.text, 1);
      lv_label_set_long_mode(h.text, LV_LABEL_LONG_MODE_DOTS);
      set_hidden(h.text, true);
      h.rule = mk_rule(list_, margin_, 0, ROW_W, T_DIVIDER);
      set_hidden(h.rule, true);
      heads_.push_back(h);
    }
    return heads_[i];
  }
  // Hidden objects do not extend the scroll area, so the list scrolls exactly as far as content.
  static void place_label_(lv_obj_t *o, int x, int y, int w, const std::string &text) {
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_label_set_text(o, text.c_str());
    set_hidden(o, text.empty());
  }
  static void touch_cb_(lv_event_t *e) { emit("TOUCH"); }

  lv_obj_t *list_ = nullptr;
  EmptyCard empty_;
  std::vector<Card> cards_;
  std::vector<Head> heads_;
  std::vector<AgendaEvent> events_;
  // The day the strip names, as the tick last worked it out, and the day after today, which is
  // what tells a "TOMORROW" heading from a weekday one.
  std::string day_, short_day_, last_hm_, tomorrow_;
  uint32_t asof_ = 0;
  bool stale_ = false;
};

// ---- sky: the weather face, laid out as SkyFace in the design system.
//
// 24 px of padding, the place and the stamp on a 28 px header, one big temperature with the day
// in a line and a sentence under it, and the hours to come as a strip of bars along the bottom.
//
// No icons are drawn here at all: the Material glyphs are on their way out and the weather set
// the design wants has not been drawn yet. The bars and the numbers say it in the meantime.
class SkyView : public PageView {
 public:
  SkyView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'g') {
    strip_.build(root_, margin_);
    // The hero is auto-width so `feels` can be put beside it, on its baseline, once the number
    // is known. The degree sign is a real glyph in the 88 px face, not a drawn ring.
    hero_ = mk_label(root_, margin_, HEAD_BOTTOM, 0, 0, F(g_fonts.hero88), T_CHALK, "");
    tracked(hero_, -4);
    feels_ = mk_label(root_, margin_, HEAD_BOTTOM, 0, 0, F(g_fonts.sans500_20), T_CHALK70, "");
    head_ = mk_label(root_, margin_, HEAD_BOTTOM, TEXT_W, 0, F(g_fonts.sans500_20), T_HEADLINE, "");
    lv_label_set_long_mode(head_, LV_LABEL_LONG_MODE_DOTS);
    sent_ = mk_label(root_, margin_, HEAD_BOTTOM, TEXT_W, 0, F(g_fonts.sans400_18), T_CHALK70, "");
    lv_label_set_long_mode(sent_, LV_LABEL_LONG_MODE_WRAP);
    build_strip_();
    empty_.build(root_, margin_, HEAD_BOTTOM + 40, TEXT_W);
    build_problem_();
  }

  void apply(const Page &pg) override {
    asof_ = pg.asof;
    stale_ = pg.stale;
    strip_.set_left(pg.place.empty() ? std::string("WEATHER") : upper(pg.place), T_WEATHER);
    set_stamp_(asof_, stale_);

    std::string temp = pg.temp, feels = pg.feels, head = pg.head, sent = pg.sent;
    bool hours = !temp.empty() && !pg.hours.empty();
    if (temp.empty()) {
      // A document from a backend older than this face, or one cached before it: the first row's
      // big value is the temperature and its second line is all the headline there is. Nothing
      // else on the page can be trusted to be about the weather, so nothing else is shown.
      if (!pg.grows.empty() && is_number(pg.grows[0].value)) {
        temp = pg.grows[0].value;
        head = pg.grows[0].b;
      }
      feels.clear();
      sent.clear();
    }
    if (temp.empty()) {
      for (lv_obj_t *o : {hero_, feels_, head_, sent_, card_}) set_hidden(o, true);
      empty_.set("OFFLINE", "Can't reach the forecast.",
                 asof_ == 0 ? std::string() : "SHOWING " + hhmm_of(asof_));
      return;
    }
    empty_.hide();
    label_text(hero_, temp + "\xC2\xB0");              // U+00B0
    label_text(feels_, feels.empty() ? "" : "feels " + feels + "\xC2\xB0");
    label_text(head_, head);
    label_text(sent_, sent);
    for (lv_obj_t *o : {hero_, feels_, head_, sent_}) set_hidden(o, false);
    set_hidden(feels_, feels.empty());
    set_hidden(head_, head.empty());
    set_hidden(sent_, sent.empty());
    set_hidden(card_, !hours);
    if (hours) fill_strip_(pg.hours);
    // With no strip to draw, the block has the whole face to be centred on rather than the top
    // of it: a page carrying only rows should not look like one with something missing.
    centre_(hours ? card_y_ : 480 - margin_);
  }

  void tick(esphome::ESPTime now) override { tick_header_(now); }

 private:
  // The face's geometry: the header ends at 52, the strip card's bottom sits on the 24 px margin
  // and the block between them is centred on what is left.
  static const int HEAD_BOTTOM = 52, TEXT_W = 432, GAP = 10;
  // The strip card, from SkyFace: six columns 60 wide with 8 between them inside 16 of padding,
  // and a bar area of 68. The card is taller than the design's 128 because a rasterised 16 px
  // label and a 14 px one need more room than the mock's line boxes did.
  static const int COLS = 6, COL_W = 60, COL_PITCH = 68, CARD_PAD = 16, BAR_MAX = 68;

  struct Col {
    lv_obj_t *root = nullptr, *temp = nullptr, *bar = nullptr, *hour = nullptr;
  };

  void build_strip_() {
    int th = lv_font_get_line_height(F(g_fonts.sans500_16));
    int hh = lv_font_get_line_height(F(g_fonts.mono14));
    int inner = th + 6 + BAR_MAX + 6 + hh;
    card_h_ = inner + 2 * CARD_PAD;
    card_y_ = 480 - margin_ - card_h_;
    card_ = mk_panel(root_, margin_, card_y_, TEXT_W, card_h_, T_CARD, 16);
    set_hidden(card_, true);
    for (int i = 0; i < COLS; i++) {
      Col &c = cols_[i];
      c.root = mk_obj(card_, CARD_PAD + COL_PITCH * i, CARD_PAD, COL_W, inner);
      c.temp = mk_label(c.root, 0, 0, COL_W, th, F(g_fonts.sans500_16), T_HEADLINE, "");
      lv_obj_set_style_text_align(c.temp, LV_TEXT_ALIGN_CENTER, 0);
      // Anchored at the bottom of the bar area, which is what makes a row of bars a chart.
      c.bar = mk_panel(c.root, 0, th + 6, COL_W, BAR_MAX, T_WEATHER, 4);
      c.hour = mk_label(c.root, 0, th + 12 + BAR_MAX, COL_W, hh, F(g_fonts.mono14), T_HOUR, "");
      lv_obj_set_style_text_align(c.hour, LV_TEXT_ALIGN_CENTER, 0);
      set_hidden(c.root, true);
    }
    bar_top_ = th + 6;
  }

  // The focus column is the first hour at half a chance of rain or more, which is the hour the
  // sentence is about. With nothing above half, the next hour is the one to read.
  void fill_strip_(const std::vector<HourSlot> &hours) {
    int focus = 0;
    for (size_t i = 0; i < hours.size(); i++)
      if (hours[i].r >= 50) {
        focus = (int) i;
        break;
      }
    for (int i = 0; i < COLS; i++) {
      Col &c = cols_[i];
      if (i >= (int) hours.size()) {
        set_hidden(c.root, true);
        continue;
      }
      const HourSlot &s = hours[i];
      label_text(c.temp, s.t.empty() ? "" : s.t + "\xC2\xB0");
      set_tok(c.temp, i == focus ? T_CHALK : T_HEADLINE);
      // A dry hour keeps a stub: an empty column would read as a missing reading.
      int h = s.r * BAR_MAX / 100;
      if (h < 4) h = 4;
      lv_obj_set_pos(c.bar, 0, bar_top_ + BAR_MAX - h);
      lv_obj_set_height(c.bar, h);
      int d = i > focus ? i - focus : focus - i;
      lv_obj_set_style_opa(c.bar, d == 0 ? 255 : d == 1 ? 191 : d == 2 ? 115 : 77, 0);
      label_text(c.hour, s.h);
      set_tok(c.hour, i == focus ? T_TIME2 : T_HOUR);
      set_hidden(c.root, false);
    }
  }

  // The middle block is centred on what the header and the strip card leave, after the sentence
  // has been laid out: one line or two changes how tall the block is.
  void centre_(int bottom) {
    lv_obj_update_layout(root_);
    int hero_h = lv_obj_get_height(hero_);
    int head_h = lv_obj_has_flag(head_, LV_OBJ_FLAG_HIDDEN) ? 0 : lv_obj_get_height(head_);
    int sent_h = lv_obj_has_flag(sent_, LV_OBJ_FLAG_HIDDEN) ? 0 : lv_obj_get_height(sent_);
    int total = hero_h + (head_h > 0 ? GAP + head_h : 0) + (sent_h > 0 ? GAP + sent_h : 0);
    int top = HEAD_BOTTOM + (bottom - HEAD_BOTTOM - total) / 2;
    if (top < HEAD_BOTTOM + 8) top = HEAD_BOTTOM + 8;
    lv_obj_set_pos(hero_, margin_, top);
    // `feels` sits on the hero's baseline, not on its box, which is what puts a 20 px word on
    // the same line as an 88 px number.
    const lv_font_t *hf = F(g_fonts.hero88), *ff = F(g_fonts.sans500_20);
    int base = (lv_font_get_line_height(hf) - hf->base_line) -
               (lv_font_get_line_height(ff) - ff->base_line);
    lv_obj_set_pos(feels_, margin_ + lv_obj_get_width(hero_) + 12, top + base);
    int y = top + hero_h;
    if (head_h > 0) {
      y += GAP;
      lv_obj_set_pos(head_, margin_, y);
      y += head_h;
    }
    if (sent_h > 0) lv_obj_set_pos(sent_, margin_, y + GAP);
  }

  lv_obj_t *hero_ = nullptr, *feels_ = nullptr, *head_ = nullptr, *sent_ = nullptr;
  lv_obj_t *card_ = nullptr;
  Col cols_[COLS];
  EmptyCard empty_;
  int card_h_ = 0, card_y_ = 0, bar_top_ = 0;
  uint32_t asof_ = 0;
  bool stale_ = false;
};

// ---- list: the to-do face, laid out as ListFace in the design system.
//
// 24 px of padding, the short date and the stamp on the strip, the page's own title as an
// eyebrow, then the open items as rings. Nothing is ever ticked here: a done item never reaches
// the document, so there is no done state to draw and nothing on this face is a control.
class ListView : public PageView {
 public:
  static const int MAX_ROWS = 8;

  ListView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'g') {
    strip_.build(root_, margin_);
    eyebrow_ = mk_label(root_, margin_, EYEBROW_Y, ROW_W, 22, F(g_fonts.mono16), T_REMINDERS, "");
    tracked(eyebrow_, 1);
    lv_label_set_long_mode(eyebrow_, LV_LABEL_LONG_MODE_DOTS);
    // Eight rows are more than the screen holds, so they scroll, vertically only, as the diary's
    // do: a horizontal flick is the carousel's.
    list_ = mk_obj(root_, 0, LIST_Y, 480, LIST_BOTTOM - LIST_Y);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(list_, touch_cb_, LV_EVENT_SHORT_CLICKED, nullptr);
    empty_.build(root_, margin_, LIST_Y, ROW_W);
    build_problem_();
  }

  void apply(const Page &pg) override {
    label_text(eyebrow_, upper(pg.title));
    set_stamp_(pg.asof, pg.stale);
    for (auto &r : rows_) set_hidden(r.box, true);
    int n = (int) pg.grows.size();
    if (n > MAX_ROWS) n = MAX_ROWS;
    for (int i = 0; i < n; i++) {
      const GenericRow &d = pg.grows[i];
      Row &r = ensure_row_(i);
      // The item's own text is `a` (docs/screen-document.md); `v` is the day count, and is only
      // worth reading as the title on a page that put the text there instead.
      label_text(r.title, d.a.empty() ? d.value : d.a);
      std::string due = upper(d.b);
      label_text(r.due, due);
      set_tok(r.due, due.find("OVERDUE") == std::string::npos ? T_CHALK50 : T_LATE);
      set_hidden(r.box, false);
    }
    if (n > 0) {
      empty_.hide();
      return;
    }
    empty_.set("ALL DONE", "Nothing on the list. Enjoy it.",
               pg.asof == 0 ? std::string() : "SHOWING " + hhmm_of(pg.asof));
  }

  // The strip carries the short date, as the clock's does, and the stamp owns the other end.
  void tick(esphome::ESPTime now) override {
    if (g_nowhm == last_hm_) return;
    last_hm_ = g_nowhm;
    strip_.set_left(now.is_valid() ? ClockView::short_date(now) : std::string(), T_CHALK70);
  }

 private:
  // ReminderCheck at its large size: a 60 px row with 16 of padding, a 30 px ring and 14 after
  // it, on a 12 px gap. The eyebrow takes the 16 px of air under the strip.
  static const int EYEBROW_Y = 68, LIST_Y = 102, LIST_BOTTOM = 464, ROW_W = 432, ROW_H = 60;
  static const int GAP = 12, PAD = 16, RING = 30, TEXT_X = 60, DUE_W = 110;

  struct Row {
    lv_obj_t *box = nullptr, *ring = nullptr, *title = nullptr, *due = nullptr;
  };

  Row &ensure_row_(int i) {
    while ((int) rows_.size() <= i) {
      Row r;
      r.box = mk_panel(list_, margin_, (ROW_H + GAP) * (int) rows_.size(), ROW_W, ROW_H, T_CARD, 16);
      set_hidden(r.box, true);
      // An outline, not a fill: a ring is an item still open, and there is nothing to tick it.
      r.ring = mk_obj(r.box, PAD, (ROW_H - RING) / 2, RING, RING);
      lv_obj_set_style_radius(r.ring, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(r.ring, 3, 0);
      lv_obj_set_style_border_color(r.ring, col(T_REMINDERS), 0);
      r.title = mk_label(r.box, TEXT_X, 16, ROW_W - TEXT_X - PAD - DUE_W - 8, 28,
                         F(g_fonts.sans500_22), T_CHALK);
      lv_label_set_long_mode(r.title, LV_LABEL_LONG_MODE_DOTS);
      r.due = mk_label(r.box, ROW_W - PAD - DUE_W, 20, DUE_W, 20, F(g_fonts.mono15), T_CHALK50);
      tracked(r.due, 1);
      lv_obj_set_style_text_align(r.due, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_long_mode(r.due, LV_LABEL_LONG_MODE_DOTS);
      rows_.push_back(r);
    }
    return rows_[i];
  }
  static void touch_cb_(lv_event_t *e) { emit("TOUCH"); }

  lv_obj_t *eyebrow_ = nullptr, *list_ = nullptr;
  EmptyCard empty_;
  std::vector<Row> rows_;
  std::string last_hm_;
};

// ---- generic: the module template, for a module with no face of its own. Up to eight rows of a
// value and two text lines, in the new type and with no icons, in a vertical scroller.
class GenericView : public PageView {
 public:
  static const int MAX_ROWS = 8;

  GenericView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'g') {
    build_header_("");
    list_ = mk_obj(root_, 0, LIST_Y, 480, LIST_BOTTOM - LIST_Y);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(list_, touch_cb_, LV_EVENT_SHORT_CLICKED, nullptr);
  }

  void apply(const Page &pg) override {
    set_title_(pg.title);
    for (auto &r : rows_) set_hidden(r.box, true);
    int n = (int) pg.grows.size();
    if (n > MAX_ROWS) n = MAX_ROWS;
    for (int i = 0; i < n; i++) {
      const GenericRow &d = pg.grows[i];
      Row &r = ensure_row_(i);
      place_(r.value, d.value);
      place_(r.a, d.a);
      place_(r.b, d.b);
      set_hidden(r.box, false);
    }
    set_stamp_(pg.asof, pg.stale);
  }

  void tick(esphome::ESPTime now) override { tick_header_(now); }

 private:
  static const int LIST_Y = 68, LIST_BOTTOM = 464, ROW_W = 432, ROW_H = 60, GAP = 8, PAD = 16;
  static const int VALUE_W = 64;

  struct Row {
    lv_obj_t *box = nullptr, *value = nullptr, *a = nullptr, *b = nullptr;
  };
  Row &ensure_row_(int i) {
    while ((int) rows_.size() <= i) {
      Row r;
      r.box = mk_panel(list_, margin_, (ROW_H + GAP) * (int) rows_.size(), ROW_W, ROW_H, T_CARD, 16);
      set_hidden(r.box, true);
      r.value = mk_label(r.box, PAD, 18, VALUE_W, 26, F(g_fonts.sans600_20), T_CHALK);
      lv_label_set_long_mode(r.value, LV_LABEL_LONG_MODE_DOTS);
      r.a = mk_label(r.box, PAD + VALUE_W + 14, 10, 260, 24, F(g_fonts.sans500_18), T_TITLE2);
      lv_label_set_long_mode(r.a, LV_LABEL_LONG_MODE_DOTS);
      r.b = mk_label(r.box, PAD + VALUE_W + 14, 34, 260, 20, F(g_fonts.mono15), T_CHALK70);
      tracked(r.b, 1);
      lv_label_set_long_mode(r.b, LV_LABEL_LONG_MODE_DOTS);
      rows_.push_back(r);
    }
    return rows_[i];
  }
  static void place_(lv_obj_t *o, const std::string &text) {
    lv_label_set_text(o, text.c_str());
    set_hidden(o, text.empty());
  }
  static void touch_cb_(lv_event_t *e) { emit("TOUCH"); }

  lv_obj_t *list_ = nullptr;
  std::vector<Row> rows_;
};

// ---------------------------------------------------------------- the host
class PageHost {
 public:
  // Builds the carousel (or the stack) inside content_page's root object, with the clock on it,
  // and puts the boot overlay on top of the lot.
  void attach(lv_obj_t *content_root, const std::string &fw) {
    if (host_ != nullptr) return;
    init_styles();   // before the first widget, so nothing is built with an unset colour
    host_ = mk_panel(content_root, 0, 0, 480, 480, T_NIGHT, 0);
    // mk_obj strips CLICKABLE, but the indev hit-test only finds clickable objects: without
    // this flag a drag lands on the screen behind the host and neither scrolling nor
    // gestures ever reach it. The views themselves stay non-clickable so touches fall through.
    lv_obj_add_flag(host_, LV_OBJ_FLAG_CLICKABLE);
#if HB_CAROUSEL
    lv_obj_add_flag(host_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(host_, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_dir(host_, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(host_, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(host_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(host_, scroll_end_cb_, LV_EVENT_SCROLL_END, this);
#else
    // Gestures bubble up by default and are only delivered to an object that does not bubble.
    lv_obj_remove_flag(host_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(host_, gesture_cb_, LV_EVENT_GESTURE, this);
#endif
    views_.clear();
    views_.emplace_back(new ClockView(host_));
    cur_ = 0;
    dots_.build(host_);
    boot_.reset(new BootView(host_, fw));
    if (!ssid_.empty()) boot_->on_network(ssid_);   // in case Wi-Fi came up before we were attached
    layout_();
    show(0, false);
  }

  // Take the carousel back down so the next attach() builds it again. The device never calls this:
  // it is here for the host simulator (firmware/sim/), which renders every face in one process and
  // needs a clean host between them. Order matters: the views and the overlay delete their own
  // roots, so they go before the host they hang off.
  void reset() {
    views_.clear();
    boot_.reset();
    dots_.destroy();
    if (host_ != nullptr) lv_obj_delete(host_);
    host_ = nullptr;
    cur_ = 0;
    unpaired_ = false;
    ssid_.clear();
    events_.clear();
    temp_.clear();
    line_day_.clear();
    line_hm_.clear();
  }

  // Reconcile the pages on screen with the document: reuse by id and type, create what is new,
  // delete what is gone, and stay on the page the user was looking at.
  void set_document(const Document &doc) {
    if (host_ == nullptr) return;
    unpaired_ = false;
    std::string keep = cur_ < views_.size() ? views_[cur_]->id() : "";
    std::vector<std::unique_ptr<PageView>> next;
    next.push_back(std::move(views_[0]));  // the clock is always page one
    for (const Page &pg : doc.pages) {
      if (pg.type != 'b' && pg.type != 'a' && pg.type != 'g') continue;
      if (next.size() >= 1 + MAX_CONTENT) break;
      std::unique_ptr<PageView> v;
      for (size_t i = 1; i < views_.size(); i++) {
        // The module is part of the test, not only the id and the type: a generic page that
        // changes module is a different face and has to be built again.
        if (views_[i] && views_[i]->id() == pg.id && views_[i]->type() == pg.type &&
            views_[i]->module() == pg.module) {
          v = std::move(views_[i]);
          break;
        }
      }
      if (!v) v = make_view_(pg);
      v->apply(pg);
      next.push_back(std::move(v));
    }
    views_ = std::move(next);  // whatever the document dropped is destroyed here
    if (boot_) boot_->on_document(doc.name);
    auto *clock = static_cast<ClockView *>(views_[0].get());
    clock->clear_problem();
    // The document's notice goes on after the problem line has been cleared, so the arrival of a
    // document no longer wipes it: only a document without one does.
    clock->set_doc_notice(doc.notice);
    read_clock_sources_(doc);
    line_day_ = g_today;
    line_hm_ = g_nowhm;
    refresh_clock_line_();
    layout_();
    size_t idx = cur_ < views_.size() ? cur_ : views_.size() - 1;
    if (!keep.empty()) {
      for (size_t i = 0; i < views_.size(); i++)
        if (views_[i]->id() == keep) {
          idx = i;
          break;
        }
    }
    show(idx, false);
  }

  // Unclaimed (or freshly 401'd): the clock and the pairing page, nothing else.
  void set_unpaired(const std::string &code) {
    if (host_ == nullptr) return;
    unpaired_ = true;
    std::unique_ptr<PageView> clock = std::move(views_[0]);
    std::unique_ptr<PageView> pair;
    for (size_t i = 1; i < views_.size(); i++)
      if (views_[i] && views_[i]->type() == 'p') {
        pair = std::move(views_[i]);
        break;
      }
    views_.clear();
    if (!pair) pair.reset(new PairingView(host_));
    // The diary and the weather belonged to a household that no longer claims this device, and
    // so did the notice.
    events_.clear();
    temp_.clear();
    static_cast<ClockView *>(clock.get())->set_line("", "");
    static_cast<ClockView *>(clock.get())->set_doc_notice("");
    static_cast<PairingView *>(pair.get())->set_code(code);
    views_.push_back(std::move(clock));
    views_.push_back(std::move(pair));
    layout_();
    show(cur_ < views_.size() ? cur_ : views_.size() - 1, false);
    if (boot_) boot_->on_unpaired(code);
    raise_boot_();   // the pairing view was just created on top of everything, including us
  }

  void show(size_t index, bool animate) {
    if (views_.empty()) return;
    if (index >= views_.size()) index = views_.size() - 1;
#if HB_CAROUSEL
    cur_ = index;
    // The pages were just repositioned; scrolling reads coordinates, so settle them first.
    lv_obj_update_layout(host_);
    lv_obj_scroll_to_view(views_[index]->root(), animate ? LV_ANIM_ON : LV_ANIM_OFF);
#else
    if (index == cur_ || !animate) {
      cur_ = index;
      for (size_t i = 0; i < views_.size(); i++) {
        lv_obj_set_x(views_[i]->root(), 0);
        set_hidden(views_[i]->root(), i != cur_);
      }
      dots_.set_active(cur_);   // right for the next time something brings them up
      return;
    }
    lv_obj_t *out = views_[cur_]->root();
    lv_obj_t *in = views_[index]->root();
    int dir = index > cur_ ? -1 : 1;  // moving forward slides the old page off to the left
    cur_ = index;
    set_hidden(in, false);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, anim_x_cb_);
    lv_anim_set_duration(&a, 320);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_var(&a, in);
    lv_anim_set_values(&a, -dir * 480, 0);
    lv_anim_start(&a);
    lv_anim_set_var(&a, out);
    lv_anim_set_values(&a, 0, dir * 480);
    lv_anim_set_completed_cb(&a, anim_done_cb_);
    lv_anim_start(&a);
#endif
    // A page that moved is a page the household just asked for; say where they are.
    dots_.set_active(cur_);
    dots_.show();
  }

  // A touch anywhere brings the page indicator up, as a swipe does. hallboard.yaml's TOUCH
  // branch calls this.
  void touch() { dots_.show(); }

  // One step around the carousel, wrapping, for the gesture fallback and the button.
  void step(int delta) {
    if (views_.size() < 2) return;
    int n = (int) views_.size();
    int idx = ((int) cur_ + delta) % n;
    if (idx < 0) idx += n;
    show((size_t) idx, true);
  }

  void tick(esphome::ESPTime now) {
    if (now.is_valid()) {
      g_nowhm = now.strftime("%H:%M");
      g_today = now.strftime("%Y%m%d");
      g_now_epoch = (uint32_t) now.timestamp;
      if (boot_) boot_->on_time_valid();
      // The foot of the clock is about what is next, so it is rebuilt whenever the minute the
      // events are measured against moves, and not only when a document lands.
      if (g_today != line_day_ || g_nowhm != line_hm_) {
        line_day_ = g_today;
        line_hm_ = g_nowhm;
        refresh_clock_line_();
      }
    }
    for (auto &v : views_) v->tick(now);
  }

  // The boot sequence's own clock, off the 100 ms interval. The overlay goes when its fade has
  // finished, one tick later, and the object count in the heap log comes back with it.
  void tick_fast(uint32_t now_ms) {
    if (!boot_) return;
    boot_->tick_fast(now_ms);
    if (boot_->finished()) {
      boot_.reset();
      ESP_LOGI("hb", "boot view done");
    }
  }
  bool booting() const { return (bool) boot_; }

  // Remembered even before attach, so a network that comes up early still reaches the support line.
  void set_network(const std::string &ssid) {
    ssid_ = ssid;
    if (boot_) boot_->on_network(ssid);
  }

  void set_status(const std::string &msg, bool problem = false) {
    for (auto &v : views_) v->set_status(msg, problem);
    if (boot_) boot_->on_status(msg, problem);
  }

  // Firmware update progress and the post-update confirmation, shown on the clock page only so a
  // board or agenda keeps its own footer. An empty string clears it.
  void set_notice(const std::string &msg) {
    if (boot_) boot_->set_notice(msg);
    if (views_.empty()) return;
    static_cast<ClockView *>(views_[0].get())->set_notice(msg);
  }

  size_t current() const { return cur_; }
  size_t count() const { return views_.size(); }
  bool unpaired() const { return unpaired_; }

  // What the pairing QR carries, empty when there is no pairing page or no code yet. The host
  // simulator prints it so the fragment contract stays checked; the device never logs it.
  std::string pair_qr_url() const {
    for (const auto &v : views_)
      if (v && v->type() == 'p') return static_cast<const PairingView *>(v.get())->qr_url();
    return "";
  }
  uint32_t object_count() const { return host_ == nullptr ? 0 : lv_obj_get_child_count(host_); }

  PageView *view(size_t i) const { return i < views_.size() ? views_[i].get() : nullptr; }
  PageView *current_view() const { return view(cur_); }
  char current_type() const { return cur_ < views_.size() ? views_[cur_]->type() : 0; }
  BoardView *current_board() const {
    PageView *v = current_view();
    return (v != nullptr && v->type() == 'b') ? static_cast<BoardView *>(v) : nullptr;
  }

 private:
  static constexpr size_t MAX_CONTENT = 8, MAX_CLOCK_EVENTS = 24;

  // The face a page gets. Three of them are generic pages as far as the document is concerned,
  // which is why the module decides between them and why it is remembered on the view.
  std::unique_ptr<PageView> make_view_(const Page &pg) {
    std::unique_ptr<PageView> v;
    if (pg.type == 'b')
      v.reset(new BoardView(host_, pg.id));
    else if (pg.type == 'a')
      v.reset(new AgendaView(host_, pg.id));
    else if (pg.module == "weather")
      v.reset(new SkyView(host_, pg.id));
    else if (pg.module == "reminders")
      v.reset(new ListView(host_, pg.id));
    else
      v.reset(new GenericView(host_, pg.id));
    v->set_module(pg.module);
    return v;
  }

  // Positions, stacking order and the page indicator after any change to the page list.
  void layout_() {
    for (size_t i = 0; i < views_.size(); i++) {
      lv_obj_t *o = views_[i]->root();
      lv_obj_move_to_index(o, (int32_t) i);
#if HB_CAROUSEL
      lv_obj_set_pos(o, (int32_t) (480 * i), 0);
#else
      lv_obj_set_pos(o, 0, 0);
      set_hidden(o, i != cur_);
#endif
    }
    dots_.rebuild(views_.size());
    dots_.set_active(cur_ < views_.size() ? cur_ : 0);
    dots_.raise();   // re-indexing the views has just put them above the dots
    raise_boot_();   // and the overlay goes above the lot
  }

  void raise_boot_() {
    if (boot_) boot_->raise();
  }

  // ---- the clock's foot line
  // The next thing in the diary and the temperature come off two pages the clock itself never
  // sees, so they are worked out here. What a document contributes is kept, in the smallest
  // shape that will do, because the line also has to be rebuilt as the day moves on.
  struct ClockEvent {
    std::string d, t, s;
    bool all_day = false;
  };

  void read_clock_sources_(const Document &doc) {
    events_.clear();
    temp_.clear();
    const Page *weather = nullptr;
    bool named = false;
    for (const Page &pg : doc.pages) {
      // Only today and tomorrow are ever read and the backend sorts by day, so the first two
      // dozen events of the first agenda page are always enough to find them.
      if (pg.type == 'a' && events_.empty()) {
        for (const AgendaEvent &e : pg.events) {
          if (events_.size() >= MAX_CLOCK_EVENTS) break;
          ClockEvent c;
          c.d = e.d;
          c.t = e.t;
          c.s = e.s;
          c.all_day = e.all_day;
          events_.push_back(std::move(c));
        }
        continue;
      }
      if (pg.type != 'g') continue;
      if (!named && pg.module == "weather") {
        weather = &pg;
        named = true;
      } else if (weather == nullptr) {
        weather = &pg;
      }
    }
    if (weather == nullptr) return;
    if (!weather->temp.empty()) {
      temp_ = weather->temp + "\xC2\xB0";   // U+00B0
      return;
    }
    // A backend older than the weather face sends no `temp`, so the first row's big value stands
    // in for it, but only when it really is a number: `v` may be a day count or anything else.
    if (weather->grows.empty()) return;
    if (is_number(weather->grows[0].value)) temp_ = weather->grows[0].value + "\xC2\xB0";
  }

  // Today's next timed event, else today's first all-day one, else tomorrow's first. Empty when
  // the document carried no agenda page, or the clock has not been set yet.
  std::string next_event_() const {
    if (events_.empty() || g_today.empty()) return "";
    const ClockEvent *all_day = nullptr;
    for (const auto &e : events_) {
      if (e.d != g_today) continue;
      if (e.all_day) {
        if (all_day == nullptr) all_day = &e;
        continue;
      }
      // Sorted by day and time, so the first one still to come is the next one.
      if (e.t.size() == 5 && e.t > g_nowhm) return e.t + " " + e.s;
    }
    if (all_day != nullptr) return "All day \xC2\xB7 " + all_day->s;
    std::string tomorrow = day_after(g_today);
    if (tomorrow.empty()) return "";
    for (const auto &e : events_)
      if (e.d == tomorrow) return "Tomorrow \xC2\xB7 " + (e.all_day ? e.s : e.t + " " + e.s);
    return "";
  }

  void refresh_clock_line_() {
    if (views_.empty()) return;
    static_cast<ClockView *>(views_[0].get())->set_line(next_event_(), temp_);
  }

#if HB_CAROUSEL
  static void scroll_end_cb_(lv_event_t *e) {
    auto *self = static_cast<PageHost *>(lv_event_get_user_data(e));
    if (self == nullptr || self->host_ == nullptr || self->views_.empty()) return;
    int32_t idx = (lv_obj_get_scroll_x(self->host_) + 240) / 480;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t) self->views_.size()) idx = (int32_t) self->views_.size() - 1;
    self->cur_ = (size_t) idx;
  }
#else
  static void anim_x_cb_(void *var, int32_t v) { lv_obj_set_x(static_cast<lv_obj_t *>(var), v); }
  static void anim_done_cb_(lv_anim_t *a) {
    auto *o = static_cast<lv_obj_t *>(a->var);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(o, 0);
  }
  static void gesture_cb_(lv_event_t *e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT)
      emit("SWIPE|L");
    else if (dir == LV_DIR_RIGHT)
      emit("SWIPE|R");
  }
#endif

  lv_obj_t *host_ = nullptr;
  std::vector<std::unique_ptr<PageView>> views_;
  // The page indicator is not a page either: it floats above them all and fades on its own.
  FaceDots dots_;
  // The boot overlay is never a page: it is owned here and destroyed once, on its own.
  std::unique_ptr<BootView> boot_;
  std::string ssid_;
  // What the clock's foot line is built from, and the minute it was last built for.
  std::vector<ClockEvent> events_;
  std::string temp_, line_day_, line_hm_;
  size_t cur_ = 0;
  bool unpaired_ = false;
};

inline PageHost g_host;

}  // namespace hb
