// The HallBoard design system's device palette, as LVGL styles.
//
// Every colour the runtime UI draws is one of the tokens below. Nothing in hb_ui.h names a hex
// value: a widget adds the shared text or background style for its token, so switching the whole
// board to the night palette is one apply_palette() call and not a walk over the object tree.
//
// The hex values are the sRGB of the design's OKLCH colours (see the plan's "Design values to
// port"). Module hues are for text, dots and bars only; they are never a fill.
#pragma once

#include <cstdint>

#include <lvgl.h>

namespace hb {

enum Tok : uint8_t {
  T_NIGHT,      // the page behind everything
  T_CARD,       // a resting card on the page
  T_RAISED,     // the one card a face wants read first
  T_LINE,       // hairline between rows
  T_BEZEL,      // the boot screen, darker than the page so the panel edge disappears
  T_CHALK,      // primary text
  T_CHALK70,    // the lowest tone a sentence may use
  T_CHALK50,    // meta only: stamps, units, day headings
  T_TRANSIT,    // module hue: boards
  T_CALENDAR,   // module hue: the diary
  T_WEATHER,    // module hue: the sky
  T_REMINDERS,  // module hue: the list
  T_OK,         // on time
  T_LATE,       // delayed, or a reminder past its date
  T_OFF,        // cancelled, or off
  T_VIOLET,     // the one primary action on a screen
  T_LIFT,       // the lilac that marks the live thing: face dot, breathing dot
  T_HEADLINE,   // a heading grey, a shade under chalk
  T_TITLE2,     // a resting row's title
  T_TIME2,      // a resting row's time
  T_HOUR,       // the hour labels under the weather strip
  T_DOT,        // an inactive dot
  T_FIELD,      // a text field's fill, and the inactive face dots
  T_OUTLINE,    // a secondary button's border
  T_DIVIDER,    // under a day heading, dimmer than a row hairline
  T_DONEBG,     // a ticked reminder's fill
  T_PENDING,    // a boot step not reached yet
  T_WHITE,      // the QR's light modules and the mark's letters, not a text tone
  T_COUNT,
};

struct Palette {
  uint32_t rgb[T_COUNT];
};

// The day palette, in Tok order.
inline constexpr Palette DAY = {{
    0x0F0E15,  // T_NIGHT
    0x1B1922,  // T_CARD
    0x292731,  // T_RAISED
    0x2E2D35,  // T_LINE
    0x07070A,  // T_BEZEL
    0xF5F4F9,  // T_CHALK
    0xABAAB2,  // T_CHALK70
    0x807F87,  // T_CHALK50
    0xA698E4,  // T_TRANSIT
    0xE3928B,  // T_CALENDAR
    0x71BCDF,  // T_WEATHER
    0x82CB9B,  // T_REMINDERS
    0x82CB9B,  // T_OK
    0xEEB154,  // T_LATE
    0xEC5258,  // T_OFF
    0x7A5AF8,  // T_VIOLET
    0xA99CFA,  // T_LIFT
    0xDEDDE3,  // T_HEADLINE
    0xE8E7EC,  // T_TITLE2
    0xD1D0D7,  // T_TIME2
    0x929199,  // T_HOUR
    0x484652,  // T_DOT
    0x383641,  // T_FIELD
    0x42414C,  // T_OUTLINE
    0x24232B,  // T_DIVIDER
    0x16151B,  // T_DONEBG
    0x424148,  // T_PENDING
    0xFFFFFF,  // T_WHITE
}};

// Inside the night window the palette desaturates to half chroma. S12 generates the table; until
// then night is day, so set_night() is a no-op that already goes through the right path.
inline constexpr Palette NIGHT = DAY;

// One text style and one background style per token, shared by every widget that uses it. A
// widget adds the style rather than setting a local colour, which is what makes apply_palette()
// enough to repaint the board.
inline lv_style_t g_text[T_COUNT];
inline lv_style_t g_bg[T_COUNT];
inline const Palette *g_palette = &DAY;
inline bool g_styles_ready = false;

// Rewrite every shared style from `p` and tell LVGL the objects using them need drawing again.
inline void apply_palette(const Palette &p) {
  g_palette = &p;
  if (!g_styles_ready) return;
  for (int i = 0; i < T_COUNT; i++) {
    lv_style_set_text_color(&g_text[i], lv_color_hex(p.rgb[i]));
    lv_style_set_bg_color(&g_bg[i], lv_color_hex(p.rgb[i]));
    lv_style_set_bg_opa(&g_bg[i], LV_OPA_COVER);
  }
  lv_obj_report_style_change(nullptr);
}

// Called before the first widget is built. Safe to call again; it only does the work once.
inline void init_styles() {
  if (g_styles_ready) return;
  for (int i = 0; i < T_COUNT; i++) {
    lv_style_init(&g_text[i]);
    lv_style_init(&g_bg[i]);
  }
  g_styles_ready = true;
  apply_palette(*g_palette);
}

// The raw colour, for the few places a style cannot reach: a border, the QR's two colours.
inline lv_color_t col(Tok t) { return lv_color_hex(g_palette->rgb[t]); }

}  // namespace hb
