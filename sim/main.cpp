// HallBoard face renderer. Builds the real firmware UI (../hb_ui.h) against a host LVGL, drives
// it through a table of scenarios, and writes one 480x480 PNG per scenario to out/.
//
// Nothing here ships to the device: the stubs under stubs/ stand in for the four ESPHome headers
// hb_ui.h and hb_parse.h include, and the fixtures under fixtures/ are synthetic screen documents.
// The point is to see what a change to hb_ui.h does to every face without flashing a board.
//
// Time is fixed at Friday 18 September 2026, 08:41 local, TZ Europe/London, so the output is the
// same on every machine and a PNG only changes when the UI does.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <lvgl.h>

#if HB_SIM_SDL
#include <unistd.h>

#include "src/drivers/sdl/lv_sdl_window.h"
#else
// lodepng's C++ convenience overloads are declared inside LVGL's extern "C" block, which does not
// compile. Nothing here wants them; lodepng_encode24_file is a C function either way.
#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"
#endif

#include "hb_parse.h"
#include "hb_ui.h"

namespace {

const int W = 480, H = 480;
const char *FW = "1.4.0";

// 2026-09-18 08:41:07 Europe/London. Chosen so the agenda fixture has a spent event, a live one
// and two later days, and so the stamp reads "LIVE · 08:38", inside the five-minute window.
const time_t NOW_EPOCH = 1789717267;

// ---------------------------------------------------------------- fonts
// Every slot hb_ui.h asks hb::g_fonts for. ESPHome rasterises these at build time from the
// `font:` entries in ui.yaml, which get a static instance per weight out of the Google CSS API;
// here TinyTTF does it at run time from the matching static TTFs, so a 600 is really a 600.
struct FontSpec {
  const lv_font_t *hb::FontSet::*slot;
  const char *path;
  int px;
};
const char *FIG_600 = "fonts/Figtree-SemiBold.ttf";
const char *FIG_500 = "fonts/Figtree-Medium.ttf";
const char *FIG_400 = "fonts/Figtree-Regular.ttf";
const char *FIG_700 = "fonts/Figtree-Bold.ttf";
const char *MONO_400 = "fonts/IBMPlexMono-Regular.ttf";
const FontSpec FONTS[] = {
    {&hb::FontSet::clock132, FIG_600, 132},
    {&hb::FontSet::hero88, FIG_600, 88},
    {&hb::FontSet::sans600_30, FIG_600, 30},
    {&hb::FontSet::sans600_20, FIG_600, 20},
    {&hb::FontSet::sans500_22, FIG_500, 22},
    {&hb::FontSet::sans500_20, FIG_500, 20},
    {&hb::FontSet::sans500_18, FIG_500, 18},
    {&hb::FontSet::sans500_16, FIG_500, 16},
    {&hb::FontSet::sans400_18, FIG_400, 18},
    {&hb::FontSet::mark50, FIG_700, 50},
    {&hb::FontSet::mono64, MONO_400, 64},
    {&hb::FontSet::mono20, MONO_400, 20},
    {&hb::FontSet::mono16, MONO_400, 16},
    {&hb::FontSet::mono15, MONO_400, 15},
    {&hb::FontSet::mono14, MONO_400, 14},
    {&hb::FontSet::icon, "../fonts/materialdesignicons-webfont.ttf", 48},
};

std::string read_file(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rb");
  if (f == nullptr) {
    fprintf(stderr, "cannot open %s\n", path.c_str());
    exit(2);
  }
  std::string out;
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  fclose(f);
  return out;
}

// The TTF bytes have to outlive the font, so they are parked here for the life of the process.
std::vector<std::string> g_ttf_bytes;

void load_fonts() {
  for (const FontSpec &spec : FONTS) {
    g_ttf_bytes.push_back(read_file(spec.path));
    const std::string &bytes = g_ttf_bytes.back();
    lv_font_t *font = lv_tiny_ttf_create_data(bytes.data(), bytes.size(), spec.px);
    if (font == nullptr) {
      fprintf(stderr, "could not rasterise %s at %d px\n", spec.path, spec.px);
      exit(2);
    }
    hb::g_fonts.*spec.slot = font;
  }
}

// ---------------------------------------------------------------- display
#if !HB_SIM_SDL
// One full-screen RGB565 buffer, the same pixel format the RGB panel scans, so the PNGs carry the
// board's banding rather than a truer 24-bit render. The flush is a no-op: the buffer is read
// straight out of here.
// Aligned to LV_DRAW_BUF_ALIGN: lv_display_set_buffers asserts on a buffer that is not.
alignas(32) uint16_t g_fb[W * H];

void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  LV_UNUSED(area);
  LV_UNUSED(px);
  lv_display_flush_ready(disp);
}

