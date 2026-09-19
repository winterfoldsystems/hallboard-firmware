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

// A page of any type. The three row vectors are a union in spirit: only the one matching `type`
// is ever filled.
struct Page {
  char type = 0;  // 'b' board, 'a' agenda, 'g' generic
  std::string id, title, module, mode;
  uint32_t asof = 0;
  bool stale = false;
  std::vector<BoardRow> rows;
  std::vector<AgendaEvent> events;
  std::vector<GenericRow> grows;
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
// Agenda display mode, mirrored into a restoring global by hallboard.yaml.
inline bool g_agenda_expanded = false;

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

// The stamp in the corner of every content face: "LIVE · 08:41", "SHOWING 08:12", "LOADING".
inline std::string stamp_text(uint32_t asof, bool stale) {
  if (asof == 0) return "LOADING";
  time_t t = (time_t) asof;
  struct tm lt;
  localtime_r(&t, &lt);
  char buf[8];
  strftime(buf, sizeof buf, "%H:%M", &lt);
  // U+00B7, the middle dot the design separates with.
  return stale_now(asof, stale) ? std::string("SHOWING ") + buf : std::string("LIVE \xC2\xB7 ") + buf;
}

// Uppercase an ASCII string, for the strip's left label and the agenda's day headings. Document
// text is ASCII by the time the parser is done with it, so there is nothing else to fold.
inline std::string upper(std::string s) {
  for (auto &c : s) c = (char) toupper((unsigned char) c);
  return s;
}

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

  // A transient line (Wi-Fi, backend errors) shown where the page has room for it. `problem` is
  // true when the message is something support would want to see; the clock page shows only
  // those, every other page puts the lot on a small line at the foot of the screen. The faces
  // decide where it belongs from S4 on; until then this is what keeps it visible.
  virtual void set_status(const std::string &msg, bool problem) {
    status_text_ = msg;
    if (status_ == nullptr) return;
    lv_label_set_text(status_, msg.c_str());
  }

  lv_obj_t *root() const { return root_; }
  const std::string &id() const { return id_; }
  char type() const { return type_; }

 protected:
  // The chrome the three content templates share: the strip, with the page title on the left and
  // the stamp (or, until one arrives, the clock) on the right, and the status line at the foot.
  void build_header_(const char *title) {
    strip_.build(root_, margin_);
    strip_.set_left_font(F(g_fonts.sans600_20));
    strip_.set_left(title, T_CHALK);
    status_ = mk_label(root_, margin_, 452, 480 - 2 * margin_, 20, F(g_fonts.mono15), T_CHALK50, "");
    tracked(status_, 1);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
    if (!status_text_.empty()) lv_label_set_text(status_, status_text_.c_str());
  }
  void set_title_(const std::string &title) { strip_.set_left(title, T_CHALK); }
  // The freshness stamp owns the right of the strip once a document has been through the page.
  void set_stamp_(uint32_t asof, bool stale) {
    stamp_ = stamp_text(asof, stale);
    strip_.set_right(stamp_, T_CHALK70);
  }
  void tick_header_(const esphome::ESPTime &now) {
    if (!stamp_.empty()) return;   // a stamp says more than the time does
    strip_.set_right(now.is_valid() ? g_nowhm : std::string("--:--"), T_CHALK70);
  }

  lv_obj_t *root_ = nullptr;
  lv_obj_t *status_ = nullptr;
  StatusStrip strip_;
  std::string id_, status_text_, stamp_;
  char type_;
  int margin_;
};

