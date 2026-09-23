# Meteocons (line, static)

Source: https://github.com/basmilius/meteocons

Pinned at tag `v2.0.0`, commit `fdd19749b9389f0b4e00d55584b083a7ab2deded`. Fetched from
`https://cdn.jsdelivr.net/gh/basmilius/meteocons@v2.0.0/production/line/all/<name>.svg`, the
static (non-animated-at-rest) line set. `LICENSE` in this directory is that tag's own, copied
verbatim: MIT, Copyright (c) 2020-2021 Bas Milius.

These SVGs are not drawn in their original colours: `firmware/sim/tools/icons.py` rasterises each
one to an alpha-only mask (luminance-independent coverage) and generates `firmware/hb_icons.h`, an
LVGL A8 image descriptor per icon per size. The device then draws them with `lv_image` recoloured
by a design token, so the night palette repaints them like everything else. See that script's own
docstring for how to regenerate, and the main sim README for the local rasteriser setup.

## Files and their sha256

| File | sha256 |
| --- | --- |
| `clear-day.svg` | `49d8c73159dd877b4ba70f55a26a6bceb381b2c7b94179b62e6f3aa0ced92259` |
| `clear-night.svg` | `15c7cb78d651a78749cb71e7e5cbb27f9573ed4545cda0fe37e28c86e43fbbce` |
| `partly-cloudy-day.svg` | `807a1a448bb3727f1dc720ec2bfaf512bcb74eb0c20b495d27f0120dc5bf179f` |
| `overcast.svg` | `b590fedd73f4a2bc8beaf5d8ca2c0391cfd41e3cf716250833deeffd413ba442` |
| `rain.svg` | `b26b9aff2366d5e0104095b848cd2c2302011a7e241e5453a0b20859a0fa5c9a` |
| `raindrops.svg` | `b26d7a4a3e2c070d0bdbc3fd423a5c4a92529c11b62df7077de102acc530eee0` |
| `snow.svg` | `050f823cfd4be2d4500a8fa909d4df6ec2650019a2e905a60fad5e67631cd02c` |
| `fog.svg` | `3b9c20d009475b05870a0415b8df7fc7644405799cbb8f66ac0ea7ad5a10fefd` |
| `thunderstorms.svg` | `745c5115470ec333b35c28f01ece9bdcbbbace47b35373ca68ebc030d97ced0e` |
| `wind.svg` | `e44ecfa2e56b235f8a7f4cf9c7568862d4c13aefb345d65e6489f977c0460211` |
| `raindrop.svg` | `f9d530f92306a759d70ffd1fbc67b5928e97a9384ff77a3905e0f2ed53107924` |
| `umbrella.svg` | `058ced00c996bfc1294f15b370a3897a807465c9794dace2eb7c3f147e6a8a64` |

## Name mapping

HallBoard's icon names (`docs/screen-document.md`'s `i` fields, both the page's current-conditions
icon and each hourly slot's) to the file each one draws:

| HallBoard name | Meteocons file | Used for |
| --- | --- | --- |
| `sun` | `clear-day` | current conditions, hourly slots |
| `night` | `clear-night` | current conditions, hourly slots |
| `partly` | `partly-cloudy-day` | current conditions, hourly slots |
| `cloud` | `overcast` | current conditions, hourly slots |
| `rain` | `rain` | current conditions, hourly slots |
| `pour` | `raindrops` | current conditions, hourly slots |
| `snow` | `snow` | current conditions, hourly slots |
| `fog` | `fog` | current conditions, hourly slots |
| `storm` | `thunderstorms` | current conditions, hourly slots |
| `wind` | `wind` | current conditions, hourly slots |
| (fixed) | `raindrop` | the rain card (card 3) when `rday` < 50 |
| (fixed) | `umbrella` | the rain card (card 3) when `rday` >= 50 |

`pour` is a judgement call: the set has no icon explicitly named for heavy rain or a downpour (no
`heavy-rain`, `downpour`, etc., in `production/line/all/`). `raindrops.svg` (two large drop shapes)
reads as visually heavier than `rain.svg` (a cloud with three light streaks, used for `rain`), so it
stands in for `pour`. If a later Meteocons release adds a dedicated heavy-rain icon, prefer it.

Icons carry `<animate>`/`<animateTransform>` elements for their web use; the rasteriser renders the
SVG's static base state only (no animation), which is what a still 24-32 px mask wants anyway.
