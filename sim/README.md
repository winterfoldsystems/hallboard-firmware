# Host simulator

Renders the board's faces on a laptop. It compiles the firmware's own UI, `../hb_ui.h` and
`../hb_parse.h`, against LVGL built for the host, feeds it synthetic screen documents, and writes
one 480x480 PNG per face. A change to a colour, a font size or a row's geometry can be looked at
in seconds instead of flashed.

Nothing here ships to the device. The stubs under `stubs/` stand in for the four ESPHome headers
the UI includes, the fixtures are made up, and `firmware/sim/` is never part of an ESPHome build.

## Running it

```
make -C firmware/sim png                      # every face, into firmware/sim/out/
make -C firmware/sim png && open firmware/sim/out/clock.png
./firmware/sim/build/hbsim --only board-live   # one face
./firmware/sim/build/hbsim --list              # the scenario names
make -C firmware/sim run                       # an SDL2 window, if SDL2 is installed
python3 firmware/sim/tools/check_glyphs.py     # every character drawn has a glyph
```

The first `make png` clones LVGL at the tag the device build uses (v9.5.0) into `lvgl/` and
downloads three fonts into `fonts/`, checked against `fonts.sha256`. Both directories are
gitignored, and those two fetches are the only network anything here does; rendering talks to
nothing. Building all of LVGL takes a couple of minutes once, and is incremental after that.

Time is pinned to Friday 18 September 2026, 08:41 local, `TZ=Europe/London`, so a PNG changes when
the UI changes and not otherwise.

## Scenarios

| Name | What it shows |
|---|---|
| `clock` | The clock page of a paired board, with the weather line under it |
| `clock-notice` | The same with a `settings.notice` from the document |
| `clock-problem` | The same with a live problem status, which wins over a notice |
| `clock-waiting-time` | SNTP never answered: no time, no date, "Waiting for time..." |
| `board-live` | A rail departures board, five rows, one delayed |
| `board-stale` | An arrivals board being served from the backend's last good data |
| `board-empty` | A board with no departures in the window |
| `board-unavailable` | A board the backend could not build at all (`asof` 0) |
| `board-cancelled` | A rail board with a cancelled row, a delayed one and one with no platform |
| `board-tube` | A tube board: waits instead of expected times, no platform |
| `board-bus` | A bus board: route numbers in the platform column |
| `day` | The agenda, today marked, spent events dropped, two later days |
| `day-empty` | The agenda with nothing in the next seven days |
| `weather` | The generic template as the weather module fills it, seven rows |
| `reminders` | The generic template as the to-do module fills it |
| `pair` | The pairing page with a code and its QR |
| `pair-waiting` | Unclaimed, but the pairing call has not come back yet |
| `boot-step1` | The boot overlay looking for Wi-Fi |
| `boot-step2` | Wi-Fi ticked off, downloading content |
| `boot-step3` | Content ticked off, syncing time |
| `boot-step4` | All four steps done, a moment before the overlay fades |
| `boot-help` | A Wi-Fi problem during the first step, with the help line it puts up |

Adding one is a row in the `SCENARIOS` table in `main.cpp` and, usually, a fixture.

## What it does not cover

- **The ESPHome-declared pages.** Only the runtime pages in `hb_ui.h` are built here. The service
  detail page, the station picker, the Wi-Fi list, the password page and the keyboard are declared
  in `ui.yaml` and belong to ESPHome, so they do not appear.
- **Touch.** There is no input device. Scenarios call `show()` and `step()` directly, so nothing
  exercises the swipe gesture, the long press on a row or the scrolling of the agenda.
- **Font rasterisation.** ESPHome rasterises the four custom fonts at build time with its own
  renderer; here TinyTTF does it at run time. Weights and hinting differ slightly, so the custom
  sizes (the 120 px clock, the 56 px wordmark, the 64 px pairing code, the 48 px icons) are a
  little lighter on screen than on the board. The built-in Montserrat sizes are the same bitmaps
  in both. Google Fonts publishes Montserrat and Figtree as variable fonts only, and TinyTTF is
  stb_truetype underneath, which ignores the weight axis and renders the default instance.
- **The panel.** Colours go through RGB565 as they do on the board, but nothing models the
  backlight, the night dimming window or the panel's own gamma.