// ---- clock: always page one, with the date and, when the document carries a weather page, the
// first weather row underneath.
class ClockView : public PageView {
 public:
  explicit ClockView(lv_obj_t *parent) : PageView(parent, "__clock", 'c') {
    // The clock font holds digits, a colon, a space and a hyphen and nothing else, so the label
    // starts empty rather than showing "--:--": four missing glyphs would draw as four boxes.
    time_ = mk_label(root_, 0, 128, 480, 0, F(g_fonts.clock132), T_CHALK, "");
    lv_obj_set_style_text_align(time_, LV_TEXT_ALIGN_CENTER, 0);
    tracked(time_, -8);
    waiting_ = mk_label(root_, 0, 176, 480, 36, F(g_fonts.sans500_20), T_CHALK70, "Waiting for time...");
    lv_obj_set_style_text_align(waiting_, LV_TEXT_ALIGN_CENTER, 0);
    date_ = mk_label(root_, 0, 296, 480, 36, F(g_fonts.sans500_20), T_CHALK70, "");
    lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, 0);
    // Only shown while something is wrong, and only for a problem status or a notice carried by
    // the document: the clock page is what the household looks at, so it stays a clock until
    // support (or the backend) needs a line.
    problem_ = mk_label(root_, 24, 380, 432, 26, F(g_fonts.sans500_18), T_CHALK50, "");
    lv_obj_set_style_text_align(problem_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(problem_, LV_LABEL_LONG_MODE_DOTS);
    set_hidden(problem_, true);
    weather_ = mk_label(root_, 24, 424, 432, 28, F(g_fonts.sans500_18), T_CHALK70, "");
    lv_obj_set_style_text_align(weather_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(weather_, LV_LABEL_LONG_MODE_DOTS);
  }

  void tick(esphome::ESPTime now) override {
    if (!now.is_valid()) {
      lv_label_set_text(time_, "");
      lv_label_set_text(date_, "");
      set_hidden(waiting_, false);
      last_hm_.clear();
      return;
    }
    set_hidden(waiting_, true);
    if (g_nowhm != last_hm_) {
      last_hm_ = g_nowhm;
      lv_label_set_text(time_, last_hm_.c_str());
      lv_label_set_text(date_, date_text(now).c_str());
    }
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

  void set_weather(const std::string &line) {
    weather_text_ = line;
    refresh_line_();
  }
  // A firmware notice ("Updating firmware 12%", "Updated to 1.3.0") takes the weather line for as
  // long as it is set, then hands it back. Nothing else interrupts the clock.
  void set_notice(const std::string &msg) {
    if (notice_ == msg) return;
    notice_ = msg;
    refresh_line_();
    refresh_problem_();   // a firmware notice holds the document's notice back while it runs
  }
  // The document's own notice, from `settings.notice`. It shares the small grey line with a
  // problem status, which wins while it lasts, and unlike a problem it survives the next
  // document: only another document (or an empty notice in one) takes it away.
  void set_doc_notice(const std::string &msg) {
    if (doc_notice_ == msg) return;
    doc_notice_ = msg;
    refresh_problem_();
  }
  // Informational statuses are dropped: the clock page stays a clock. A problem is worth a small
  // grey line, cleared by clear_problem() as soon as a document arrives.
  void set_status(const std::string &msg, bool problem) override {
    if (!problem) return;
    if (problem_text_ == msg) return;
    problem_text_ = msg;
    refresh_problem_();
  }
  void clear_problem() {
    if (problem_text_.empty()) return;
    problem_text_.clear();
    refresh_problem_();
  }

 private:
  void refresh_line_() {
    lv_label_set_text(weather_, notice_.empty() ? weather_text_.c_str() : notice_.c_str());
    set_tok(weather_, notice_.empty() ? T_CHALK70 : T_CHALK);
  }
  // A live problem first, then the document's notice, and nothing at all while a firmware notice
  // is on the line below: an update in progress is not the moment for a billing line.
  void refresh_problem_() {
    const std::string &text = !problem_text_.empty() ? problem_text_
                              : notice_.empty()      ? doc_notice_
                                                     : empty_;
    lv_label_set_text(problem_, text.c_str());
    set_hidden(problem_, text.empty());
  }

  lv_obj_t *time_ = nullptr, *waiting_ = nullptr, *date_ = nullptr, *problem_ = nullptr, *weather_ = nullptr;
  std::string last_hm_, weather_text_, notice_, problem_text_, doc_notice_;
  const std::string empty_;
};

// ---- pairing: the only page besides the clock while the device is unclaimed. The QR encodes the
// short-lived code and nothing about the device.
class PairingView : public PageView {
 public:
  explicit PairingView(lv_obj_t *parent) : PageView(parent, "__pair", 'p') {
    lv_obj_t *t = mk_label(root_, 0, 8, 480, 32, F(g_fonts.sans600_20), T_CHALK, "HallBoard");
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    // 12 px of chalk quiet zone around a 220 px code, centred in the upper half.
    quiet_ = mk_panel(root_, 118, 46, 244, 244, T_CHALK, 6);
    qr_ = lv_qrcode_create(quiet_);
    lv_qrcode_set_size(qr_, 220);
    lv_qrcode_set_dark_color(qr_, col(T_NIGHT));
    lv_qrcode_set_light_color(qr_, col(T_CHALK));
    lv_obj_set_pos(qr_, 12, 12);
    code_ = mk_label(root_, 0, 300, 480, 0, F(g_fonts.mono64), T_CHALK, "");
    lv_obj_set_style_text_align(code_, LV_TEXT_ALIGN_CENTER, 0);
    tracked(code_, 8);
    hint_lbl_ = mk_label(root_, 24, 396, 432, 26, F(g_fonts.sans500_18), T_CHALK70,
                         "Scan or enter this code at www.hallboard.co.uk");
    lv_obj_set_style_text_align(hint_lbl_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint_lbl_, LV_LABEL_LONG_MODE_DOTS);
    wifi_ = mk_label(root_, 24, 430, 432, 24, F(g_fonts.mono15), T_CHALK50, "");
    tracked(wifi_, 1);
    lv_obj_set_style_text_align(wifi_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(wifi_, LV_LABEL_LONG_MODE_DOTS);
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
      set_hidden(quiet_, true);
      lv_label_set_text(code_, "");
      lv_label_set_text(hint_lbl_, "Waiting for a pairing code...");
      return;
    }
    // The code rides in the fragment: the portal reads it in the browser and it never
    // reaches a server log line.
    std::string url = "https://www.hallboard.co.uk/pair#code=" + code;
    lv_qrcode_update(qr_, url.c_str(), (uint32_t) url.size());
    set_hidden(quiet_, false);
    lv_label_set_text(code_, (code.substr(0, 3) + " " + code.substr(3)).c_str());
    lv_label_set_text(hint_lbl_, "Scan or enter this code at www.hallboard.co.uk");
  }

  void set_status(const std::string &msg, bool problem) override { lv_label_set_text(wifi_, msg.c_str()); }

 private:
  lv_obj_t *quiet_ = nullptr, *qr_ = nullptr, *code_ = nullptr, *hint_lbl_ = nullptr, *wifi_ = nullptr;
  std::string code_shown_ = "\x01";  // never a valid code, so the first set_code always draws
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

    logo_ = make_logo_(root_);
    notice_ = mk_label(root_, 24, 198, 432, 26, F(g_fonts.sans500_18), T_CHALK, "");
    lv_obj_set_style_text_align(notice_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(notice_, LV_LABEL_LONG_MODE_DOTS);
    for (int i = 0; i < STEPS; i++) {
      steps_[i] = mk_label(root_, 24, 232 + 34 * i, 432, 30, F(g_fonts.mono16), T_PENDING, "");
      lv_obj_set_style_text_align(steps_[i], LV_TEXT_ALIGN_CENTER, 0);
      tracked(steps_[i], 1);
      lv_label_set_long_mode(steps_[i], LV_LABEL_LONG_MODE_DOTS);
    }
    help_ = mk_label(root_, 24, 372, 432, 54, F(g_fonts.sans400_18), T_CHALK70, "");
    lv_obj_set_style_text_align(help_, LV_TEXT_ALIGN_CENTER, 0);
    detail_ = mk_label(root_, 24, 428, 432, 20, F(g_fonts.mono15), T_CHALK50, "");
    lv_obj_set_style_text_align(detail_, LV_TEXT_ALIGN_CENTER, 0);
    tracked(detail_, 1);
    lv_label_set_long_mode(detail_, LV_LABEL_LONG_MODE_DOTS);
    support_ = mk_label(root_, 24, 452, 432, 20, F(g_fonts.mono15), T_CHALK50, "");
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

  // The mark. Two letters is all the 50 px face carries; S10 puts them on the violet tile.
  static lv_obj_t *make_logo_(lv_obj_t *parent) {
    lv_obj_t *l = mk_label(parent, 0, 136, 480, 0, F(g_fonts.mark50), T_CHALK, "HB");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(l, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_opa_cb_);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_start(&a);
    lv_anim_set_exec_cb(&a, anim_y_cb_);
    lv_anim_set_values(&a, 136, 120);
    lv_anim_start(&a);
    return l;
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
      set_help_("Cannot reach hallboard.co.uk, retrying...");
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
        if (!started_) return begin_step_(0, "Connecting to Wi-Fi...");
        if (have_net_) {
          finish_step_(0, "Wi-Fi connected");
          return advance_(S_CONTENT);
        }
        // Twenty seconds without a network and the household needs telling how to fix it. A
        // status string that already said so wins: it is more specific than this one.
        if (help_text_.empty() && (int32_t) (now - t0_) > 20000)
          set_help_("Still looking for Wi-Fi... hold the side button to choose a network");
        return;
      case S_CONTENT:
        if (!started_) return begin_step_(1, "Downloading content...");
        if (unpaired_) {
          finish_step_(1, "Content loaded");
          return advance_(S_PAIR);
        }
        if (have_doc_) {
          finish_step_(1, "Content loaded");
          content_at_ = now;
          return advance_(S_TIME);
        }
        return;
      case S_TIME:
        if (!started_) return begin_step_(2, "Syncing time...");
        if (have_time_) {
          finish_step_(2, "Time synced");
          return advance_(S_READY);
        }
        // SNTP is not worth waiting on: the clock page says "Waiting for time..." instead.
        if ((int32_t) (now - content_at_) > 30000) {
          set_help_("Time not synced yet");
          return advance_(S_READY);
        }
        return;
      case S_READY:
        if (!started_) return begin_step_(3, "Ready to use", true, true);
        return start_fade_();
      case S_PAIR:
        if (!started_) return begin_step_(3, "Pair this board", true);
        return start_fade_();
      default:
        return;
    }
  }

 private:
  enum Step { S_WIFI = 0, S_CONTENT, S_TIME, S_READY, S_PAIR, S_FADING };
  static const int STEPS = 4;
  static const uint32_t HOLD_MS = 600;

  // Each of these writes exactly one line and holds the next transition for 600 ms. A finished
  // step is marked by its colour alone: the tick glyph belonged to Montserrat, which the mono
  // face the steps are set in does not carry.
  void begin_step_(int i, const char *text, bool done = false, bool ticked = false) {
    set_tok(steps_[i], ticked ? T_OK : (done ? T_CHALK : T_TITLE2));
    lv_label_set_text(steps_[i], text);
    started_ = true;
    hold_until_ = last_now_ + HOLD_MS;
  }
  void finish_step_(int i, const char *text) {
    set_tok(steps_[i], T_OK);
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

// ---- board: the departures or arrivals template. The Phase 1a geometry, with the rows dropped
// eight pixels so the 36 px margin the design gives a board leaves the strip its band. S5 lays
// the face out properly.
class BoardView : public PageView {
 public:
  static const int ROWS = 5;

  BoardView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'b') {
    build_header_("Train board");
    for (int i = 0; i < ROWS; i++) {
      Row &r = rows_[i];
      r.card = mk_card(root_, 14, 68 + 74 * i, 452, 68);
      set_hidden(r.card, true);
      // 84 px of mono 20 holds "08:44" and its tracking without wrapping to a second line.
      r.time = mk_label(r.card, 14, 5, 84, 34, F(g_fonts.mono20), T_CHALK);
      tracked(r.time, 1);
      r.dest = mk_label(r.card, 104, 5, 254, 34, F(g_fonts.sans500_20), T_CHALK);
      lv_label_set_long_mode(r.dest, LV_LABEL_LONG_MODE_DOTS);
      r.plat = mk_label(r.card, 360, 5, 78, 34, F(g_fonts.sans500_20), T_CHALK70);
      lv_obj_set_style_text_align(r.plat, LV_TEXT_ALIGN_RIGHT, 0);
      r.exp = mk_label(r.card, 14, 41, 84, 22, F(g_fonts.mono15), T_CHALK70);
      tracked(r.exp, 1);
      r.status = mk_label(r.card, 104, 41, 334, 22, F(g_fonts.mono15), T_CHALK70);
      tracked(r.status, 1);
      lv_label_set_long_mode(r.status, LV_LABEL_LONG_MODE_DOTS);
    }
    // A tap anywhere refreshes; the row hit rects sit on top and add the long press. Neither is
    // scrollable, so a horizontal drag still reaches the carousel.
    lv_obj_t *tap = mk_obj(root_, 0, 0, 480, 480);
    lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap, touch_cb_, LV_EVENT_SHORT_CLICKED, nullptr);
    for (int i = 0; i < ROWS; i++) {
      lv_obj_t *hit = mk_obj(root_, 0, 68 + 74 * i, 480, 74);
      lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(hit, (void *) (intptr_t) i);
      lv_obj_add_event_cb(hit, touch_cb_, LV_EVENT_SHORT_CLICKED, nullptr);
      lv_obj_add_event_cb(hit, long_cb_, LV_EVENT_LONG_PRESSED, nullptr);
    }
  }

  void apply(const Page &pg) override {
    arrivals_ = pg.mode == "arr";
    // The module decides the empty-state wording and whether the side button has anything to do
    // here: only a rail board is a pair of CRS stations the picker can change. A document from a
    // backend that does not send `module` is rail, which is all there was before 1.4.0.
    module_ = pg.module;
    rail_ = module_.empty() || module_ == "rail";
    if (!pg.title.empty()) set_title_(pg.title);
    uids_.clear();
    for (int i = 0; i < ROWS; i++) {
      Row &r = rows_[i];
      if (i < (int) pg.rows.size()) {
        const BoardRow &d = pg.rows[i];
        label_text(r.time, d.time);
        label_text(r.dest, d.dest);
        label_text(r.plat, d.plat);
        // The status string arrives fully composed (delay, coaches, operator, and for an arrivals
        // board the origin's booked and actual departure).
        label_text(r.status, d.status);
        set_tok(r.status, row_colour(d.colour));
        label_text(r.exp, d.expected);
        set_tok(r.exp, row_colour(d.colour));
        set_hidden(r.card, false);
        uids_.push_back(d.uid);
      } else {
        clear_row_(r);
      }
    }
    if (pg.rows.empty()) {
      // asof is 0 only when the backend could not build this board at all. Say so rather than
      // claiming there is nothing due.
      bool missing = pg.asof == 0;
      // A bus stop shows buses; rail and tube boards both show trains. Only a rail board can
      // promise a window, because only its backend adapter asks for one.
      const char *none = module_ == "bus" ? "No buses" : "No trains";
      const char *waiting = rail_ ? (arrivals_ ? "None arriving in the next 2 hours"
                                               : "None due in the next 2 hours")
                                  : "Nothing expected at this stop";
      lv_label_set_text(rows_[0].dest, missing ? "Board unavailable" : none);
      lv_label_set_text(rows_[0].status, missing ? "Not in the last update from the backend" : waiting);
      set_tok(rows_[0].status, T_CHALK70);
      set_hidden(rows_[0].card, false);
    }
    set_stamp_(pg.asof, pg.stale);
  }

  void tick(esphome::ESPTime now) override { tick_header_(now); }

  // Used after the station picker so the header is right before the new document arrives.
  void set_pending(const std::string &title) {
    set_title_(title);
    uids_.clear();
    for (auto &r : rows_) clear_row_(r);
    set_stamp_(0, false);
  }

  const std::string &uid_at(int i) const {
    static const std::string none;
    if (i < 0 || i >= (int) uids_.size()) return none;
    return uids_[i];
  }

  // True for a national rail board, the only kind the on-device station picker can edit.
  bool rail() const { return rail_; }

 private:
  struct Row {
    lv_obj_t *card = nullptr, *time = nullptr, *dest = nullptr, *plat = nullptr, *exp = nullptr, *status = nullptr;
  };
  static void clear_row_(Row &r) {
    lv_label_set_text(r.time, "");
    lv_label_set_text(r.dest, "");
    lv_label_set_text(r.plat, "");
    lv_label_set_text(r.status, "");
    lv_label_set_text(r.exp, "");
    set_hidden(r.card, true);
  }
  static void touch_cb_(lv_event_t *e) { emit("TOUCH"); }
  static void long_cb_(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target_obj(e);
    emit("LONG|" + std::to_string((int) (intptr_t) lv_obj_get_user_data(o)));
  }

  Row rows_[ROWS];
  std::vector<std::string> uids_;
  std::string module_;
  bool arrivals_ = false;
  bool rail_ = true;
};

// ---- agenda: the 1a render_calendar algorithm, with cards created on demand instead of a pool of
// 64 hidden objects.
class AgendaView : public PageView {
 public:
  static const int MAX_CARDS = 60, MAX_HEADS = 8;

  AgendaView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'a') {
    build_header_("Calendar");
    // Vertical only, so a horizontal drag chains up to the carousel instead of being eaten here.
    list_ = mk_obj(root_, 0, 56, 480, 378);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(list_, touch_cb_, LV_EVENT_SHORT_CLICKED, nullptr);
  }

  void apply(const Page &pg) override {
    events_ = pg.events;
    asof_ = pg.asof;
    stale_ = pg.stale;
    if (!pg.title.empty()) set_title_(pg.title);
    render();
  }

  void tick(esphome::ESPTime now) override { tick_header_(now); }

  void scroll_top() { lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF); }

