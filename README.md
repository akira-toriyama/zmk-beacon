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
embed a GIF sprite that plays above the readings at the keyboard's typing
speed (canon task t-rx4e; shown on the Prospector Dongle 2026-09-27).

## The screen

The left half's battery sits in the bottom-left corner, the right half's in
the bottom-right.

| Shows | Meaning |
| --- | --- |
| `75%` in white | The battery of that half, from a status advertisement received in the last minute. |
| `--` in white | The keyboard is heard, but that half has no reading: it is off, out of range, or has not reported since it connected. |
| `--` in grey | No status advertisement in the last minute, or none since the Prospector Dongle started. |

Which half is "left" is decided on the keyboard side: the Imprint Dongle
reports its first-paired half first (canon's CLAUDE.md, split peripheral slot).

## The sprite

With `CONFIG_BEACON_SPRITE_GIF="<absolute path>"` the build embeds a GIF and
the screen plays it above the battery readings, top-centred and scaled by the
largest integer factor that fits between the top edge and the digits (a
90x90 px GIF shows at 2x on the 280x240 panel). The tempo follows the keyboard:

| Keyboard                                              | Sprite                                                                 |
| ----------------------------------------------------- | ---------------------------------------------------------------------- |
| Typed on                                              | The GIF's own tempo plus 2 % per WPM, capped at 300 % (100 WPM and up) |
| Not typed on, or not heard                            | The GIF's own tempo                                                    |

The sprite never stops. WPM is what ZMK computes on the Imprint Dongle
(`CONFIG_ZMK_WPM`, selected by the broadcaster) from keycode releases only,
so `&vkey`, layer and mouse keys do not count; it travels in byte 24 of the
status payload and reads 0 1-6 s after the last typed key, which brings the
sprite back to its own tempo. A WPM from a payload older than 5 s counts as
0.

Limits:

- GIF89a with a global colour table only (what LVGL's gifdec opens), at most
  280x185 px (the box above the digits), with at least one frame. The build
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
  `--logging` combines). CI and the release build without a sprite.

## What the module provides

| Path | What |
| --- | --- |
| `boards/shields/prospector/` | The shield: ST7789V panel over SPI3, PWM backlight on D6 (P1.11), a dummy kscan (ZMK needs one), one USB CDC ACM port and no HID device. `prospector.conf` holds the defaults a consumer can override. |
| `src/status_observer.c` | The BLE observer. It brings Bluetooth up itself (`CONFIG_ZMK_BLE=n` in the shield, so ZMK never advertises), scans actively without a duplicate filter, and reads each half's battery from the status payload. |
| `src/status_broadcaster.c` | `CONFIG_BEACON_STATUS_BROADCAST`: on a keyboard's split central, sends the status payload as manufacturer data on a second, legacy, non-connectable advertising set next to ZMK's own, every 200 ms. |
| `src/status_payload.h` | The payload both sides share: 26 bytes, the prospector-zmk-module v2.2.3 layout, of which the battery bytes, the active layer's index and name, and the WPM byte are used. |
| `src/prospector_screen.c` | ZMK custom status screen (LVGL 9): the battery screen above, and the sprite's tempo from the payload's WPM. |
| `src/sprite.c` | `CONFIG_BEACON_SPRITE`: the GIF player, an own player on LVGL's gifdec with speed control, one invalidation per changed frame and an endless loop. |
| `src/bootloader_on_1200_baud.c` | `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD`: opening the serial port at 1200 baud reboots the device into its UF2 bootloader. On by default for the shield. |
| `Kconfig` | The `BEACON_*` options (`BEACON_BACKLIGHT_BRIGHTNESS`, `BEACON_BOOTLOADER_ON_1200_BAUD`, `BEACON_SPRITE_GIF`, `BEACON_STATUS_BROADCAST`, `BEACON_STATUS_BROADCAST_INTERVAL_MS`). |

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