void write_png(const std::string &path) {
  std::vector<unsigned char> rgb((size_t) W * H * 3);
  for (int i = 0; i < W * H; i++) {
    uint16_t v = g_fb[i];
    unsigned r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    // Bit replication, which is what the panel's 5/6/5 lines do to an 8-bit value.
    rgb[i * 3 + 0] = (unsigned char) ((r << 3) | (r >> 2));
    rgb[i * 3 + 1] = (unsigned char) ((g << 2) | (g >> 4));
    rgb[i * 3 + 2] = (unsigned char) ((b << 3) | (b >> 2));
  }
  // lodepng's own file writer goes through lv_fs, which would mean turning on a file system
  // driver the device does not have. Encoding to memory and writing it here avoids that.
  unsigned char *png = nullptr;
  size_t len = 0;
  unsigned err = lodepng_encode24(&png, &len, rgb.data(), W, H);
  if (err != 0) {
    fprintf(stderr, "could not encode %s (lodepng %u)\n", path.c_str(), err);
    exit(2);
  }
  FILE *f = fopen(path.c_str(), "wb");
  if (f == nullptr || fwrite(png, 1, len, f) != len) {
    fprintf(stderr, "could not write %s\n", path.c_str());
    exit(2);
  }
  fclose(f);
  lv_free(png);
}
#endif  // !HB_SIM_SDL

// ---------------------------------------------------------------- the driver
// Everything a scenario can do to the UI, and the synthetic clocks behind it: `ms` is what the
// 100 ms interval in hallboard.yaml passes to tick_fast, and `time_valid` is whether SNTP has
// answered yet, which is what the once-a-second tick passes as an ESPTime.
struct Sim {
  uint32_t ms = 0;
  bool time_valid = false;

  static esphome::ESPTime valid_now() { return esphome::ESPTime::from_epoch_local(NOW_EPOCH); }
  esphome::ESPTime now() const { return time_valid ? valid_now() : esphome::ESPTime(); }

  // 10 ms of wall clock at a time, which is finer than either interval the device runs.
  void pump(uint32_t duration_ms) {
    const uint32_t STEP = 10;
    for (uint32_t done = 0; done < duration_ms; done += STEP) {
      ms += STEP;
      lv_tick_inc(STEP);
      hb::g_host.tick_fast(ms);
      if (ms % 1000 == 0) hb::g_host.tick(now());
      lv_timer_handler();
    }
  }

  void attach() { hb::g_host.attach(lv_screen_active(), FW); }
  void network(const char *ssid) { hb::g_host.set_network(ssid); }
  void document(const char *fixture) {
    hb::Document doc;
    std::string body = read_file(std::string("fixtures/") + fixture);
    if (!tb::parse_screen(body, doc)) {
      fprintf(stderr, "fixture %s is not a valid screen document\n", fixture);
      exit(2);
    }
    hb::g_host.set_document(doc);
  }
  // Runs the boot overlay to its end and waits for the fade, so the face underneath is clear.
  // The cap is past the 30 s the overlay gives SNTP before it gives up on it.
  void finish_boot() {
    for (int i = 0; i < 400 && hb::g_host.booting(); i++) pump(100);
    pump(500);
  }
  void show(size_t page) {
    hb::g_host.show(page, false);
    pump(100);
  }
};

