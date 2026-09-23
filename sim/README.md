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
downloads five fonts into `fonts/`, checked against `fonts.sha256`. Both directories are
gitignored, and those two fetches are the only network anything here does; rendering talks to
nothing. Building all of LVGL takes a couple of minutes once, and is incremental after that.

Time is pinned to Friday 18 September 2026, 08:41 local, `TZ=Europe/London`, so a PNG changes when
the UI changes and not otherwise.

## Scenarios

| Name | What it shows |
|---|---|
| `clock` | The clock page of a paired board: the date above the numerals, the temperature below |
| `clock-night` | The same inside the household's night window: muted hues |
| `clock-notice` | The same with a `settings.notice` from the document, on the foot row |
| `clock-problem` | The same with a live problem status on the foot row, which wins over a notice |
| `clock-waiting-time` | SNTP never answered: no time, no date, "Setting the clock.", temperature still showing |
| `board-live` | A rail departures board, five rows, one delayed |
| `board-live-night` | The same board on the night palette |
| `dots` | The same board a moment after a swipe, with the page indicator up |
| `board-stale` | An arrivals board being served from the backend's last good data |
| `board-empty` | A board with no departures in the window |
| `board-unavailable` | A board the backend could not build at all (`asof` 0) |
| `board-cancelled` | A rail board with a cancelled row, a delayed one and one with no platform |
| `board-tube` | A tube board: waits instead of expected times, no platform |
| `board-bus` | A bus board: route numbers in the platform column |
| `day` | The agenda, today marked, spent events dropped, two later days |
| `day-night` | The same diary on the night palette |
| `day-empty` | The agenda with nothing in the next seven days |
| `weather` | The weather face: the hero, the sentence and the hours to come |
| `weather-night` | The same forecast on the night palette |
| `weather-rows-only` | A weather page from a backend older than the face: rows and nothing else |
| `reminders` | The to-do face, open items as rings |
| `reminders-night` | The same list on the night palette |
| `reminders-empty` | The to-do face with nothing left on it |
| `pair` | The pairing page with a code and its QR |
| `pair-night` | The same page at night, which is what re-colours the QR itself |
| `pair-waiting` | Unclaimed, but the pairing call has not come back yet |
| `pair-problem` | A code on screen and the network gone: the problem takes the caption |
| `boot-step1` | The boot overlay looking for Wi-Fi |
| `boot-step2` | Wi-Fi ticked off, downloading content |
| `boot-step3` | Content ticked off, syncing time |
| `boot-step4` | All four steps done, a moment before the overlay fades |
| `boot-step4-night` | The same, with the night palette already applied |
| `boot-help` | A Wi-Fi problem during the first step, with the help line it puts up |

Adding one is a row in the `SCENARIOS` table in `main.cpp` and, usually, a fixture. A `-night`
variant is the day scenario followed by `s.night()`, which is the call hallboard.yaml's
`apply_brightness` makes when the household's window opens.

## What it does not cover

- **The ESPHome-declared pages.** Only the runtime pages in `hb_ui.h` are built here. The service
  detail page, the Wi-Fi list, the password page and the keyboard are declared in `ui.yaml` and
  belong to ESPHome, so they do not appear.
- **Touch.** There is no input device. Scenarios call `show()` and `step()` directly, so nothing
  exercises the swipe gesture, the long press on a row or the scrolling of the agenda. The `dots`
  scenario reaches the page indicator through `step()`, which is the same call the gesture makes.
- **Font rasterisation.** ESPHome rasterises every `font:` entry at build time with its own
  renderer; here TinyTTF does it at run time. Both start from the same outlines: `gfonts://` asks
  the Google CSS API for a weight and gets a static instance of the family back, and the Makefile
  downloads the matching static files (Figtree Regular, Medium, SemiBold and Bold from the
  family's own repository, IBM Plex Mono Regular from Google Fonts). TinyTTF is stb_truetype
  underneath and would ignore a variable font's weight axis, which is why no `[wght]` file is
  fetched. Hinting still differs, so the glyphs are a shade lighter on screen than on the board.
- **The panel.** Colours go through RGB565 as they do on the board, but nothing models the
  backlight or the panel's own gamma. The night *palette* is rendered (the `-night` scenarios);
  the night *dimming* is a backlight level and does not reach a PNG.
