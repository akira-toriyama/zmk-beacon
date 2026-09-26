# zmk-beacon

A [ZMK](https://zmk.dev) module for the **Prospector Dongle**: a Seeed XIAO
nRF52840 with a Waveshare 1.69" 240x280 LCD (the
[beekeeb pre-soldered Prospector](https://shop.beekeeb.com/products/pre-soldered-prospector-zmk-dongle))
that sits next to a keyboard and shows its state, the active layer and the
battery of each half, received over BLE from the keyboard's status
advertisement. It never pairs or connects. Its USB-C carries power and one
serial port, used only to reflash it.

It is built for the Cyboard Imprint through its Imprint Dongle
([akira-toriyama/canon](https://github.com/akira-toriyama/canon)), which today
broadcasts with
[t-ogura/prospector-zmk-module](https://github.com/t-ogura/prospector-zmk-module).
This module replaces that dependency end to end: the display side first (the
`prospector` shield here), then the keyboard side (the broadcaster).

## Status

Skeleton. The `prospector` shield builds; its screen shows placeholder text
and receives nothing yet. The BLE observer, the real screen and the
keyboard-side broadcaster follow (canon tasks t-5gxp, t-eray, t-k8pk). The
parts moved here from canon were run on hardware there (USB layout, 1200 baud
bootloader entry, backlight); nothing has been flashed from this repository
yet.

## What the module provides

| Path | What |
| --- | --- |
| `boards/shields/prospector/` | The shield: ST7789V panel over SPI3, PWM backlight on D6 (P1.11), a dummy kscan (ZMK needs one), one USB CDC ACM port and no HID device. `prospector.conf` holds the defaults a consumer can override. |
| `src/prospector_screen.c` | ZMK custom status screen (LVGL 9). Placeholder until the observer lands. |
| `src/bootloader_on_1200_baud.c` | `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD`: opening the serial port at 1200 baud reboots the device into its UF2 bootloader. On by default for the shield. |
| `Kconfig` | The `BEACON_*` options (`BEACON_BACKLIGHT_BRIGHTNESS`, `BEACON_BOOTLOADER_ON_1200_BAUD`). |

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

## Building this repository

```sh
./scripts/build.sh              # every target in build.yaml -> firmware/prospector.uf2
./scripts/build.sh --logging    # firmware/prospector-logging.uf2 (CONFIG_ZMK_USB_LOGGING=y, console on the serial port)
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