  void render() {
    for (auto &c : cards_) set_hidden(c.box, true);
    for (auto &h : heads_) {
      set_hidden(h.text, true);
      set_hidden(h.rule, true);
    }
    const bool expanded = g_agenda_expanded;
    const int CARD_H = expanded ? 64 : 40, GAP = 6, HEAD_GAP = 14;
    int card = 0, head = 0, y = 16, shown = 0;  // start clear of the rule under the title
    std::string last_day;
    for (const auto &it : events_) {
      if (card >= MAX_CARDS) break;
      // hide today's timed events that have already ended
      if (!it.all_day && it.d == g_today && !g_nowhm.empty() && it.u.size() == 5 && it.u <= g_nowhm) continue;
      if (it.w != last_day) {
        if (head >= MAX_HEADS) break;
        if (!last_day.empty()) y += HEAD_GAP - GAP;
        Head &h = ensure_head_(head);
        bool today = it.d == g_today;
        set_tok(h.text, today ? T_CHALK : T_CHALK50);
        place_label_(h.text, 14, y, 452, upper(it.w) + (today ? "   TODAY" : ""));
        lv_obj_set_pos(h.rule, 14, y + 26);
        set_hidden(h.rule, false);
        head++;
        y += 36;
        last_day = it.w;
      }
      bool has_end = !it.all_day && !it.u.empty() && it.u != it.t;
      Card &c = ensure_card_(card);
      lv_obj_set_pos(c.box, 14, y);
      lv_obj_set_height(c.box, CARD_H);
      set_hidden(c.box, false);
      if (expanded) {
        // line 1: "start - end" in amber, location right-aligned in grey; line 2: title
        std::string range = it.all_day ? "All day" : (has_end ? it.t + " - " + it.u : it.t);
        place_label_(c.a, 14, 6, 200, range);
        place_label_(c.b, 224, 8, 214, it.l);
        place_label_(c.c, 14, 34, 424, it.s);
      } else {
        // one line: start, title, end time right-aligned in grey
        place_label_(c.a, 14, 8, 82, it.all_day ? "All day" : it.t);
        std::string endtxt = has_end ? it.u : "";
        int title_w = endtxt.empty() ? 336 : 336 - 90;
        place_label_(c.c, 102, 8, title_w, it.s);
        place_label_(c.b, 348, 10, 90, endtxt);
      }
      y += CARD_H + GAP;
      card++;
      shown++;
    }
    if (shown == 0) {
      Head &h = ensure_head_(0);
      set_tok(h.text, T_CHALK);
      place_label_(h.text, 14, 16, 452, asof_ ? "Nothing in the next 7 days" : "Loading...");
    }
    set_stamp_(asof_, stale_);
  }

