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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <lvgl.h>
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
#include "hb_copy.h"
#include "hb_icons.h"
#include "hb_temp_scale.h"
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

// One row of a generic page: a large value and two text lines. `icon` is drawn by exactly one
// face: SkyView reads the weather page's first row's `icon` for the current-conditions icon on
// its "now" card. Every other face's rows carry it (a backend older than 1.4.0 always sends one)
// and never draw it, the reminders list saying its own state in type instead.
struct GenericRow {
  std::string icon, value, a, b;
};

// One two-hourly slot of the weather page's strip: the hour, the temperature at it and the
// chance of rain as a percentage. `i` is one of hb_icons.h's names (sun, partly, cloud, rain,
// pour, snow, fog, storm, wind, night), optional: empty when the backend has nothing for it.
struct HourSlot {
  std::string h, t, i;
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
  // The weather face's own fields, all optional. SkyView draws them; the clock uses `temp`,
  // `cond` and `wind` for its own one-line summary. `wdir`/`wspd`/`gust` are the wind card's own
  // (current conditions' icon is `grows[0].icon`); `rday` is today's chance of rain, -1 when the
  // backend sent none, which is not the same as 0.
  std::string place, temp, feels, cond, wind, wdir, wspd, gust;
  int rday = -1;
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

// ---------------------------------------------------------------- fonts
// The design system's type scale: Figtree for text, IBM Plex Mono for times, codes and meta.
// Set once on boot from the `font:` entries in ui.yaml (hidden anchor labels there are what
// compiles each one in); the host simulator fills the same members from TTFs.
struct FontSet {
  const lv_font_t *clock168 = nullptr;   // Figtree 600, the clock face
  const lv_font_t *sans600_46 = nullptr;  // Figtree 600, a weather card's big value
  const lv_font_t *sans600_30 = nullptr;
  const lv_font_t *sans600_24 = nullptr;
  const lv_font_t *sans600_20 = nullptr;
  const lv_font_t *sans500_22 = nullptr;
  const lv_font_t *sans500_20 = nullptr;
  const lv_font_t *sans500_18 = nullptr;
  const lv_font_t *sans500_16 = nullptr;
  const lv_font_t *sans400_24 = nullptr;  // Figtree 400, the clock's date and temperature
  const lv_font_t *sans400_18 = nullptr;
  const lv_font_t *mark50 = nullptr;     // Figtree 700, the two letters of the mark
  const lv_font_t *mono64 = nullptr;     // IBM Plex Mono 400, the pairing code
  const lv_font_t *mono20 = nullptr;
  const lv_font_t *mono16 = nullptr;
  const lv_font_t *mono15 = nullptr;
  const lv_font_t *mono14 = nullptr;
};
inline FontSet g_fonts;

// A font slot that has not been filled yet draws in whatever ui.yaml made the default, which is
// readable at any size rather than a screen of missing-glyph boxes.
inline const lv_font_t *F(const lv_font_t *f) { return f != nullptr ? f : LV_FONT_DEFAULT; }

// Letter spacing, in whole pixels. The mono sizes need a little air, the display sizes need
// taking in; the amounts are the design's.
inline void tracked(lv_obj_t *o, int px) { lv_obj_set_style_text_letter_space(o, px, 0); }

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
// The same instant as a Unix time, 0 until SNTP has synced. Kept beside the strings so the host
// simulator's pinned clock is the one instant every face reads.
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
  // A label with a width is one line unless a caller says otherwise: LVGL's default is to wrap,
  // and a time that measures a pixel wider on the board than on the host would otherwise fold
  // its minutes under the row. Sentences that may run to two lines set WRAP themselves.
  if (w > 0) lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
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

// A weather icon: a hidden, sourceless lv_image, built once and pointed at an A8 mask from
// hb_icons.h by set_icon below whenever a document says which one. The token is the only colour
// it ever has (hb_icons.h's icons carry none of their own), so switching it later means removing
// this style and adding another, the same as set_tok does for a label.
inline lv_obj_t *mk_icon(lv_obj_t *parent, int x, int y, Tok tok) {
  lv_obj_t *img = lv_image_create(parent);
  lv_obj_set_pos(img, x, y);
  lv_obj_add_style(img, &g_img[tok], 0);
  set_hidden(img, true);
  return img;
}

// Points an icon built by mk_icon at `name` (one of hb_icons.h's, or empty), at `size` (32 on a
// weather card, 24 in the hourly strip). Hidden rather than a blank image when the name is empty
// or not one hb_icons.h has at that size, which is also what an older backend's missing field
// comes in as.
inline void set_icon(lv_obj_t *img, const std::string &name, int size) {
  const lv_image_dsc_t *dsc = name.empty() ? nullptr : icon(name.c_str(), size);
  lv_image_set_src(img, dsc);
  set_hidden(img, dsc == nullptr);
}

// A card: the dark rounded panel boards and the agenda rest their rows on. The design has no
// coloured left edge; a row's state is in its status colour.
inline lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h) {
  return mk_panel(parent, x, y, w, h, T_CARD, 16);
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
// The wording is hb_copy.h's; what is worked out here is the number of minutes.
inline std::string starts_in(const std::string &t) {
  int at = hhmm_to_minutes(t), now = hhmm_to_minutes(g_nowhm);
  if (at < 0 || now < 0) return "";
  return copy::starts_in(at - now);
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

// The weather strip's bar colour: `t` (a temperature string, contract says whole degrees Celsius
// but this rounds rather than trusts that) looked up in the absolute scale generated into
// TEMP_SCALE_DAY/NIGHT by firmware/sim/tools/temp_scale.py, clamped to the table's -5 to 30 C
// span. `night` picks the table, not a token: the colour is by value, not a shared style, so
// nothing here reads the global palette on its own.
inline lv_color_t temp_bar_color(const std::string &t, bool night) {
  int c = (int) std::lround(atof(t.c_str()));
  if (c < -5) c = -5;
  if (c > 30) c = 30;
  return lv_color_hex((night ? TEMP_SCALE_NIGHT : TEMP_SCALE_DAY)[c + 5]);
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
    set_hidden(root_, true);
  }

  // The card's height follows its sentence.
  void set(const std::string &label, const std::string &text) {
    if (root_ == nullptr) return;
    lv_label_set_text(label_, label.c_str());
    lv_label_set_text(text_, text.c_str());
    set_hidden(root_, false);
    lv_obj_update_layout(root_);
    lv_obj_set_height(root_, TEXT_Y + lv_obj_get_height(text_) + PAD);
  }
  void hide() {
    if (root_ != nullptr) set_hidden(root_, true);
  }

 private:
  // 24 of padding, the 20 px label and the design's 12 px gap.
  static const int PAD = 24, TEXT_Y = 56;

  lv_obj_t *root_ = nullptr, *label_ = nullptr, *text_ = nullptr;
};

// ---------------------------------------------------------------- chrome
// The band across the top of every face: a short line on the left, optionally with a hue dot in
// front of it, and the time on the right. 28 px tall, inset by the face's margin.
class StatusStrip {
 public:
  void build(lv_obj_t *parent, int margin) {
    if (root_ != nullptr) return;
    int w = 480 - 2 * margin;
    root_ = mk_obj(parent, margin, margin, w, 28);
    dot_ = mk_panel(root_, 0, 10, 9, 9, T_LIFT, LV_RADIUS_CIRCLE);
    set_hidden(dot_, true);
    // The right label holds a time and takes the last 80 px, so the left one stops ten pixels
    // short of it. Both are Figtree at 20, the title in the heavier weight: a board title of
    // about thirty capitals fits, and the dots stand for the rest of a longer one.
    left_ = mk_label(root_, 0, 1, w - 90, 26, F(g_fonts.sans600_20), T_CHALK70, "");
    tracked(left_, 1);
    lv_label_set_long_mode(left_, LV_LABEL_LONG_MODE_DOTS);
    right_ = mk_label(root_, w - 80, 1, 80, 26, F(g_fonts.sans500_20), T_CHALK70, "");
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
    apply_breathing_();
  }

  // The live dot breathes over four seconds. What the face asked for is remembered, so the night
  // can stop the dot without the day having to be told to start it again.
  void set_breathing(bool on) {
    breathing_ = on;
    apply_breathing_();
  }
  // Night holds the dot still. A hallway at two in the morning has nothing moving in it, and a
  // fade that never rests is the one thing on this face that would catch an eye in the dark.
  void set_night(bool night) {
    if (night_ == night) return;
    night_ = night;
    apply_breathing_();
  }

  lv_obj_t *root() const { return root_; }

 private:
  // The dot is left at full opacity whenever it is not breathing, so a still dot is a lit dot
  // and never one caught half way through a fade.
  void apply_breathing_() {
    if (dot_ == nullptr) return;
    lv_anim_delete(dot_, anim_bg_opa_cb_);
    lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);
    if (!breathing_ || night_ || lv_obj_has_flag(dot_, LV_OBJ_FLAG_HIDDEN)) return;
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

  static void anim_bg_opa_cb_(void *var, int32_t v) {
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(var), (lv_opa_t) v, 0);
  }

  lv_obj_t *root_ = nullptr, *dot_ = nullptr, *left_ = nullptr, *right_ = nullptr;
  Tok left_tok_ = T_CHALK70, right_tok_ = T_CHALK70;
  bool breathing_ = false, night_ = false;
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
      : id_(std::move(id)), type_(type), margin_(16) {
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
  // informational status is dropped, because a face full of data has nothing to gain from one.
  // The clock and the pairing page override this.
  virtual void set_status(const std::string &msg, bool problem) {
    if (!problem) return;
    show_problem_(msg);
  }

  // The palette itself is global and repaints through the shared styles. What a view has to do
  // for itself is the movement (a breathing dot has no business breathing at night) and the few
  // colours that were set by value rather than by style; a view with neither does nothing here.
  // A strip that was never built ignores it, which is how the board face gets away with this.
  virtual void set_night(bool night) { strip_.set_night(night); }

  lv_obj_t *root() const { return root_; }
  const std::string &id() const { return id_; }
  char type() const { return type_; }
  // The document's module id, kept so PageHost can tell a weather page from a to-do list when it
  // reconciles: both are generic pages and they are not the same face.
  const std::string &module() const { return module_; }
  void set_module(const std::string &m) { module_ = m; }

 protected:
  // The chrome the generic template shares with the board: the strip, with the page title on the
  // left and the clock on the right.
  void build_header_(const char *title) {
    strip_.build(root_, margin_);
    strip_.set_left_font(F(g_fonts.sans600_20));
    strip_.set_left(title, T_CHALK);
    build_problem_();
  }
  // A problem worth support seeing, on one line near the foot of the face.
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
  // The clock owns the right of the strip on every face that has one. Written once a minute:
  // the tick comes every second and a label rewrite is a redraw.
  void tick_header_(const esphome::ESPTime &now) {
    std::string hm = now.is_valid() ? g_nowhm : std::string("--:--");
    if (hm == strip_hm_) return;
    strip_hm_ = hm;
    strip_.set_right(hm, T_CHALK70);
  }

  lv_obj_t *root_ = nullptr;
  lv_obj_t *status_ = nullptr;
  StatusStrip strip_;
  std::string id_, module_, status_text_, strip_hm_;
  char type_;
  int margin_;
};

// ---- clock: always page one. No strip, no diary line; the date sits above the numerals and the
// temperature below them, and the foot row is a notice line that only shows when there is one.
//
// Geometry, from ClockFace in the design system with 16 px of padding all round and no strip at
// all: the hairline still sits at 424 and the foot row still runs 440 to 464, under the page dots
// when those show. The numerals stand 121 px tall at 168 px and start 39 px below their label's
// top, so putting the visible digits' centre on y=240 (the middle of the 480 px face) takes a
// label top of 240 - 39 - 121/2 = 140.5, rounded to 141, which puts their visible ink at 179-300 in
// the rendered PNG. The date and the weather line labels are Figtree 400 at 24 px with auto height,
// so their box top is not their ink top, and the two were placed by rendering
// firmware/sim/out/clock.png, reading the ink back off its pixels and moving the label until the
// gap either side of the numerals was 30 px: the date at y=124 puts its ink at 127-149, 30 px above
// the digits' ink at 179; the weather line at y=327 puts its ink at 330-347, 30 px below the digits'
// ink at 300. The line in the middle of the face is only ever the wait for SNTP, and the numerals
// and the date are empty while it shows.
class ClockView : public PageView {
 public:
  explicit ClockView(lv_obj_t *parent) : PageView(parent, "__clock", 'c') {
    date_ = mk_label(root_, 16, 124, 448, 0, F(g_fonts.sans400_24), T_CHALK70, "");
    lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, 0);

    // The clock font holds digits and a colon and nothing else, so the label starts empty rather
    // than showing "--:--": four missing glyphs would draw as four boxes.
    // The tracking comes off after the last figure too, which pulls a centred line 5 px to the
    // left of centre; the label starts 10 px in to put it back.
    time_ = mk_label(root_, 10, 141, 470, 0, F(g_fonts.clock168), T_CHALK, "");
    lv_obj_set_style_text_align(time_, LV_TEXT_ALIGN_CENTER, 0);
    tracked(time_, -10);
    waiting_ = mk_label(root_, 16, 226, 448, 28, F(g_fonts.sans500_20), T_CHALK70, copy::CLOCK_WAITING);
    lv_obj_set_style_text_align(waiting_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(waiting_, LV_LABEL_LONG_MODE_DOTS);

    weather_line_ = mk_label(root_, 16, 327, 448, 0, F(g_fonts.sans400_24), T_CHALK70, "");
    lv_obj_set_style_text_align(weather_line_, LV_TEXT_ALIGN_CENTER, 0);
    // Truncate with dots rather than wrap: a width>0 label defaults to CLIP (see mk_label), which
    // just cuts the glyphs off mid-line, and the simulator's font renderer measures glyph advances
    // a little differently from the device's, so a line that just fits on one does not on the
    // other. DOTS is the same fallback every other single-line label on the board uses.
    lv_label_set_long_mode(weather_line_, LV_LABEL_LONG_MODE_DOTS);
    set_hidden(weather_line_, true);

    // The foot row: a hairline and one centred line, shown only while a notice or a problem has
    // something to say. No dot here any more, the diary having left the face entirely.
    rule_ = mk_rule(root_, 16, 424, 448, T_RAISED);
    line_ = mk_label(root_, 16, 440, 448, 24, F(g_fonts.sans400_18), T_CHALK70, "");
    lv_obj_set_style_text_align(line_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(line_, LV_LABEL_LONG_MODE_DOTS);
    refresh_foot_();
  }

  void tick(esphome::ESPTime now) override {
    if (!now.is_valid()) {
      lv_label_set_text(time_, "");
      lv_label_set_text(date_, "");
      set_hidden(waiting_, false);
      last_hm_.clear();
      return;
    }
    if (g_nowhm == last_hm_) return;
    last_hm_ = g_nowhm;
    lv_label_set_text(time_, short_time(g_nowhm).c_str());
    lv_label_set_text(date_, date_text(now).c_str());
    set_hidden(waiting_, true);
  }

  // 24 hour without a leading zero, as the design has it: "8:41", "17:05".
  static std::string short_time(const std::string &hm) {
    return (hm.size() == 5 && hm[0] == '0') ? hm.substr(1) : hm;
  }

  // "Fri 18 Sep". Built by hand: newlib's strftime has no day-without-padding format, and fixed
  // English names keep the string ASCII whatever locale the runtime has.
  static std::string date_text(const esphome::ESPTime &now) {
    static const char *DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int dow = (int) now.day_of_week - 1;
    int mon = (int) now.month - 1;
    if (dow < 0 || dow > 6 || mon < 0 || mon > 11) return "";
    return std::string(DAYS[dow]) + " " + std::to_string((int) now.day_of_month) + " " + MONTHS[mon];
  }

  // The one line PageHost builds from the weather page's temperature, current conditions and
  // wind, shown centred under the numerals. Hidden rather than blank when the document has none
  // of the three.
  void set_weather_line(const std::string &line) {
    if (line == weather_line_text_) return;
    weather_line_text_ = line;
    lv_label_set_text(weather_line_, weather_line_text_.c_str());
    set_hidden(weather_line_, weather_line_text_.empty());
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
  // One row at the foot of the face, and three things that want it. A firmware update is the
  // loudest, then the document's notice, then a live problem; the hairline and the line show
  // together and hide together, since neither means anything without the other.
  void refresh_foot_() {
    const std::string &msg = !notice_.empty()       ? notice_
                             : !doc_notice_.empty() ? doc_notice_
                                                    : problem_text_;
    lv_label_set_text(line_, msg.c_str());
    set_hidden(line_, msg.empty());
    set_hidden(rule_, msg.empty());
  }

  lv_obj_t *date_ = nullptr, *time_ = nullptr, *weather_line_ = nullptr, *waiting_ = nullptr;
  lv_obj_t *rule_ = nullptr, *line_ = nullptr;
  std::string last_hm_, weather_line_text_, notice_, problem_text_, doc_notice_;
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

  // The QR's two colours are set by value on the canvas rather than through a style, and they are
  // baked in when the code is drawn, so the palette turning over means drawing it again.
  void set_night(bool night) override {
    PageView::set_night(night);
    lv_qrcode_set_dark_color(qr_, col(T_NIGHT));
    lv_qrcode_set_light_color(qr_, col(T_CHALK));
    if (!qr_url_.empty()) lv_qrcode_update(qr_, qr_url_.c_str(), (uint32_t) qr_url_.size());
  }

  // What the QR carries. The host simulator prints it so the fragment contract stays checked;
  // nothing on the device reads it, and it is never logged.
  const std::string &qr_url() const { return qr_url_; }

 private:
  static const int PAD = 16, TILE = 200, QUIET = 12, GAP = 24;
  // The design's 360, widened to the full content width. LVGL breaks a line at a full stop as
  // readily as at a space, and at 360 that split hallboard.co.uk across two lines.
  static const int CAPTION_W = 448;

  // One slot, and three things that want it: a problem first, then the caption for whichever of
  // the two states the page is in.
  void refresh_caption_() {
    const char *text = copy::PAIR_WAITING;
    if (!problem_.empty())
      text = problem_.c_str();
    else if (code_shown_.size() == 6)
      text = copy::PAIR_SCAN;
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
    notice_ = mk_label(root_, 16, top + MARK + 6, 448, 0, F(g_fonts.sans400_18), T_CHALK, "");
    lv_obj_set_style_text_align(notice_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(notice_, LV_LABEL_LONG_MODE_DOTS);
    for (int i = 0; i < STEPS; i++) {
      steps_[i] = mk_label(root_, 16, list_y + i * (step_h + STEP_GAP), 448, step_h,
                           F(g_fonts.mono16), T_PENDING, "");
      lv_obj_set_style_text_align(steps_[i], LV_TEXT_ALIGN_CENTER, 0);
      tracked(steps_[i], 1);
      lv_label_set_long_mode(steps_[i], LV_LABEL_LONG_MODE_DOTS);
    }
    help_ = mk_label(root_, 16, help_y, 448, help_h, F(g_fonts.sans400_18), T_CHALK70, "");
    lv_obj_set_style_text_align(help_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(help_, LV_LABEL_LONG_MODE_WRAP);
    detail_ = mk_label(root_, 16, detail_y, 448, meta_h, F(g_fonts.mono15), T_CHALK50, "");
    lv_obj_set_style_text_align(detail_, LV_TEXT_ALIGN_CENTER, 0);
    tracked(detail_, 1);
    lv_label_set_long_mode(detail_, LV_LABEL_LONG_MODE_DOTS);
    support_ = mk_label(root_, 16, support_y, 448, meta_h, F(g_fonts.mono15), T_CHALK50, "");
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
      set_help_(copy::BOOT_BACKEND_HELP);
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
        if (!started_) return begin_step_(0, copy::BOOT_WIFI);
        if (have_net_) {
          finish_step_(0, copy::BOOT_WIFI_DONE);
          return advance_(S_CONTENT);
        }
        // Twenty seconds without a network and the household needs telling how to fix it. A
        // status string that already said so wins: it is more specific than this one.
        if (help_text_.empty() && (int32_t) (now - t0_) > 20000) set_help_(copy::BOOT_WIFI_HELP);
        return;
      case S_CONTENT:
        if (!started_) return begin_step_(1, copy::BOOT_PAGES);
        if (unpaired_) {
          finish_step_(1, copy::BOOT_PAGES_DONE);
          return advance_(S_PAIR);
        }
        if (have_doc_) {
          finish_step_(1, copy::BOOT_PAGES_DONE);
          content_at_ = now;
          return advance_(S_TIME);
        }
        return;
      case S_TIME:
        if (!started_) return begin_step_(2, copy::BOOT_CLOCK);
        if (have_time_) {
          finish_step_(2, copy::BOOT_CLOCK_DONE);
          return advance_(S_READY);
        }
        // SNTP is not worth waiting on: the clock page carries the waiting line instead.
        if ((int32_t) (now - content_at_) > 30000) {
          set_help_(copy::CLOCK_NOT_SET);
          return advance_(S_READY);
        }
        return;
      case S_READY:
        if (!started_) return begin_step_(3, copy::BOOT_READY);
        return start_fade_();
      case S_PAIR:
        if (!started_) return begin_step_(3, copy::BOOT_PAIR);
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
  // the mono face the steps are set in holds no symbol that could stand in for one.
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
// 16 px of padding all round, the strip every face has, then four rows with the design's 22 px
// gap that fill the face to the bottom margin, each on its own card: rows at 60 (raised, 86 tall), 168, 274 and 380
// (84 tall each, ending at 464), with the page dots over the last one while they show. A problem
// line has no band of its own, so it takes the fourth row's place.
//
// The parser keeps five rows because the document may carry five; the fifth is not drawn, and
// nothing can long-press a row that is not on screen.
class BoardView : public PageView {
 public:
  static const int ROWS_SHOWN = 4;

  BoardView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'b') {
    // The same strip as every other face: the board's title in the transit hue, the clock right.
    strip_.build(root_, margin_);
    strip_.set_dot(T_TRANSIT);

    for (int i = 0; i < ROWS_SHOWN; i++) build_row_(rows_[i], i);
    build_card_();

    // A problem worth support seeing, in the fourth row's place. Informational statuses never
    // reach it.
    build_problem_();
    lv_obj_set_y(status_, row_y(3) + (ROW_H - 20) / 2);

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
    // "FARNCOMBE TO WATERLOO", "ARRIVALS AT FARNCOMBE", a stop's name: the backend's title as
    // it is. The strip has room for about thirty capitals and puts the dots after them.
    strip_.set_left(upper(pg.title), T_TRANSIT);
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
      // the wait on TfL. A time goes under the booked one it revises, in the status's colour; a
      // wait ("3 min", "Due") is not a time and stays in front of the status.
      bool revised = d.expected.size() == 5 && d.expected[2] == ':';
      label_text(r.expected, revised ? d.expected : "");
      set_hidden(r.expected, !revised);
      set_tok(r.expected, row_colour(d.colour));
      label_text(r.status, d.expected.empty() || revised ? d.status
                                                         : d.expected + " \xC2\xB7 " + d.status);
      set_tok(r.status, row_colour(d.colour));
      // A cancelled service is dimmed where it stands. It is never dropped or moved: the
      // household needs to see that the train they were going to catch is not running.
      lv_obj_set_style_opa(r.box, d.colour == "R" ? LV_OPA_60 : LV_OPA_COVER, 0);
      set_hidden(r.box, false);
      uids_.push_back(d.uid);
    }
    set_hidden(card_, n > 0);
    if (n == 0) fill_card_(pg);
  }

  // The problem line sits where the fourth row does, so that row steps aside while there is one
  // and comes back when the problem clears or the next document arrives.
  void set_status(const std::string &msg, bool problem) override {
    if (!problem) return;
    show_problem_(msg);
    if (uids_.size() > 3) set_hidden(rows_[3].box, !msg.empty());
  }

  void tick(esphome::ESPTime now) override { tick_header_(now); }

  const std::string &uid_at(int i) const {
    static const std::string none;
    if (i < 0 || i >= (int) uids_.size()) return none;
    return uids_[i];
  }

 private:
  // The face's geometry, from DepartureRow: rows 448 wide inside a 16 px margin, the first one
  // raised and a little taller for its heavier type.
  static const int PAD = 16, ROW_X = 16, ROW_W = 448;
  static const int ROW0_Y = 60, ROW0_H = 86, ROW_H = 84, ROW_GAP = 22;

  static int row_y(int i) {
    return i == 0 ? ROW0_Y : ROW0_Y + ROW0_H + ROW_GAP + (ROW_H + ROW_GAP) * (i - 1);
  }
  static int row_h(int i) { return i == 0 ? ROW0_H : ROW_H; }

  struct Row {
    lv_obj_t *box = nullptr, *time = nullptr, *dest = nullptr, *status = nullptr, *plat = nullptr;
    lv_obj_t *expected = nullptr;
  };

  // Columns inside a row, measured from the row's own left edge: 16 px of padding, a 72 px time,
  // a 12 px gap, then the destination over its status, with the platform right-aligned at the
  // far end. The time and the platform both sit on the destination's line rather than the row's
  // middle: the three are read together, and it leaves the status the full width, because the
  // composed status strings are long and the design's own are not. The platform is set as the
  // destination is, size and weight, so the line reads as one.
  void build_row_(Row &r, int i) {
    int h = row_h(i), y = row_y(i);
    bool first = i == 0;
    // The destination and its status are centred in the row's height.
    const int dest_h = first ? 30 : 28, status_h = 24;
    int pad = (h - (dest_h + 2 + status_h)) / 2;
    // Every row is a card: the first raised, the rest on the weather face's resting card colour.
    r.box = mk_panel(root_, ROW_X, y, ROW_W, h, first ? T_RAISED : T_CARD, 16);
    // The columns are measured off ROW_W rather than written down, so the row follows the page's
    // margin: 16 of padding at either end, a 72 px time, a 12 px gap, the platform at the far end.
    const int rpad = 16, time_w = 72, text_x = 100, plat_w = 72, plat_x = ROW_W - rpad - plat_w;
    // The time is set as the destination is, so the line reads as one. Figtree's figures are not
    // all one width, so the column is as wide as the widest time ("00:00" at 600/24) and the
    // destinations start from the same place whatever the time is.
    r.time = mk_label(r.box, rpad, pad, time_w, dest_h,
                      F(first ? g_fonts.sans600_24 : g_fonts.sans500_22),
                      first ? T_CHALK : T_TIME2);
    // The revised time of a late train, under the booked one and on the status's line.
    r.expected = mk_label(r.box, rpad, pad + dest_h + 2, time_w, status_h, F(g_fonts.sans500_18),
                          T_CHALK70);
    set_hidden(r.expected, true);
    r.dest = mk_label(r.box, text_x, pad, plat_x - 14 - text_x, dest_h,
                      F(first ? g_fonts.sans600_24 : g_fonts.sans500_22),
                      first ? T_CHALK : T_TITLE2);
    lv_label_set_long_mode(r.dest, LV_LABEL_LONG_MODE_DOTS);
    // The status is set in Figtree rather than mono: at a size worth reading from the hall a
    // monospaced "09:04 . Delayed . 10 coaches . SWR" does not fit the row, and this does.
    r.status = mk_label(r.box, text_x, pad + dest_h + 2, ROW_W - rpad - text_x, status_h,
                        F(g_fonts.sans500_18), T_CHALK70);
    lv_label_set_long_mode(r.status, LV_LABEL_LONG_MODE_DOTS);
    r.plat = mk_label(r.box, plat_x, pad, plat_w, dest_h,
                      F(first ? g_fonts.sans600_24 : g_fonts.sans500_22),
                      first ? T_CHALK : T_CHALK70);
    lv_obj_set_style_text_align(r.plat, LV_TEXT_ALIGN_RIGHT, 0);
    set_hidden(r.box, true);
  }

  // The one card an empty or unreachable board shows, where the first row would be. Its height
  // follows its sentence.
  void build_card_() {
    card_ = mk_panel(root_, ROW_X, ROW0_Y, ROW_W, CARD_TEXT_Y + 24 + 24, T_DONEBG, 20);
    set_hidden(card_, true);
    card_label_ = mk_label(card_, 24, 24, ROW_W - 48, 20, F(g_fonts.mono15), T_CHALK50, "");
    tracked(card_label_, 1);
    card_text_ = mk_label(card_, 24, CARD_TEXT_Y, ROW_W - 48, 0, F(g_fonts.sans400_18), T_CHALK70, "");
    lv_label_set_long_mode(card_text_, LV_LABEL_LONG_MODE_WRAP);
  }

  void fill_card_(const Page &pg) {
    // asof is 0 only when the backend could not build this board at all. Say so rather than
    // claiming there is nothing due.
    bool missing = pg.asof == 0;
    // A stop has no window to promise: only a rail board's adapter asks for one.
    bool stop = module_ == "bus" || module_ == "tube";
    const char *sentence = missing     ? copy::BOARD_NO_TIMETABLE
                           : stop      ? copy::BOARD_NONE_STOP
                           : arrivals_ ? copy::BOARD_NONE_ARRIVING
                                       : copy::BOARD_NONE_DUE;
    lv_label_set_text(card_label_, missing ? copy::OFFLINE : copy::NOTHING_DUE);
    lv_label_set_text(card_text_, sentence);
    set_hidden(card_, false);
    lv_obj_update_layout(card_);
    lv_obj_set_height(card_, CARD_TEXT_Y + lv_obj_get_height(card_text_) + 24);
  }

  static void touch_cb_(lv_event_t *e) { emit("TOUCH"); }
  static void long_cb_(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target_obj(e);
    emit("LONG|" + std::to_string((int) (intptr_t) lv_obj_get_user_data(o)));
  }

  // 24 of padding, the 20 px label and the design's 12 px gap.
  static const int CARD_TEXT_Y = 56;

  Row rows_[ROWS_SHOWN];
  lv_obj_t *card_ = nullptr, *card_label_ = nullptr, *card_text_ = nullptr;
  std::vector<std::string> uids_;
  bool arrivals_ = false;
};

// ---- day: the diary, laid out as DayFace in the design system.
//
// 16 px of padding, the strip at the top carrying the day and how much of it is left, then a
// vertical scroller from 60 to 464 holding seven days of rows. The scroller takes vertical drags
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
    strip_.set_dot(T_CALENDAR);
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
    // The calendar's name, which the parser puts in the page title.
    strip_.set_left(upper(pg.title), T_CALENDAR);
    render();
  }

  // The whole face is rebuilt on the minute: which event is next, how long it is until it starts
  // and how much of today is left all move with the clock, not with the document.
  void tick(esphome::ESPTime now) override {
    tick_header_(now);
    if (g_nowhm == last_hm_ && day_ == long_day(now)) return;
    last_hm_ = g_nowhm;
    day_ = long_day(now);
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
    for (const auto &it : events_) {
      if (spent_(it) || it.d != g_today || it.all_day) continue;
      next = &it;
      break;
    }

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
    empty_.set(copy::NOTHING_PLANNED, copy::AGENDA_EMPTY);
  }

 private:
  // The face's geometry, from DayFace and AgendaRow: rows 448 wide inside a 16 px margin, 16 of
  // padding in the raised one and 12/16 in the rest, and the board's 72 px time gutter with a
  // 12 px gap after it.
  static const int LIST_Y = 60, LIST_BOTTOM = 464, ROW_W = 448, ROW_H = 60, RAISED_H = 86;
  static const int GAP = 8, HEAD_H = 38, HEAD_GAP = 8, PAD = 16, TIME_W = 72, TEXT_X = 100;
  // The type is the board's: a line 30 tall on the raised row and 28 on the rest, with the time
  // set as the title is, and a 24 px second line in the raised row under a 2 px gap.
  static const int LINE_H = 28, RAISED_LINE_H = 30, SUB_H = 24;
  static const int DUR_W = 84;

  struct Card {
    lv_obj_t *box = nullptr, *rule = nullptr, *time = nullptr, *title = nullptr, *meta = nullptr;
  };
  struct Head {
    lv_obj_t *text = nullptr, *rule = nullptr;
  };

  // Today's timed events that have already ended are not the diary any more, and neither is
  // anything on a day before today. The backend never sends one in a document it has just built,
  // but the document on the wall at midnight was built yesterday, and the day turns here first.
  // Both days are "YYYYMMDD", so they compare as text.
  static bool spent_(const AgendaEvent &e) {
    if (g_today.size() == 8 && e.d.size() == 8 && e.d < g_today) return true;
    return !e.all_day && e.d == g_today && !g_nowhm.empty() && e.u.size() == 5 && e.u <= g_nowhm;
  }

  std::string heading_(const AgendaEvent &e) const {
    if (!tomorrow_.empty() && e.d == tomorrow_) return copy::TOMORROW_HEAD;
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
    if (raised) {
      // The time keeps the gutter it has in a resting row so the column runs straight down the
      // face, and the second line says how long there is rather than repeating the end time.
      int top = (RAISED_H - (RAISED_LINE_H + 2 + SUB_H)) / 2;
      place_label_(c.time, PAD, top, TIME_W, e.all_day ? "" : e.t);
      lv_obj_set_style_text_font(c.time, F(g_fonts.sans600_24), 0);
      set_tok(c.time, T_CHALK);
      place_label_(c.title, TEXT_X, top, ROW_W - TEXT_X - PAD, e.s);
      lv_obj_set_style_text_font(c.title, F(g_fonts.sans600_24), 0);
      set_tok(c.title, T_CHALK);
      std::string sub = e.all_day ? std::string(copy::ALL_DAY) : starts_in(e.t);
      if (!e.l.empty()) sub += (sub.empty() ? "" : " \xC2\xB7 ") + e.l;
      place_label_(c.meta, TEXT_X, top + RAISED_LINE_H + 2, ROW_W - TEXT_X - PAD, sub);
      lv_obj_set_style_text_font(c.meta, F(g_fonts.sans500_18), 0);
      lv_obj_set_style_text_align(c.meta, LV_TEXT_ALIGN_LEFT, 0);
      set_tok(c.meta, T_CHALK70);
      return;
    }
    int line_y = (ROW_H - LINE_H) / 2;
    place_label_(c.time, PAD, line_y, TIME_W, e.all_day ? "" : e.t);
    lv_obj_set_style_text_font(c.time, F(g_fonts.sans500_22), 0);
    set_tok(c.time, T_HOUR);
    place_label_(c.title, TEXT_X, line_y, ROW_W - TEXT_X - PAD - DUR_W - 8, e.s);
    lv_obj_set_style_text_font(c.title, F(g_fonts.sans500_22), 0);
    set_tok(c.title, T_TITLE2);
    // The length sits on the title's line in the second line's type, a little lower so the two
    // share a baseline.
    place_label_(c.meta, ROW_W - PAD - DUR_W, line_y + 4, DUR_W,
                 e.all_day ? std::string(copy::ALL_DAY) : duration_text(e.t, e.u));
    lv_obj_set_style_text_font(c.meta, F(g_fonts.sans500_18), 0);
    lv_obj_set_style_text_align(c.meta, LV_TEXT_ALIGN_RIGHT, 0);
    set_tok(c.meta, T_CHALK50);
  }

  Card &ensure_card_(int i) {
    while ((int) cards_.size() <= i) {
      Card c;
      c.box = mk_obj(list_, margin_, 0, ROW_W, ROW_H);
      set_hidden(c.box, true);
      c.rule = mk_rule(c.box, 0, 0, ROW_W, T_LINE);
      c.time = mk_label(c.box, PAD, 0, TIME_W, RAISED_LINE_H, F(g_fonts.sans500_22), T_HOUR);
      c.title = mk_label(c.box, TEXT_X, 0, 200, RAISED_LINE_H, F(g_fonts.sans500_22), T_TITLE2);
      lv_label_set_long_mode(c.title, LV_LABEL_LONG_MODE_DOTS);
      c.meta = mk_label(c.box, TEXT_X, 0, 200, SUB_H, F(g_fonts.sans500_18), T_CHALK50);
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
  std::string day_, last_hm_, tomorrow_;
};

// ---- sky: the weather face, laid out as SkyFace in the design system.
//
// 16 px of padding, the place and the stamp on a 28 px header, three cards (now, wind, today's
// rain), and the hours to come as a strip of bars in a card that fills the rest of the face
// down to the bottom margin.
//
// Icons are Meteocons line icons (firmware/icons/meteocons/), rasterised offline by
// firmware/sim/tools/icons.py into hb_icons.h as A8 alpha masks, drawn with lv_image and
// recoloured by a design token (T_CHALK on a card, T_CHALK70 in the hourly strip) through the
// ordinary shared-style mechanism, g_img in hb_tokens.h, so apply_palette() repaints them for
// free along with everything else.
class SkyView : public PageView {
 public:
  SkyView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'g') {
    strip_.build(root_, margin_);
    strip_.set_dot(T_WEATHER);
    build_cards_();
    build_strip_();
    empty_.build(root_, margin_, HEAD_BOTTOM + 40, TEXT_W);
    build_problem_();
  }

  void apply(const Page &pg) override {
    strip_.set_left(pg.place.empty() ? std::string(copy::WEATHER) : upper(pg.place), T_WEATHER);

    std::string temp = pg.temp, feels = pg.feels;
    bool hours = !temp.empty() && !pg.hours.empty();
    if (temp.empty()) {
      // A document from a backend older than this face, or one cached before it: the first row's
      // big value is the temperature. Nothing else on the page can be trusted to be about the
      // weather, so nothing else is shown, and the wind and rain cards fall back to their
      // icon-only state since wspd and rday cannot have arrived either.
      if (!pg.grows.empty() && is_number(pg.grows[0].value)) temp = pg.grows[0].value;
      feels.clear();
    }
    lv_obj_t *card_roots[CARDS] = {cards_[0].root, cards_[1].root, cards_[2].root};
    if (temp.empty()) {
      for (lv_obj_t *o : card_roots) set_hidden(o, true);
      set_hidden(card_, true);
      empty_.set(copy::OFFLINE, copy::SKY_NO_FORECAST);
      return;
    }
    empty_.hide();
    for (lv_obj_t *o : card_roots) set_hidden(o, false);
    fill_cards_(pg, temp, feels);
    set_hidden(card_, !hours);
    if (hours) {
      // An older backend sends no `i` on any slot: a row only exists when at least one slot has
      // something to put in it.
      bool any_icon = false;
      for (const HourSlot &h : pg.hours)
        if (!h.i.empty()) {
          any_icon = true;
          break;
        }
      layout_strip_(any_icon, CARDS_Y + cards_h_);
      fill_strip_(pg.hours);
    }
  }

  void tick(esphome::ESPTime now) override { tick_header_(now); }

  // A bar's colour is set by value (temp_bar_color), which no shared style reaches: every bar
  // already on the face has to be told when the palette turns over, the same as a reminder's ring.
  void set_night(bool night) override {
    PageView::set_night(night);
    for (int i = 0; i < COLS; i++) {
      Col &c = cols_[i];
      lv_obj_set_style_bg_color(c.bar, c.temp_str.empty() ? col(T_WEATHER) : temp_bar_color(c.temp_str, night), 0);
    }
  }

 private:
  // The face's geometry: the header ends at 44, the strip card's bottom sits on the 16 px margin,
  // the three cards start at 60, the same line the train board's first row and a list's first
  // row start on, and the strip card takes everything between them and that bottom margin.
  static const int HEAD_BOTTOM = 44, CARDS_Y = 60, TEXT_W = 448;

  // ---- the three cards: SkyFace's now/wind/rain row. 16 px side margins and 8 px between them
  // leaves (448 - 16) / 3 = 144 px wide each; 16 px corners, the same radius as the hourly card.
  // Every card has the same three rows, left aligned: a 32 px icon (with an optional short label
  // beside it, centred on the icon), one big value, one small line. 12 px padding, 8 px between
  // the icon and the value row and 4 px between the value and the sub line make the card 144 px
  // tall. A pass that also fitted a headline and a sentence under the cards squeezed these to
  // 10 px, a 28 px icon and 1/1 px gaps; with those gone the room came back. A card's own height
  // never depends on its data, only on the numbers below and the three fonts involved, so it is
  // worked out once, in build_cards_, and never revisited.
  static const int CARDS = 3, CARD_GAP = 8, CARD_PAD = 12, CARD_RADIUS = 16;
  static const int CARD_W = (TEXT_W - (CARDS - 1) * CARD_GAP) / CARDS;
  static const int CARD_ICON = 32, ICON_LABEL_GAP = 6, CARD_ROW_GAP1 = 8, CARD_ROW_GAP2 = 4;

  struct Card {
    lv_obj_t *root = nullptr, *icon = nullptr, *label = nullptr, *value = nullptr, *sub = nullptr;
  };

  void build_cards_() {
    int label_h = lv_font_get_line_height(F(g_fonts.sans600_20));
    int value_h = lv_font_get_line_height(F(g_fonts.sans600_46));
    int sub_h = lv_font_get_line_height(F(g_fonts.sans400_18));
    cards_h_ = CARD_PAD + CARD_ICON + CARD_ROW_GAP1 + value_h + CARD_ROW_GAP2 + sub_h + CARD_PAD;
    for (int i = 0; i < CARDS; i++) {
      Card &c = cards_[i];
      int x = margin_ + i * (CARD_W + CARD_GAP);
      c.root = mk_panel(root_, x, CARDS_Y, CARD_W, cards_h_, T_CARD, CARD_RADIUS);
      c.icon = mk_icon(c.root, CARD_PAD, CARD_PAD, T_CHALK);
      int label_x = CARD_PAD + CARD_ICON + ICON_LABEL_GAP;
      c.label = mk_label(c.root, label_x, CARD_PAD + (CARD_ICON - label_h) / 2, CARD_W - label_x - CARD_PAD,
                          label_h, F(g_fonts.sans600_20), T_CHALK, "");
      lv_label_set_long_mode(c.label, LV_LABEL_LONG_MODE_DOTS);
      int value_y = CARD_PAD + CARD_ICON + CARD_ROW_GAP1;
      c.value = mk_label(c.root, CARD_PAD, value_y, CARD_W - 2 * CARD_PAD, value_h, F(g_fonts.sans600_46),
                          T_CHALK, "");
      // The card's own numbers never need it (fig600_46 was sized for "-12°" and "100%" to both
      // fit at 120 px, and 12 px padding leaves exactly 120), but the sub line's "mph, gusts " plus up to
      // 4 more characters can, on an unrealistically wide gust reading, so it gets the same
      // truncate-with-dots every other label that might overflow in this file uses, rather than a
      // hard, ellipsis-less clip.
      c.sub = mk_label(c.root, CARD_PAD, value_y + value_h + CARD_ROW_GAP2, CARD_W - 2 * CARD_PAD, sub_h,
                        F(g_fonts.sans400_18), T_CHALK70, "");
      lv_label_set_long_mode(c.sub, LV_LABEL_LONG_MODE_DOTS);
    }
  }

  // Card 1 is the temperature, exactly as the old hero was: same source, same older-backend
  // fallback (both resolved by the caller, in `temp` and `feels`), no label, ever. Card 2 is the
  // wind: the icon is always there, whether or not there is a reading, but the label, the value
  // and the mph line come and go with `wspd`, the one field that makes this a reading rather
  // than an empty card with an icon on it; `wdir` and `gust` are each their own field again
  // inside that, so either can be missing while the other is not. Card 3 is today's rain: no
  // label, ever, and an umbrella rather than a raindrop once it is worth carrying one.
  void fill_cards_(const Page &pg, const std::string &temp, const std::string &feels) {
    Card &now = cards_[0];
    set_icon(now.icon, pg.grows.empty() ? "" : pg.grows[0].icon, CARD_ICON);
    set_hidden(now.label, true);
    label_text(now.value, temp + "\xC2\xB0");   // U+00B0
    label_text(now.sub, feels.empty() ? "" : copy::FEELS + feels + "\xC2\xB0");
    set_hidden(now.sub, feels.empty());

    Card &wind = cards_[1];
    set_icon(wind.icon, "wind", CARD_ICON);
    bool has_wind = !pg.wspd.empty();
    label_text(wind.label, pg.wdir);
    set_hidden(wind.label, !has_wind || pg.wdir.empty());
    label_text(wind.value, pg.wspd);
    set_hidden(wind.value, !has_wind);
    label_text(wind.sub, pg.gust.empty() ? copy::MPH : std::string(copy::MPH_GUSTS) + pg.gust);
    set_hidden(wind.sub, !has_wind);

    Card &rain = cards_[2];
    bool has_rain = pg.rday >= 0;
    set_icon(rain.icon, has_rain && pg.rday >= 50 ? "umbrella" : "raindrop", CARD_ICON);
    set_hidden(rain.label, true);
    label_text(rain.value, has_rain ? std::to_string(pg.rday) + "%" : "");
    set_hidden(rain.value, !has_rain);
    label_text(rain.sub, copy::RAIN_TODAY);
    set_hidden(rain.sub, !has_rain);
  }

  // The strip card, from SkyFace: six columns 60 wide with 8 between them inside 24 of padding.
  // The six columns measure 400 on a 68 pitch, which is what centres them in the 448 card.
  //
  // The card's top sits STRIP_GAP under the three cards and its bottom on the 16 px margin, so
  // its height is fixed by the cards, not by its contents. Inside it, top to bottom: a 24 px icon
  // row and a hairline under it, when the document has any icons, then the chance of rain and a
  // second hairline, then the temperatures over their bars and the hours under them. A backend
  // sending no icon draws no icon row and no first hairline. layout_strip_ works out what the
  // room left over from the fixed rows buys every time a document arrives: the bars grow up to
  // BAR_MAX, and anything beyond that is shared between the gaps either side of the hairlines,
  // so the rows spread out rather than the card gaining empty padding. The wind row this card
  // once had (a per-slot "SW 12" under the rain percentages) is gone: the backend stopped sending
  // hourly wind, and the room it held goes back to the bars.
  static const int COLS = 6, COL_W = 60, COL_PITCH = 68, CARD_PAD2 = 24, BAR_MAX = 88, BAR_MIN = 22, SEP = 9, RAIN_GAP = 16, CARD_FOOT = 14;
  // The icon row and the gap under it. The gap is a little wider than SEP because an icon's ink
  // runs to within a pixel of its box, where a line of text carries its own leading: 12 px, with
  // the hairline in the middle of it, leaves the same clear space above and below that hairline.
  static const int STRIP_ICON = 24, ICON_SEP = 12;
  // Between the three cards and the top of the strip card, the same 16 px as the side margins.
  static const int STRIP_GAP = 16;

  struct Col {
    lv_obj_t *root = nullptr, *icon = nullptr, *temp = nullptr, *bar = nullptr, *hour = nullptr,
             *rain = nullptr;
    // The bar's own colour is set by value (temp_bar_color), not through the T_WEATHER style, so
    // it has to be remembered here for set_night to repaint it from the other table; empty is the
    // slot with no reading, which keeps the plain module hue rather than a scale colour.
    std::string temp_str;
  };

  // Makes the card and its columns; every position inside them is layout_strip_'s, which runs
  // here once for the no-icon layout and again for each document. Nothing shows the card before
  // the first document anyway, since it starts hidden.
  void build_strip_() {
    int hh = lv_font_get_line_height(F(g_fonts.sans500_16));
    card_ = mk_panel(root_, margin_, 0, TEXT_W, 0, T_CARD, 16);
    set_hidden(card_, true);
    for (int i = 0; i < COLS; i++) {
      Col &c = cols_[i];
      c.root = mk_obj(card_, CARD_PAD2 + COL_PITCH * i, CARD_PAD2, COL_W, 0);
      c.icon = mk_icon(c.root, (COL_W - STRIP_ICON) / 2, 0, T_CHALK70);
      c.rain = mk_label(c.root, 0, 0, COL_W, hh, F(g_fonts.sans500_16), T_HOUR, "");
      lv_obj_set_style_text_align(c.rain, LV_TEXT_ALIGN_CENTER, 0);
      c.temp = mk_label(c.root, 0, 0, COL_W, hh, F(g_fonts.sans500_16), T_HEADLINE, "");
      lv_obj_set_style_text_align(c.temp, LV_TEXT_ALIGN_CENTER, 0);
      // Anchored at the bottom of the bar area, which is what makes a row of bars a chart.
      c.bar = mk_panel(c.root, 0, 0, COL_W, BAR_MIN, T_WEATHER, 4);
      c.hour = mk_label(c.root, 0, 0, COL_W, hh, F(g_fonts.sans500_16), T_HOUR, "");
      lv_obj_set_style_text_align(c.hour, LV_TEXT_ALIGN_CENTER, 0);
      set_hidden(c.root, true);
    }
    // Two hairlines across the six columns: one under the icon row, only while there is one, and
    // one under the chances of rain, always.
    int rule_w = COL_PITCH * (COLS - 1) + COL_W;
    icon_rule_ = mk_rule(card_, CARD_PAD2, 0, rule_w, T_LINE);
    hairline_ = mk_rule(card_, CARD_PAD2, 0, rule_w, T_LINE);
    layout_strip_(false, CARDS_Y + cards_h_);
  }

  // Recomputes the strip card's vertical geometry for the document that just arrived: whether the
  // icon row and its hairline are drawn, how tall the bars can be, and how far apart the rows
  // sit. `cards_bottom` is where the three cards end. Must run before fill_strip_, which knows
  // each bar's own height and so positions c.temp itself. See the comment on the geometry
  // constants above for the rule this follows.
  void layout_strip_(bool any_icon, int cards_bottom) {
    int hh = lv_font_get_line_height(F(g_fonts.sans500_16));
    int th = hh;
    card_y_ = cards_bottom + STRIP_GAP;
    card_h_ = 480 - margin_ - card_y_;
    int inner = card_h_ - CARD_PAD2 - CARD_FOOT;
    // Everything in a column but the bars and the room between the rows: the icon row (24 px, its
    // own size, not a line height) and its hairline's gap, then rain, its hairline's gap, the gap
    // down to the temperatures, the temperature, 6 px either side of the bar area, and the hour.
    int fixed = (any_icon ? STRIP_ICON + ICON_SEP : 0) + hh + SEP + RAIN_GAP + th + 6 + 6 + hh;
    int bar_max = inner - fixed;
    if (bar_max > BAR_MAX) bar_max = BAR_MAX;
    if (bar_max < BAR_MIN) bar_max = BAR_MIN;
    // What is left over is shared between the gaps around the hairlines and the one down to the
    // temperatures, so the rows spread out evenly; any odd pixels go to that last gap.
    int spare = inner - fixed - bar_max;
    if (spare < 0) spare = 0;
    int gaps = any_icon ? 3 : 2;
    int each = spare / gaps;
    int sep = SEP + each;
    int rain_gap = RAIN_GAP + spare - each * (gaps - 1);
    int icon_sep = ICON_SEP + each;
    int rain_y = any_icon ? STRIP_ICON + icon_sep : 0;
    int top = rain_y + hh + sep + rain_gap;
    bar_top_ = top + th + 6;
    temp_h_ = th;
    bar_max_ = bar_max;
    lv_obj_set_pos(card_, margin_, card_y_);
    lv_obj_set_size(card_, TEXT_W, card_h_);
    set_hidden(icon_rule_, !any_icon);
    lv_obj_set_pos(icon_rule_, CARD_PAD2, CARD_PAD2 + STRIP_ICON + icon_sep / 2);
    lv_obj_set_pos(hairline_, CARD_PAD2, CARD_PAD2 + rain_y + hh + sep / 2);
    for (int i = 0; i < COLS; i++) {
      Col &c = cols_[i];
      lv_obj_set_height(c.root, inner);
      lv_obj_set_y(c.rain, rain_y);
      // c.temp is repositioned per column in fill_strip_, which runs right after this and knows
      // each bar's own height; c.hour sits below the whole bar area, the same for every column.
      lv_obj_set_y(c.hour, bar_top_ + bar_max + 6);
    }
  }

  // The focus column is the first hour at half a chance of rain or more, which is the hour the
  // household most needs to know about. With nothing above half, the next hour is the one to read. A bar's height
  // is the temperature, because the number over it is: the warmest of the six slots is the full
  // height and the coldest about a third of it, so the strip reads as a day warming and cooling.
  // The bar's colour is a second, independent read of the same number: temp_bar_color's fixed
  // scale, so -5 C is the same colour on every day the board ever shows, not just the coldest of
  // that day's six slots. The chance of rain is a small percentage along the top of the card,
  // over a hairline, in the weather hue from half a chance up. The icon above it is plain: a slot
  // with nothing to say draws an empty cell rather than hide the whole row, which is
  // layout_strip_'s call to make.
  void fill_strip_(const std::vector<HourSlot> &hours) {
    int focus = 0, lo = 0, hi = 0;
    bool any = false, found = false;
    for (size_t i = 0; i < hours.size(); i++) {
      if (hours[i].r >= 50 && !found) {
        focus = (int) i;
        found = true;
      }
      if (hours[i].t.empty()) continue;
      int v = atoi(hours[i].t.c_str());
      if (!any || v < lo) lo = v;
      if (!any || v > hi) hi = v;
      any = true;
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
      // The coldest hour keeps a third of the height: a bar of nothing would read as a missing
      // reading, and so would an hour with no temperature, which gets the same.
      int h = BAR_MIN;
      if (!s.t.empty()) {
        h = hi > lo ? BAR_MIN + (bar_max_ - BAR_MIN) * (atoi(s.t.c_str()) - lo) / (hi - lo)
                    : (BAR_MIN + bar_max_) / 2;
      }
      lv_obj_set_pos(c.bar, 0, bar_top_ + bar_max_ - h);
      lv_obj_set_height(c.bar, h);
      lv_obj_set_y(c.temp, bar_top_ + bar_max_ - h - 6 - temp_h_);
      c.temp_str = s.t;
      lv_obj_set_style_bg_color(c.bar, s.t.empty() ? col(T_WEATHER) : temp_bar_color(s.t, g_night), 0);
      int r = s.r < 0 ? 0 : s.r > 100 ? 100 : s.r;
      label_text(c.rain, std::to_string(r) + "%");
      set_tok(c.rain, r >= 50 ? T_WEATHER : T_HOUR);
      set_icon(c.icon, s.i, STRIP_ICON);
      label_text(c.hour, s.h);
      set_tok(c.hour, i == focus ? T_TIME2 : T_HOUR);
      set_hidden(c.root, false);
    }
  }

  lv_obj_t *card_ = nullptr, *icon_rule_ = nullptr, *hairline_ = nullptr;
  Card cards_[CARDS];
  Col cols_[COLS];
  EmptyCard empty_;
  int cards_h_ = 0, card_h_ = 0, card_y_ = 0, bar_top_ = 0, temp_h_ = 0, bar_max_ = 0;
};

// ---- list: the to-do face, laid out as ListFace in the design system.
//
// 16 px of padding, the short date and the stamp on the strip, the page's own title as an
// eyebrow, then the open items as rings. Nothing is ever ticked here: a done item never reaches
// the document, so there is no done state to draw and nothing on this face is a control.
class ListView : public PageView {
 public:
  static const int MAX_ROWS = 8;

  ListView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'g') {
    strip_.build(root_, margin_);
    strip_.set_dot(T_REMINDERS);
    strip_.set_left(copy::TODO_LIST, T_REMINDERS);
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
    for (auto &r : rows_) set_hidden(r.box, true);
    int n = (int) pg.grows.size();
    if (n > MAX_ROWS) n = MAX_ROWS;
    for (int i = 0; i < n; i++) {
      const GenericRow &d = pg.grows[i];
      Row &r = ensure_row_(i);
      // The item's own text is `a` (docs/screen-document.md); `v` is the day count, and is only
      // worth reading as the title on a page that put the text there instead.
      label_text(r.title, d.a.empty() ? d.value : d.a);
      // The backend writes it in lower case ("today", "1 day", "overdue"). It is set as the
      // diary's "All day" is, so it takes a capital and nothing more.
      std::string due = d.b;
      if (!due.empty()) due[0] = (char) toupper((unsigned char) due[0]);
      label_text(r.due, due);
      set_tok(r.due, upper(d.b).find("OVERDUE") == std::string::npos ? T_CHALK50 : T_LATE);
      set_hidden(r.box, false);
    }
    if (n > 0) {
      empty_.hide();
      return;
    }
    empty_.set(copy::ALL_DONE, copy::LIST_EMPTY);
  }

  void tick(esphome::ESPTime now) override { tick_header_(now); }

  // A ring is a border colour, which no shared style reaches: it is set by value when a row is
  // built, so every row already on the face has to be told when the palette turns over.
  void set_night(bool night) override {
    PageView::set_night(night);
    for (auto &r : rows_) lv_obj_set_style_border_color(r.ring, col(T_REMINDERS), 0);
  }

 private:
  // ReminderCheck at its large size: a 60 px row with 16 of padding, a 30 px ring and 14 after
  // it, on a 12 px gap. The eyebrow takes the 16 px of air under the strip.
  static const int LIST_Y = 60, LIST_BOTTOM = 464, ROW_W = 448, ROW_H = 60;
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
      r.due = mk_label(r.box, ROW_W - PAD - DUE_W, 20, DUE_W, 24, F(g_fonts.sans500_18), T_CHALK50);
      lv_obj_set_style_text_align(r.due, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_long_mode(r.due, LV_LABEL_LONG_MODE_DOTS);
      rows_.push_back(r);
    }
    return rows_[i];
  }
  static void touch_cb_(lv_event_t *e) { emit("TOUCH"); }

  lv_obj_t *list_ = nullptr;
  EmptyCard empty_;
  std::vector<Row> rows_;
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
  }

  void tick(esphome::ESPTime now) override { tick_header_(now); }

 private:
  static const int LIST_Y = 60, LIST_BOTTOM = 464, ROW_W = 448, ROW_H = 60, GAP = 8, PAD = 16;
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
    views_[0]->set_night(night_);
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
    // The palette is a global, so it goes back to day with everything else: one scenario in the
    // host simulator must not inherit the night the last one turned on.
    set_night(false);
    views_.clear();
    boot_.reset();
    dots_.destroy();
    if (host_ != nullptr) lv_obj_delete(host_);
    host_ = nullptr;
    cur_ = 0;
    ssid_.clear();
    weather_line_.clear();
  }

  // Reconcile the pages on screen with the document: reuse by id and type, create what is new,
  // delete what is gone, and stay on the page the user was looking at.
  void set_document(const Document &doc) {
    if (host_ == nullptr) return;
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
    read_weather_line_(doc);
    clock->set_weather_line(weather_line_);
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
    std::unique_ptr<PageView> clock = std::move(views_[0]);
    std::unique_ptr<PageView> pair;
    for (size_t i = 1; i < views_.size(); i++)
      if (views_[i] && views_[i]->type() == 'p') {
        pair = std::move(views_[i]);
        break;
      }
    views_.clear();
    if (!pair) {
      pair.reset(new PairingView(host_));
      pair->set_night(night_);
    }
    // The weather belonged to a household that no longer claims this device, and so did the
    // notice.
    weather_line_.clear();
    static_cast<ClockView *>(clock.get())->set_weather_line("");
    static_cast<ClockView *>(clock.get())->set_doc_notice("");
    static_cast<PairingView *>(pair.get())->set_code(code);
    views_.push_back(std::move(clock));
    views_.push_back(std::move(pair));
    layout_();
    show(cur_ < views_.size() ? cur_ : views_.size() - 1, false);
    if (boot_) boot_->on_unpaired(code);
    raise_boot_();   // the pairing view was just created on top of everything, including us
  }

  // `dir` is the way the household swiped: 1 forward (a flick to the left), -1 back, 0 to go by
  // the index. A step that wraps says which way it went, because the index cannot: the clock is
  // page one, and coming round to it from the last page is still a step forward.
  void show(size_t index, bool animate, int step_dir = 0) {
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
    bool forward = step_dir != 0 ? step_dir > 0 : index > cur_;
    int dir = forward ? -1 : 1;  // moving forward slides the old page off to the left
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
    show((size_t) idx, true, delta);
  }

  void tick(esphome::ESPTime now) {
    if (now.is_valid()) {
      g_nowhm = now.strftime("%H:%M");
      g_today = now.strftime("%Y%m%d");
      g_now_epoch = (uint32_t) now.timestamp;
      if (boot_) boot_->on_time_valid();
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

  // The night palette, on or off. hallboard.yaml's apply_brightness is the only caller: the
  // household's window and the two-minute lift a touch gives it are decided there, beside the
  // brightness, so the two never disagree about what time of night it is.
  //
  // A no-op when nothing has changed, because that script runs every 60 s and repainting the
  // whole object tree once a minute would be work for nothing. The state is kept so a view built
  // later (a page a new document brought, the pairing page) starts on the right palette.
  void set_night(bool night) {
    if (night == night_) return;
    night_ = night;
    hb::set_night(night);
    for (auto &v : views_) v->set_night(night);
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
  BoardView *current_board() const {
    PageView *v = current_view();
    return (v != nullptr && v->type() == 'b') ? static_cast<BoardView *>(v) : nullptr;
  }

 private:
  static constexpr size_t MAX_CONTENT = 8;

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
    // Built after the window opened, so it starts on the palette the board is already on rather
    // than flashing the day one until the next minute's re-evaluation.
    v->set_night(night_);
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

  // ---- the clock's weather line
  // The line under the numerals comes off the weather page the clock itself never sees, so it is
  // worked out here: temperature, current conditions and wind, joined by " · " (U+00B7), with any
  // missing part left out together with its separator. The temperature logic, including its
  // fallback to a `grows` row for a backend older than the `temp` field, is exactly SkyView's.
  void read_weather_line_(const Document &doc) {
    weather_line_.clear();
    const Page *weather = nullptr;
    bool named = false;
    for (const Page &pg : doc.pages) {
      if (pg.type != 'g') continue;
      if (!named && pg.module == "weather") {
        weather = &pg;
        named = true;
      } else if (weather == nullptr) {
        weather = &pg;
      }
    }
    if (weather == nullptr) return;
    std::string temp;
    if (!weather->temp.empty()) {
      temp = weather->temp + "\xC2\xB0";   // U+00B0
    } else if (!weather->grows.empty() && is_number(weather->grows[0].value)) {
      // A backend older than the weather face sends no `temp`, so the first row's big value
      // stands in for it, but only when it really is a number: `v` may be a day count or
      // anything else.
      temp = weather->grows[0].value + "\xC2\xB0";
    }
    std::vector<std::string> parts;
    if (!temp.empty()) parts.push_back(temp);
    if (!weather->cond.empty()) parts.push_back(weather->cond);
    if (!weather->wind.empty()) parts.push_back(weather->wind);
    for (size_t i = 0; i < parts.size(); i++) {
      if (i > 0) weather_line_ += " \xC2\xB7 ";   // U+00B7, middle dot
      weather_line_ += parts[i];
    }
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
  // The clock's weather line, built by read_weather_line_.
  std::string weather_line_;
  size_t cur_ = 0;
  // Which palette the board is on, so a view built later starts on the same one.
  bool night_ = false;
};

inline PageHost g_host;

}  // namespace hb