// Back to a bare screen, so one scenario cannot leak a page into the next.
void reset_ui() {
  hb::g_host.reset();
  hb::g_today.clear();
  hb::g_nowhm.clear();
  hb::g_now_epoch = 0;
  hb::g_agenda_expanded = false;
  lv_obj_clean(lv_screen_active());
  lv_refr_now(nullptr);
}

// ---- the faces
// A paired board that has finished booting, sitting on the page a scenario names.
void booted(Sim &s, const char *fixture, size_t page) {
  s.attach();
  s.network("Home Wi-Fi");
  s.time_valid = true;
  // A full second before the document, because the once-a-second tick is what fills g_today and
  // g_nowhm, and the agenda reads both the moment a document lands. On the board the tick has
  // been running since boot, so this is the ordering the device has and not a nicety.
  s.pump(1000);
  s.document(fixture);
  s.finish_boot();
  s.show(page);
}

// The boot overlay part way through, stopped before the next line is written. The timings track
// BootView's 600 ms holds: every stage below lands in the gap after one write and before the next.
enum BootStage { BOOT_1 = 1, BOOT_2, BOOT_3, BOOT_4 };

void boot_to(Sim &s, BootStage stage) {
  s.attach();
  s.pump(550);   // "Connecting to Wi-Fi...", wordmark animation finished
  if (stage == BOOT_1) return;
  s.network("Home Wi-Fi");
  s.pump(800);   // Wi-Fi ticked off, "Downloading content..."
  if (stage == BOOT_2) return;
  s.document("screen_full.json");
  s.pump(1300);  // content ticked off, "Syncing time..."
  if (stage == BOOT_3) return;
  s.time_valid = true;
  s.pump(1200);  // time ticked off, "Ready to use" in green, still short of the fade
}

struct Scenario {
  const char *name;
  void (*run)(Sim &);
};

const Scenario SCENARIOS[] = {
    {"clock", [](Sim &s) { booted(s, "screen_full.json", 0); }},
    {"clock-notice", [](Sim &s) { booted(s, "screen_notice.json", 0); }},
    {"clock-problem",
     [](Sim &s) {
       booted(s, "screen_full.json", 0);
       hb::g_host.set_status("Cannot reach hallboard.co.uk, retrying...", true);
       s.pump(200);
     }},
    {"clock-waiting-time",
     [](Sim &s) {
       // SNTP never answers. The overlay gives it 30 s, says so, and hands over to a clock page
       // that shows the waiting line instead of a time.
       s.attach();
       s.network("Home Wi-Fi");
       s.pump(1000);
       s.document("screen_full.json");
       s.finish_boot();
       s.show(0);
     }},
    {"board-live", [](Sim &s) { booted(s, "screen_full.json", 1); }},
    {"dots",
     [](Sim &s) {
       // A swipe from the clock to the first board: the page indicator is up, part way through
       // the two seconds it waits before fading.
       booted(s, "screen_full.json", 0);
       hb::g_host.step(1);
       s.pump(600);
     }},
    {"board-stale", [](Sim &s) { booted(s, "board_stale.json", 1); }},
    {"board-empty", [](Sim &s) { booted(s, "board_empty.json", 1); }},
    {"board-unavailable", [](Sim &s) { booted(s, "board_unavailable.json", 1); }},
    {"board-cancelled", [](Sim &s) { booted(s, "board_cancelled.json", 1); }},
    {"board-tube", [](Sim &s) { booted(s, "board_tube.json", 1); }},
    {"board-bus", [](Sim &s) { booted(s, "board_bus.json", 1); }},
    {"day", [](Sim &s) { booted(s, "screen_full.json", 2); }},
    {"day-empty", [](Sim &s) { booted(s, "agenda_empty.json", 1); }},
    {"weather", [](Sim &s) { booted(s, "screen_full.json", 3); }},
    {"reminders", [](Sim &s) { booted(s, "screen_full.json", 4); }},
    {"pair",
     [](Sim &s) {
       s.attach();
       s.network("Home Wi-Fi");
       s.time_valid = true;
       s.pump(1000);
       hb::g_host.set_unpaired("H7K2M9");
       s.finish_boot();
       s.show(1);
     }},
    {"pair-waiting",
     [](Sim &s) {
       // Claimed by nobody and the pairing call has not come back yet: no QR, no code.
       s.attach();
       s.network("Home Wi-Fi");
       s.time_valid = true;
       s.pump(1000);
       hb::g_host.set_unpaired("");
       s.finish_boot();
       s.show(1);
     }},
    {"boot-step1", [](Sim &s) { boot_to(s, BOOT_1); }},
    {"boot-step2", [](Sim &s) { boot_to(s, BOOT_2); }},
    {"boot-step3", [](Sim &s) { boot_to(s, BOOT_3); }},
    {"boot-step4", [](Sim &s) { boot_to(s, BOOT_4); }},
    {"boot-help",
     [](Sim &s) {
       // A Wi-Fi problem while the first step is still running: the status string is the help
       // line, because it already says what the household should do about it.
       s.attach();
       s.pump(300);
       hb::g_host.set_status("No Wi-Fi network saved. Hold the side button to choose one", true);
       s.pump(700);
     }},
};