 private:
  struct Card {
    lv_obj_t *box = nullptr, *a = nullptr, *b = nullptr, *c = nullptr;
  };
  struct Head {
    lv_obj_t *text = nullptr, *rule = nullptr;
  };

  Card &ensure_card_(int i) {
    while ((int) cards_.size() <= i) {
      Card c;
      c.box = mk_card(list_, 14, 0, 452, 40);
      set_hidden(c.box, true);
      c.a = mk_label(c.box, 14, 8, 200, 26, F(g_fonts.mono16), T_TIME2);
      tracked(c.a, 1);
      lv_label_set_long_mode(c.a, LV_LABEL_LONG_MODE_DOTS);
      c.b = mk_label(c.box, 224, 8, 214, 26, F(g_fonts.sans500_16), T_CHALK50);
      lv_label_set_long_mode(c.b, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_style_text_align(c.b, LV_TEXT_ALIGN_RIGHT, 0);
      c.c = mk_label(c.box, 14, 34, 424, 26, F(g_fonts.sans500_18), T_TITLE2);
      lv_label_set_long_mode(c.c, LV_LABEL_LONG_MODE_DOTS);
      cards_.push_back(c);
    }
    return cards_[i];
  }
  Head &ensure_head_(int i) {
    while ((int) heads_.size() <= i) {
      Head h;
      h.text = mk_label(list_, 14, 0, 452, 24, F(g_fonts.mono15), T_CHALK50);
      tracked(h.text, 1);
      lv_label_set_long_mode(h.text, LV_LABEL_LONG_MODE_DOTS);
      set_hidden(h.text, true);
      h.rule = mk_rule(list_, 14, 0, 452, T_DIVIDER);
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
  std::vector<Card> cards_;
  std::vector<Head> heads_;
  std::vector<AgendaEvent> events_;
  uint32_t asof_ = 0;
  bool stale_ = false;
};

// ---- generic: the module template. Up to eight rows of icon, value and two text lines. The rows
// live in a vertical scroller so eight of them are reachable on a 480 px screen.
class GenericView : public PageView {
 public:
  static const int MAX_ROWS = 8;

  GenericView(lv_obj_t *parent, const std::string &id) : PageView(parent, id, 'g') {
    build_header_("");
    list_ = mk_obj(root_, 0, 56, 480, 378);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(list_, touch_cb_, LV_EVENT_SHORT_CLICKED, nullptr);
  }

  void apply(const Page &pg) override {
    set_title_(pg.title);
    for (int i = 0; i < (int) rows_.size(); i++) set_hidden(rows_[i].box, true);
    int n = (int) pg.grows.size();
    if (n > MAX_ROWS) n = MAX_ROWS;
    for (int i = 0; i < n; i++) {
      const GenericRow &d = pg.grows[i];
      Row &r = ensure_row_(i);
      lv_label_set_text(r.icon, icon_glyph(d.icon));
      label_text(r.value, d.value);
      label_text(r.a, d.a);
      label_text(r.b, d.b);
      set_hidden(r.box, false);
    }
    set_stamp_(pg.asof, pg.stale);
  }

  void tick(esphome::ESPTime now) override { tick_header_(now); }

 private:
  struct Row {
    lv_obj_t *box = nullptr, *icon = nullptr, *value = nullptr, *a = nullptr, *b = nullptr;
  };
  Row &ensure_row_(int i) {
    while ((int) rows_.size() <= i) {
      int y = 4 + 58 * (int) rows_.size();  // 52 tall with a 6 px gap, first row at page y 60
      Row r;
      r.box = mk_panel(list_, 14, y, 452, 52, T_CARD, 16);
      set_hidden(r.box, true);
      r.icon = mk_label(r.box, 0, 0, 60, 0, F(g_fonts.icon), T_CHALK70);
      lv_obj_set_style_text_align(r.icon, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(r.icon, LV_ALIGN_LEFT_MID, 10, 0);
      r.value = mk_label(r.box, 0, 0, 90, 0, F(g_fonts.sans600_20), T_CHALK);
      lv_obj_align(r.value, LV_ALIGN_LEFT_MID, 78, 0);
      r.a = mk_label(r.box, 176, 4, 262, 24, F(g_fonts.sans500_18), T_CHALK);
      lv_label_set_long_mode(r.a, LV_LABEL_LONG_MODE_DOTS);
      r.b = mk_label(r.box, 176, 28, 262, 20, F(g_fonts.mono15), T_CHALK70);
      tracked(r.b, 1);
      lv_label_set_long_mode(r.b, LV_LABEL_LONG_MODE_DOTS);
      rows_.push_back(r);
    }
    return rows_[i];
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
        if (views_[i] && views_[i]->id() == pg.id && views_[i]->type() == pg.type) {
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
    clock_weather_(doc);
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
    static_cast<ClockView *>(clock.get())->set_weather("");
    // The notice belonged to a household that no longer claims this device.
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
  uint32_t object_count() const { return host_ == nullptr ? 0 : lv_obj_get_child_count(host_); }

  PageView *view(size_t i) const { return i < views_.size() ? views_[i].get() : nullptr; }
  PageView *current_view() const { return view(cur_); }
  char current_type() const { return cur_ < views_.size() ? views_[cur_]->type() : 0; }
  BoardView *current_board() const {
    PageView *v = current_view();
    return (v != nullptr && v->type() == 'b') ? static_cast<BoardView *>(v) : nullptr;
  }
  AgendaView *current_agenda() const {
    PageView *v = current_view();
    return (v != nullptr && v->type() == 'a') ? static_cast<AgendaView *>(v) : nullptr;
  }

  // 0 or 1 for the first and second board pages in display order, -1 for anything else. This is
  // the `board` field of PATCH /v1/device/settings and the index into the station globals.
  int board_ordinal(size_t index) const {
    int n = 0;
    for (size_t i = 0; i < views_.size(); i++) {
      if (views_[i]->type() != 'b') continue;
      if (i == index) return n < 2 ? n : -1;
      n++;
    }
    return -1;
  }
  // The board ordinal the side button's station picker should edit, starting from the page at
  // `index`: that page when it is a rail board, otherwise the first rail board in the document so
  // the button still works from the clock. -1 when there is nothing to edit, which is the answer
  // on a tube or bus board (no CRS station to change) and in a document with no rail board.
  int picker_ordinal(size_t index) const {
    if (index < views_.size() && views_[index]->type() == 'b') {
      int ord = board_ordinal(index);
      return (ord >= 0 && static_cast<const BoardView *>(views_[index].get())->rail()) ? ord : -1;
    }
    int n = 0;
    for (const auto &v : views_) {
      if (v->type() != 'b') continue;
      if (n >= 2) break;   // only the first two boards have station globals behind them
      if (static_cast<const BoardView *>(v.get())->rail()) return n;
      n++;
    }
    return -1;
  }

  // The carousel index of a board ordinal, or -1 when the document has no such board.
  int board_index(int ordinal) const {
    int n = 0;
    for (size_t i = 0; i < views_.size(); i++) {
      if (views_[i]->type() != 'b') continue;
      if (n == ordinal) return (int) i;
      n++;
    }
    return -1;
  }

 private:
  static constexpr size_t MAX_CONTENT = 8;

  std::unique_ptr<PageView> make_view_(const Page &pg) {
    if (pg.type == 'b') return std::unique_ptr<PageView>(new BoardView(host_, pg.id));
    if (pg.type == 'a') return std::unique_ptr<PageView>(new AgendaView(host_, pg.id));
    return std::unique_ptr<PageView>(new GenericView(host_, pg.id));
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

  // The clock page carries the first row of the weather page when the document has one.
  void clock_weather_(const Document &doc) {
    const Page *weather = nullptr;
    for (const Page &pg : doc.pages) {
      if (pg.type != 'g' || pg.grows.empty()) continue;
      if (pg.module == "weather") {
        weather = &pg;
        break;
      }
      if (weather == nullptr) weather = &pg;
    }
    std::string line;
    if (weather != nullptr) {
      const GenericRow &r = weather->grows[0];
      line = r.value;
      if (!r.a.empty()) line += (line.empty() ? "" : "  ") + r.a;
      if (!r.b.empty()) line += (line.empty() ? "" : "  ") + r.b;
    }
    static_cast<ClockView *>(views_[0].get())->set_weather(line);
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
  size_t cur_ = 0;
  bool unpaired_ = false;
};

inline PageHost g_host;

}  // namespace hb
