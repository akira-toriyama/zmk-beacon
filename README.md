# zmk-beacon

A [ZMK](https://zmk.dev) module for the **Prospector Dongle**: a Seeed XIAO
nRF52840 with a Waveshare 1.69" 240x280 LCD (the
[beekeeb pre-soldered Prospector](https://shop.beekeeb.com/products/pre-soldered-prospector-zmk-dongle))
that sits next to a keyboard and shows the battery of each half, received
over BLE from the keyboard's status advertisement. It only listens: it never
advertises, pairs or connects. Its USB-C carries power and one serial port,
used only to reflash it.

It is built for the Cyboard Imprint through its Imprint Dongle
([akira-toriyama/canon](https://github.com/akira-toriyama/canon)). Both ends
of the status advertisement are in this module: the `prospector` shield with
the display, and the broadcaster for the keyboard's split central
(`CONFIG_BEACON_STATUS_BROADCAST`), which replaced
[t-ogura/prospector-zmk-module](https://github.com/t-ogura/prospector-zmk-module)
on the Imprint Dongle.

## Status

The `prospector` shield receives the status advertisement with its own BLE
observer and shows each half's battery (on the Prospector Dongle since
2026-09-26, 19 h without a gap). The broadcaster sends that advertisement
from the Imprint Dongle on a second advertising set next to ZMK's own
(measured on hardware 2026-09-27 as canon's t-eray spike). A local build can
embed a GIF sprite that plays above the readings (canon task t-rx4e; shown
on the Prospector Dongle 2026-09-27) and steps eight frames per key press on
the keyboard (canon task t-7c05, hardware 2026-09-29).

## The screen

The battery readings come in three styles (the `BEACON_READINGS` choice):

- **Digits** (the Kconfig default): the left half's battery in the
  bottom-left corner, the right half's in the bottom-right, Montserrat 48.
- **HP bar** (`CONFIG_BEACON_READINGS_HP_BAR=y`): one battle-screen HP bar,
  `HP` and a bar with its value on it (`65/100`) in a dark box along the
  bottom. Its length is the keyboard's battery, the mean of the halves with
  a reading (or the one half that has one): green, yellow under 50 %, red
  under 20 %. The digits' `--` is an empty track with `--/100` on it, and
  their grey is a grey `HP`, value and name. With a sprite name
  (`CONFIG_BEACON_SPRITE_NAME`, a local build's `--sprite-name`) the box
  has a second row with the name; 15 glyphs fit, a longer name is cut.
- **None** (`CONFIG_BEACON_READINGS_NONE=y`): no readings while a sprite
  shows; without a sprite the digits show, so CI and the release build stay
  valid with that conf.

This repository's own build, and so its release `prospector.uf2`, picks the
HP bar and the fill layout in `config/prospector.conf` (canon's screen); the
shield's default stays the digits.

| Shows (digits) | Meaning |
| --- | --- |
| `75%` in white | The battery of that half, from a status advertisement received in the last minute. |
| `--` in white | The keyboard is heard, but that half has no reading: it is off, out of range, or has not reported since it connected. |
| `--` in grey | No status advertisement in the last minute, or none since the Prospector Dongle started. |

Which half is "left" is decided on the keyboard side: the Imprint Dongle
reports its first-paired half first (canon's CLAUDE.md, split peripheral slot).

## The sprite

With `CONFIG_BEACON_SPRITE_GIF="<absolute path>"` the build embeds a GIF and
the screen plays it above the battery readings, top-centred and scaled by the
largest whole factor that fits between the top edge and the readings (a
90x90 px GIF shows at 2x above the digits on the 280x240 panel). While nobody
types it plays at three quarters of the GIF's own tempo and never stops.
While the keyboard is typed on it runs on the key presses instead: every
press, either half, any key, queues eight visibly different frames, a render
steps one of them while the queue holds one press's worth or less and
several at a time (skipping frames, so the sprite runs faster) as presses
pile up, and 0.2 s after the last press arrived the frames still queued are
dropped and the tempo resumes from the frame shown; the sprite never steps
back. As that 0.2 s equals the advertising interval, a lone press shows
three or four of its frames before the tempo takes over, and while presses
keep coming the queue holds the presses of the last payload, so the sprite
runs at the display's pace, skipping frames in proportion to how fast the
keys come (the user's pick on hardware, canon task t-7c05). The press
travels in the status advertisement as an 8-bit counter that the Imprint
Dongle sends right after the press, so from the key to the first frame
takes the advertising wait (up to 200 ms plus a random delay of up to
10 ms), a render still pending, a decode or two and a render: about
0.1-0.35 s (calculated). Every physical press counts once, at the press,
whether or not a hold-tap or a combo later holds it back or consumes it.

The sprite draws itself: the player scales the decoded frame straight into
the display buffer by nearest neighbour (`src/sprite.c`), and the panel's SPI
runs at 32 MHz. Hardware 2026-09-28 with the 2.2x sprite above the HP bar: a
render, flush included, takes about 43 ms, so the screen showed every step
of a 10-step-a-second GIF played at 150 % (the tempo until 2026-09-29), about
15 frames a second; decoding costs about 11 ms a GIF frame. For comparison,
LVGL's own image transform took 105 ms a render of a 2x sprite (6 frames a
second at 150 %) and the 16 MHz SPI clock 63 ms (10). When drawing falls
behind, the player skips GIF frames to keep the tempo; while typing it steps
once per render, more frames at a time the fuller the queue. 32 MHz is above the ST7789V data sheet's
write cycle, as 16 MHz already was; should the panel show noise, set
`mipi-max-frequency` in the shield overlay back to 20 MHz (16 MHz effective).

`CONFIG_BEACON_SPRITE_FILL=y` lets the sprite fill the space above the
readings instead: scaled by the largest factor of its proportions that fits
between the top edge and the readings, whole or not (about 2.2x for a 90x90 px
GIF above the HP bar, a GIF pixel covering 2 or 3 panel pixels in turn), 2 px
in from the edges and standing on the readings, which keep their 12 px margin
(the panel's corners are rounded; a box closer to them loses its own corners).
The `BEACON_READINGS` choice above picks the readings' style, or none. Without
a GIF the option does nothing, so it can stay in a consumer's conf.

Limits:

- GIF89a with a global colour table only (what LVGL's gifdec opens), no
  larger than the sprite box, with at least one frame. The box is the panel
  down to the readings' top: 280x185 above the digits, 280x200 above the HP
  bar (280x182 when it carries a sprite name), 280x240 with
  `BEACON_READINGS_NONE`, and 4 px narrower and 2 px shorter with
  `BEACON_SPRITE_FILL` (276x198 above the HP bar, 276x180 with a name). The build
  rejects anything else, and a GIF whose decoder state (5 bytes per pixel
  plus 16 KiB) does not fit the LVGL pool next to the screen; the error names
  the `CONFIG_LV_Z_MEM_POOL_SIZE` that would fit. The shield's pool grows from
  48 KiB to 88 KiB when a sprite is configured, room for about 110x110 px.
- Frame delays of 0 and 10 ms play as 100 ms, as browsers do.
- gifdec ignores disposal 3 (restore to previous), and a frame without its
  own graphic control extension reuses the previous frame's delay and
  transparency. Such a GIF shows trails or holes on the device.
- The GIF is a personal file and never enters a repository: build locally with
  `./scripts/build.sh --sprite <gif>` (`firmware/prospector-sprite.uf2`;
  `--logging` combines). `--sprite-name <text>` shows the sprite's name under
  the HP bar (printable ASCII, no quote, backslash or `??`; it is the
  subject's name, so it stays out of repositories like the GIF). CI and the
  release build without a sprite or a name.

## What the module provides

| Path | What |
| --- | --- |
| `boards/shields/prospector/` | The shield: ST7789V panel over SPI3, PWM backlight on D6 (P1.11), a dummy kscan (ZMK needs one), one USB CDC ACM port and no HID device. `prospector.conf` holds the defaults a consumer can override. |
| `src/status_observer.c` | The BLE observer. It brings Bluetooth up itself (`CONFIG_ZMK_BLE=n` in the shield, so ZMK never advertises), scans actively without a duplicate filter, and reads each half's battery and the key press count from the status payload. |
| `src/status_broadcaster.c` | `CONFIG_BEACON_STATUS_BROADCAST`: on a keyboard's split central, sends the status payload as manufacturer data on a second, legacy, non-connectable advertising set next to ZMK's own, every 200 ms and right after a key press. |
| `src/status_payload.h` | The payload both sides share: 26 bytes, the prospector-zmk-module v2.2.3 layout, of which the battery bytes, the active layer's index and name and the key press counter are used. |
| `src/prospector_screen.c` | ZMK custom status screen (LVGL 9): places the readings style along the bottom and the sprite above it, and feeds the readings the observer's state every 500 ms. |
| `src/readings.h` | The readings style interface: where it starts, create, show. `src/readings_digits.c` is the digits (the default, and the fallback of `BEACON_READINGS_NONE` without a sprite). |
| `src/hp_bar.c` | `CONFIG_BEACON_READINGS_HP_BAR`: the HP bar readings, `HP`, a bar with its value on it and, with `CONFIG_BEACON_SPRITE_NAME`, the sprite's name on a second row, in a box along the bottom; and the mapping of both halves' batteries to its one level. A sibling of the sprite, not a part of it. |
| `src/sprite.c` | `CONFIG_BEACON_SPRITE`: the GIF player, an own player on LVGL's gifdec with a tempo factor and eight frames per key press, one invalidation per changed frame, an endless loop, and its own nearest-neighbour draw into the display buffer. |
| `src/bootloader_on_1200_baud.c` | `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD`: opening the serial port at 1200 baud reboots the device into its UF2 bootloader. On by default for the shield. |
| `Kconfig` | The `BEACON_*` options (`BEACON_BACKLIGHT_BRIGHTNESS`, `BEACON_BOOTLOADER_ON_1200_BAUD`, `BEACON_SPRITE_GIF`, `BEACON_SPRITE_FILL`, `BEACON_SPRITE_NAME`, the `BEACON_READINGS` choice, `BEACON_STATUS_BROADCAST`, `BEACON_STATUS_BROADCAST_INTERVAL_MS`). |

## Flashing

The device enumerates on USB as `Prospector Dongle` (one serial port, no
keyboard). Two ways into the UF2 bootloader, which mounts as `XIAO-SENSE`:

- From the host, no hands on the device:
  `stty -f /dev/cu.usbmodemXXXX 1200`, wait for `/Volumes/XIAO-SENSE`, then
  `cp -X firmware/prospector.uf2 /Volumes/XIAO-SENSE/`. The `cp` ends with an
  I/O error: that is the device rebooting into the new firmware.
  canon's `scripts/flash-prospector.sh` automates this.
- Double-tap the reset button, then the same `cp -X`.

The Imprint Dongle mounts as the same `XIAO-SENSE` volume. Never have both in
the bootloader at once, and keep canon's `flash-watch.sh` / `flash-reset.sh`
stopped while flashing this device: they copy `imprint_dongle.uf2` onto any
`XIAO-SENSE` mount.

## Using the module in a ZMK config

`config/west.yml`:

```yaml
manifest:
  remotes:
    - name: akira-toriyama
      url-base: https://github.com/akira-toriyama
  projects:
    - name: zmk-beacon
      remote: akira-toriyama
      revision: main # or a release tag
      path: modules/zmk-beacon
```

`build.yaml`:

```yaml
include:
  - board: xiao_ble/nrf52840/zmk
    shield: prospector
```

`config/prospector.conf` in the consuming repository overrides any line of the
shield's own `prospector.conf`.

On the keyboard side, the split central's config (canon:
`config/imprint_dongle.conf`) turns the broadcaster on:

```
CONFIG_BEACON_STATUS_BROADCAST=y
CONFIG_BT_EXT_ADV_MAX_ADV_SET=2
```

The second line is required: the option selects `CONFIG_BT_EXT_ADV`, under
which ZMK's own advertising takes one of the sets. Peripheral builds compile
the broadcaster out. The Imprint Dongle keeps its double-tap into the
bootloader; only the Prospector Dongle listens for 1200 baud.

## Building this repository

```sh
./scripts/build.sh              # every target in build.yaml -> firmware/prospector.uf2
./scripts/build.sh --logging    # firmware/prospector-logging.uf2 (CONFIG_ZMK_USB_LOGGING=y, console on the serial port)
./scripts/build.sh --sprite ~/a.gif  # firmware/prospector-sprite.uf2 (local only; --logging combines to -sprite-logging)
./scripts/build.sh --sprite ~/a.gif --sprite-name "A"  # ... with the name under the HP bar (config/prospector.conf picks the bar)
./scripts/build.sh --update     # refresh zmk@main and its modules first
```

Docker is required (`zmkfirmware/zmk-build-arm:stable`). The west workspace
lives in `~/.cache/zmk-beacon`; the first run downloads ZMK and its modules.

CI builds every `build.yaml` target on pull requests and on `main`, and once a
week against the current ZMK `main`. Every push to `main` refreshes one rolling
draft release with `prospector.uf2` attached; publishing the draft by hand
creates the tag.

## Hardware notes

- The beekeeb pre-soldered unit has no APDS9960 ambient light sensor and its
  touch panel is not wired, so the backlight is fixed
  (`CONFIG_BEACON_BACKLIGHT_BRIGHTNESS`, default 80) and there is no touch
  input.
- Pin mapping and panel parameters are the Prospector's, taken from
  prospector-zmk-module (see `prospector.overlay`).

## Credits and license

MIT. The overlay, the panel parameters and the backlight initialization derive
from [t-ogura/prospector-zmk-module](https://github.com/t-ogura/prospector-zmk-module)
(MIT), itself derived from
[carrefinho's Prospector](https://github.com/carrefinho/prospector-zmk-module).
