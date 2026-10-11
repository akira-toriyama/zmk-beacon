# zmk-beacon

A [ZMK](https://zmk.dev) module for the **Prospector Dongle**: a Seeed XIAO
nRF52840 with a Waveshare 1.69" 240x280 LCD (the
[beekeeb pre-soldered Prospector](https://shop.beekeeb.com/products/pre-soldered-prospector-zmk-dongle))
that sits next to a keyboard and shows its battery, received over BLE from the
keyboard's status advertisement. It only listens: it never advertises, pairs
or connects, and its scan is passive. Its USB-C carries power and one serial
port, used to reflash it, to take a picture of its screen and, in a logging
build, for its log.

It is built for the Cyboard Imprint through its Imprint Dongle
([akira-toriyama/canon](https://github.com/akira-toriyama/canon)). Both ends
of the status advertisement are in this module: the `prospector` shield with
the display, and the broadcaster for the keyboard's split central
(`CONFIG_BEACON_STATUS_BROADCAST`), which replaced
[t-ogura/prospector-zmk-module](https://github.com/t-ogura/prospector-zmk-module)
on the Imprint Dongle.

## Status

The `prospector` shield receives the status advertisement with its own BLE
observer and shows the keyboard's battery (on the Prospector Dongle since
2026-09-26). The broadcaster sends that advertisement from the Imprint Dongle
on a second advertising set next to ZMK's own (on hardware since 2026-09-27).
A local build can embed a GIF sprite that plays above the HP bar and steps
with the key presses on the keyboard (on hardware since 2026-09-29).

## The screen

One battle-screen HP bar in a dark box along the bottom: `HP` and a bar with
its value on it (`65/100`). Its length is the keyboard's battery: the mean of
both halves' readings, or the one half that has a reading. The bar is green,
yellow under 50 % and red under 20 %. A build with a sprite name
(`CONFIG_BEACON_SPRITE_NAME`, `--sprite-name`) shows the name on a second row
of the box; 15 characters fit, a longer name is cut.

| Shows | Meaning |
| --- | --- |
| A coloured bar and `65/100` | The keyboard's battery, from a status advertisement received in the last minute. |
| An empty bar and `--/100` | The keyboard is heard, but neither half has a reading: both are off or out of range, or have not reported since they connected. |
| Grey `HP` and `--/100` | No status advertisement in the last minute, or none since the Prospector Dongle started. |
| Nothing: the screen is dark | No key press on the keyboard for five minutes. |

The screen turns off when the keyboard has not been typed on for five minutes
(`CONFIG_BEACON_SCREEN_OFF_AFTER_S`, default 300; 0 keeps it lit): the
backlight fades out within a second, and the next key press, either half, any
key, lights it at once. Only the keyboard's key presses count, so the screen
also goes dark while only a pointing device is in use, and five minutes after
the Prospector Dongle started when no key press has reached it by then.

## The sprite

With `CONFIG_BEACON_SPRITE_GIF="<absolute path>"` the build converts a GIF
into a sprite pack (below), embeds that, and the screen plays it above the HP
bar: centred, standing on the bar, and scaled to the largest size of its own
proportions that fits the space above it, 2 px in from the panel's top and
sides (a 90x90 px GIF shows at 198x198, or 180x180 above a bar with a name).

While nobody types it plays at three quarters of the GIF's own tempo and stops
only while the screen is off. While the keyboard is typed on it runs on the key
presses instead: every
press, either half, any key, queues eight visibly different frames. A render
steps one of them while the queue holds one press's worth or less, and several
at a time (skipping frames, so the sprite runs faster) as presses pile up.
0.2 s after the last press arrived, the frames still queued are dropped and
the tempo resumes from the frame shown; the sprite never steps back. As that
0.2 s equals the advertising interval, a lone press shows three or four of its
frames before the tempo takes over. Every physical press counts once, at the
press, whether or not a hold-tap or a combo later holds it back or consumes it.

The sprite draws itself: the player inflates the frame's picture into a
packed canvas and scales it straight into the display buffer by nearest
neighbour (`src/sprite.c`), and the panel's SPI runs at 32 MHz. Redrawing a
sprite shown at about 2x takes 27-51 ms, flush included, and inflating a
153x94 picture 6-9 ms (hardware 2026-10-06); a frame whose picture is already
on the canvas costs nothing, and catching up after a slow render moves the
frame index without decoding. 32 MHz is above the ST7789V data sheet's write
cycle, as 16 MHz already was; should the panel show noise, set
`mipi-max-frequency` in the shield overlay back to 20 MHz (16 MHz effective).

Limits:

- Any GIF a browser plays (GIF87a or GIF89a, disposal 0-3, transparency,
  interlace, local colour tables), with at least one frame and at most 255
  colours in all, no larger than the sprite box: 276x198, or 276x180 with a
  sprite name. A larger GIF stops the build with `static assertion failed:
  "CONFIG_BEACON_SPRITE_GIF is wider than the sprite box: ..."` or `"... is
  taller than the sprite box: ..."`. A GIF whose player (its canvas and
  dictionary: two bytes a pixel with more than 15 colours, one otherwise, plus
  8 KiB) does not fit the LVGL pool next to the screen stops it in CMake, and
  the message names the `CONFIG_LV_Z_MEM_POOL_SIZE` that would fit. The
  shield's pool grows from 48 KiB to 64 KiB when a sprite is configured: room
  for any GIF of the box at 15 colours, or about 200x140 px beyond that.
- Frame delays of 0 and 10 ms play as 100 ms, as browsers do (the converter
  writes them so).
- The GIF is a personal file and never enters a repository: build locally with
  `./scripts/build.sh --sprite <gif>` (`firmware/prospector-sprite.uf2`).
  `--sprite-name <text>` adds the name (printable ASCII without a double quote,
  a backslash or `??`); it names the GIF's subject, so it stays out of
  repositories like the GIF, and the script prints neither. CI and the release
  build without a sprite or a name.

### The sprite pack

`tools/sprite_pack.py` turns a GIF, and optionally a second one for the back
view, into a sprite pack (`.spk`): the distinct pictures the GIF shows, packed
and deflated against the first, with the order and delays, the name, a length
and a CRC-32. It is what the player plays, embedded by the build from
`CONFIG_BEACON_SPRITE_GIF`, and the unit of a sprite swapped over the air
without a reboot; [docs/sprite-pack.md](docs/sprite-pack.md) explains the
format and what was measured. Standard library Python 3.9 or
later:

```sh
python3 tools/sprite_pack.py pack --name <text> -o <out.spk> <front.gif> [<back.gif>]
python3 tools/sprite_pack.py check <pack.spk> <front.gif> [<back.gif>]   # every frame against the GIFs
python3 tools/sprite_pack.py info <pack.spk>
```

The GIF, the pack and the name are personal and stay out of repositories,
as above; the tool prints sizes only.

## What the module provides

| Path | What |
| --- | --- |
| `boards/shields/prospector/` | The shield: ST7789V panel over SPI3, PWM backlight on D6 (P1.11), a dummy kscan (ZMK needs one), one USB CDC ACM port and no HID device. `prospector.conf` holds the defaults a consumer can override. |
| `src/status_observer.c` | The BLE observer. It brings Bluetooth up itself (`CONFIG_ZMK_BLE=n` in the shield, so ZMK never advertises), scans passively without a duplicate filter, and reads each half's battery and the key press count from the status payload. |
| `src/status_broadcaster.c` | `CONFIG_BEACON_STATUS_BROADCAST`: on a keyboard's split central, sends the status payload as manufacturer data on a second, legacy, non-connectable advertising set next to ZMK's own, every 200 ms; a key press refreshes the payload for the next advertising event. |
| `src/status_payload.h` | The payload both sides share: 26 bytes at prospector-zmk-module v2.2.3's offsets, of which the battery bytes, the active layer's index and name and the key press counter are used. |
| `src/prospector_screen.c` | ZMK custom status screen (LVGL 9): the HP bar along the bottom and the sprite above it, fed the observer's state every 500 ms; checks at compile time that the GIF fits. |
| `src/hp_bar.c`, `src/hp_bar.h` | The HP bar: `HP`, the bar with its value and, with `CONFIG_BEACON_SPRITE_NAME`, the name on a second row; and the mapping of both halves' batteries to its one level. |
| `src/sprite.c`, `src/sprite_pack.c` | `CONFIG_BEACON_SPRITE`: the sprite pack player, with a tempo, eight frames per key press, one invalidation per changed picture, an endless loop, and its own nearest-neighbour draw into the display buffer; and the pack's reader and inflater, plain C that also compiles on the host. |
| `src/bootloader_on_1200_baud.c` | `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD`: setting the serial port to 1200 baud reboots the device into its UF2 bootloader. On by default for the shield; canon turns it on for the Imprint Dongle too. |
| `src/screen_dump.c` | `CONFIG_BEACON_SCREEN_DUMP`: setting the serial port to 2400 baud makes the device render its screen once more and send it over the port, band by band, with a CRC-32. On by default for the shield. |
| `src/screen_off.c` | `CONFIG_BEACON_SCREEN_OFF_AFTER_S`: fades the backlight out and blanks the panel once no key press has arrived for that long, and lights both at the next key press. The screen keeps being drawn while it is dark; only the sprite stands still. |
| `src/backlight.c` | Lights the backlight (`CONFIG_BEACON_BACKLIGHT_BRIGHTNESS`) at boot, before LVGL and ZMK's display start, and sets it for `src/screen_off.c`. |
| `Kconfig` | The `BEACON_*` options: `BEACON_BACKLIGHT_BRIGHTNESS`, `BEACON_BOOTLOADER_ON_1200_BAUD`, `BEACON_SCREEN_DUMP`, `BEACON_SCREEN_OFF_AFTER_S`, `BEACON_SPRITE_GIF`, `BEACON_SPRITE_NAME`, `BEACON_STATUS_BROADCAST`. |

## Flashing

The device enumerates on USB as `Prospector Dongle` (one serial port, no
keyboard). Find its port by that product string, for example with canon's
`python3 scripts/dongle.py port prospector`: never by a `/dev/cu.usbmodem*`
name, which follows the USB socket it is plugged into, nor by its USB IDs,
which the Imprint Dongle shares. Two ways into the UF2 bootloader, which
mounts as `XIAO-SENSE`:

- From the host, no hands on the device: canon's
  `./scripts/flash-dongle.sh <image.uf2>` sets the port to 1200 baud, waits for
  `XIAO-SENSE`, checks that the volume belongs to this device, copies the image
  and waits for the device to come back. The image can be any path, e.g.
  `../zmk-beacon/firmware/prospector.uf2`.
- Double-tap the reset button, then `cp -X <image>.uf2 /Volumes/XIAO-SENSE/`.
  The `cp` ends with an I/O error: that is the device rebooting into the new
  firmware.

The Imprint Dongle mounts as the same `XIAO-SENSE` volume, and canon gives it
the same 1200 baud entry. Never have both in the bootloader at once, and keep
canon's `flash-watch.sh` / `flash-reset.sh` stopped while flashing this
device: they copy `imprint_dongle.uf2` onto any `XIAO-SENSE` mount.

## A picture of the screen

canon's `python3 scripts/dongle.py shot` writes a PNG of what the screen shows
and prints its path. It sets the serial port to 2400 baud, on which the device
renders the screen once more and sends it; then it sets 115200 again. The
screen stands still while the picture goes out, and a logging build leaves out
the log lines of that moment. The picture is what the device draws, not what
the backlight shows: a dark screen gives the picture it would show when lit. A
picture of a sprite build shows the sprite: keep it out of repositories like
the GIF.

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
# Optional: reflash from the host without a double-tap (needs a CDC ACM port)
CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD=y
```

The second line is required: the broadcaster selects `CONFIG_BT_EXT_ADV`,
under which ZMK's own advertising takes one of the sets. Peripheral builds
compile the broadcaster out.

## Building

```sh
./scripts/build.sh              # every target in build.yaml -> firmware/prospector.uf2
./scripts/build.sh --logging    # firmware/prospector-logging.uf2: the log on the serial port
./scripts/build.sh --sprite ~/a.gif --sprite-name "A"  # firmware/prospector-sprite.uf2 (local only)
./scripts/build.sh --kconfig CONFIG_LV_USE_SYSMON=y --kconfig CONFIG_LV_USE_PERF_MONITOR=y --tag perf  # firmware/prospector-kconfig-perf.uf2
./scripts/build.sh --update     # refresh zmk@main and its modules first
```

Docker is required (`zmkfirmware/zmk-build-arm:stable`). The west workspace
lives in `~/.cache/zmk-beacon`; the first run downloads ZMK and its modules.
Each run ends with the images' sha256, flash and RAM use, and the revisions
they were built from.

The dongles run canon's images, built against the revision canon pins. To try
a change first, canon builds both against a local checkout of this repository,
`./scripts/build-zmk.sh prospector imprint_dongle --beacon <path to zmk-beacon>`
(the only build of the broadcaster), and flashes them with
`./scripts/flash-dongle.sh <image>`.

CI builds every `build.yaml` target on pull requests and on `main`, and once a
week against the current ZMK `main`. Every push to `main` refreshes one rolling
draft release with `prospector.uf2` attached; publishing the draft by hand
creates the tag.

## Hardware notes

- The beekeeb pre-soldered unit has no APDS9960 ambient light sensor and its
  touch panel is not wired, so a lit screen has one brightness
  (`CONFIG_BEACON_BACKLIGHT_BRIGHTNESS`, default 80) and there is no touch
  input.
- Pin mapping and panel parameters are the Prospector's, taken from
  prospector-zmk-module (see `prospector.overlay`).

## Credits and license

MIT. The overlay, the panel parameters and the backlight initialization derive
from [t-ogura/prospector-zmk-module](https://github.com/t-ogura/prospector-zmk-module)
(MIT), itself derived from
[carrefinho's Prospector](https://github.com/carrefinho/prospector-zmk-module).