void log_event(const std::string &msg) { fprintf(stderr, "[event] %s\n", msg.c_str()); }

}  // namespace

int main(int argc, char **argv) {
  const char *only = nullptr;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--list") == 0) {
      for (const Scenario &sc : SCENARIOS) printf("%s\n", sc.name);
      return 0;
    }
    if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
      only = argv[++i];
      continue;
    }
    fprintf(stderr, "usage: hbsim [--list] [--only <scenario>]\n");
    return 1;
  }

  // The zone the fixtures declare. stamp_text() and the clock both go through localtime, so this
  // is what makes "LIVE · 08:38" and "Friday 18 September" the same on any machine.
  setenv("TZ", "Europe/London", 1);
  tzset();

  lv_init();
#if HB_SIM_SDL
  lv_sdl_window_create(W, H);
#else
  lv_display_t *disp = lv_display_create(W, H);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, g_fb, nullptr, sizeof g_fb, LV_DISPLAY_RENDER_MODE_FULL);
#endif
  load_fonts();
  hb::g_event = log_event;

#if HB_SIM_SDL
  // One face in a window, left up until the window is closed. `--only` picks it.
  const Scenario *pick = &SCENARIOS[0];
  if (only != nullptr) {
    pick = nullptr;
    for (const Scenario &sc : SCENARIOS)
      if (strcmp(only, sc.name) == 0) pick = &sc;
    if (pick == nullptr) {
      fprintf(stderr, "no scenario called %s\n", only);
      return 1;
    }
  }
  reset_ui();
  Sim s;
  pick->run(s);
  printf("%s\n", pick->name);
  for (;;) {
    lv_tick_inc(10);
    lv_timer_handler();
    usleep(10000);
  }
#else
  int written = 0;
  for (const Scenario &sc : SCENARIOS) {
    if (only != nullptr && strcmp(only, sc.name) != 0) continue;
    reset_ui();
    Sim s;
    sc.run(s);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);
    std::string path = std::string("out/") + sc.name + ".png";
    write_png(path);
    printf("%s\n", path.c_str());
    written++;
  }
  if (written == 0) {
    fprintf(stderr, "no scenario matched%s%s\n", only ? ": " : "", only ? only : "");
    return 1;
  }
  return 0;
#endif
}
