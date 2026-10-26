# CLAUDE.md

Claude Code notes: what breaks, where the reasoning lives (source headers), how to
build, flash and read the devices. Usage: [README.md](README.md). English only.

## What this repository is

- A ZMK module for both ends of the status advertisement: the **Prospector
  Dongle** (shield `prospector`: a BLE observer showing the keyboard's battery as
  an HP bar under a sprite, which a Mac swaps over BLE into the sprite room,
  `CONFIG_BEACON_SPRITE_RECEIVE`) and the **Imprint Dongle**'s broadcaster
  (`CONFIG_BEACON_STATUS_BROADCAST`). Device names follow canon's
  [docs/glossary.md](https://github.com/akira-toriyama/canon/blob/main/docs/glossary.md):
  Cyboard Imprint (the halves), Imprint Dongle (the split central), Prospector
  Dongle; never "XIAO dongle" or "scanner".
- Two roles in one tree ([config/west.yml](config/west.yml)): a Zephyr module
  (`zephyr/module.yml`, `boards/`, `src/`, `Kconfig`, `CMakeLists.txt`) and the
  zmk-config (`config/`, `build.yaml`) that CI and [scripts/build.sh](scripts/build.sh) build.
- The one consumer, [canon](https://github.com/akira-toriyama/canon), pins this
  repository by commit SHA for both dongles (its `config/imprint_dongle.conf`:
  `CONFIG_BEACON_STATUS_BROADCAST=y`, `CONFIG_BT_EXT_ADV_MAX_ADV_SET=2`,
  `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD=y`); a merge here reaches the dongles with
  canon's pin bump.
- `tools/`: `sprite_pack.py` (the sprite pack, [docs/sprite-pack.md](docs/sprite-pack.md):
  a GIF as its distinct pictures, for the player and the over-the-air swap) on
  `gifpics.py` (GIF composition as browsers do it), standard library only so
  that macOS's `/usr/bin/python3` (3.9) and the ZMK build image both run them;
  `sprite_colours.py` (the colours a sprite's GIFs, its front and its back,
  show most, for the key LEDs' palette of canon's keyboard: its docstring
  holds the rule and the numbers measured on the user's collection) on the
  same two;
  `test_sprite_pack.py` and `test_sprite_colours.py` write their own GIFs
  (`python3 -m unittest discover
  tools`, CI `tools-test.yml`); `pkmn_font.py` (its docstring) renders the
  name row's katakana font, `src/font_pkmn.{c,h}`, from the pkmn TTF, which
  is not in the repository (clone nue-of-k/pkmn): run it again rather than
  edit them, and its `preview` prints glyphs as dots. The 1,298-GIF checks against Pillow and the
  round trips are local only: the GIFs are personal files. `sprite-sender/`
  (Swift, no dependencies; README "The sprite sender"): `SpriteSenderCore` is
  pure (config, rotation, name rule, report v1, page plan; `swift test` on
  CI's macOS runner, `tools-test.yml`, as a Mac with only CommandLineTools
  has no XCTest) and `SpriteSender` does files, the Python tools as child
  processes (`Tools.swift`: the pack converter, the colours) and
  CoreBluetooth (Swift 5 language mode: CoreBluetooth's delegate types are
  not Sendable). `package.sh` makes `SpriteSender.app` with the Python
  tools in its Resources, signed with the `chord-dev` identity (canon
  t-3cf6: the Bluetooth permission is keyed to identity and bundle id).
  Nothing it logs names a GIF or the sprite.
- Tasks: `furrow list -r zmk-beacon` (the 2026-09-29 refactor: epic e-vgt5).

## Build

- `./scripts/build.sh [shield | board:shield] [--logging] [--sprite <gif>
  [--sprite-name <text>]] [--kconfig CONFIG_X=V]... [--tag <name>] [--update]`
  (flags: its header). Docker, workspace `$ZMK_WS` (default `~/.cache/zmk-beacon`),
  images `firmware/<shield>[-sprite][-logging][-kconfig][-<tag>].uf2`, then a
  summary (sha256 prefix, FLASH, RAM, revisions). `--update` moves zmk@main.
- The west topdir is never the module root: `$RUNNER_TEMP/ws` in CI, `$ZMK_WS/ws`
  locally, the repository passed as `-DZMK_EXTRA_MODULES` (as ZMK's
  `build-user-config.yml` does). Never `west init` inside the repository.
- Kconfig merge order, later wins (build logs here and in canon, 2026-09-29):
  board defconfig (`xiao_ble_zmk_defconfig`) → ZMK `app/prj.conf` → the shield's
  `prospector.conf` → the consumer's `config/prospector.conf` → the
  `EXTRA_CONF_FILE` fragments (build.sh: sprite, logging, `--kconfig`) →
  `-DCONFIG_*`. Shield facts go in the shield conf, the LVGL pool in
  `Kconfig.defconfig`; canon-only lines (e.g. `CONFIG_ZMK_RGB_UNDERGLOW=n`, forced
  on by the Cyboard module) stay in canon.
- ZMK tracks `main`, never a tag: canon's Cyboard board needs zmk@main and this
  module compiles on the same tree; the weekly run of `build.yml` warns early.
- FLASH / RAM of 788 / 256 KB (zmk 5b51501f, 2026-10-06): plain 25.30% / 67.98%,
  a 89x91 sprite (a 39 KB pack from an 84 KB GIF) 30.72% / 75.65%, a 153x94
  one with `--logging` 32.48% / 81.70%; the gifdec player's 84 KB sprite took
  36.26% / 83.60% and 39.36% / 89.71% with logging (2026-10-04).
- CI: `build.yml` → the local reusable `zmk-build.yml` (it says why not ZMK's);
  `tools-test.yml` runs `tools/`' unit tests (Python on Ubuntu, the sprite
  sender's `swift build` and `swift test` on macOS).
  `release.yml` keeps a rolling draft release of `prospector.uf2`, tagged when
  published by hand. A ruleset requires `build / Build (xiao_ble/nrf52840/zmk, prospector)`.

## Debugging and device operations

The device tools are canon's `scripts/` (their headers document every flag). A
dongle's serial port is silent unless it runs a `--logging` image.

| Goal | Command | Healthy result |
| --- | --- | --- |
| Prospector image from this tree | `./scripts/build.sh --logging` | `images:` line with FLASH/RAM, `revisions: ... zmk-beacon <describe>` |
| Both dongles' images from this tree; the only build of the broadcaster | canon: `./scripts/build-zmk.sh prospector imprint_dongle --module <this checkout> --logging` (`--beacon` until canon #267) | `revisions:` has `zmk-beacon <dir> @ <describe> (--module)`; the images carry `-beacon` |
| Flash either dongle, no double-tap | canon: `./scripts/flash-dongle.sh <image.uf2>` (any directory; named `<device>.uf2` or `<device>-*.uf2`; `--dry-run` checks only) | last line `DONE <device> ... sha256=<the build summary's 12 hex>`, exit 0 |
| Find the dongles | canon: `python3 scripts/dongle.py list` | each dongle with its `/dev/cu.*` port and `port free` |
| Read the logs | canon: `python3 scripts/dongle.py log prospector imprint_dongle --seconds 130 [--grep RE] [--out FILE]` | the lines below, each after a timestamp and the device |
| See the Prospector's screen | canon: `python3 scripts/dongle.py shot --out <scratch>/shot.png`, then Read the PNG | stdout the PNG's path, stderr `280x240 in N bands, CRC-32 ok` |

- `dongle.py log` uses 115200 only, follows a dongle through a reboot (started
  before a flash, it catches the boot log) and drops key-event lines unless `--raw`.
  `shot` sets 2400, then 115200 (the dump: [src/screen_dump.c](src/screen_dump.c)),
  refuses a port another process holds (stop the log reader) and a path in any
  git work tree: a sprite build's picture shows the personal GIF.
- `--logging` keeps the boot log in a 4 KiB CDC ring until the port opens (70 s
  after boot it still held the whole boot log; ZMK's 1 KiB cut it, hardware
  2026-09-29). A late opener gets the oldest 4 KiB first: check the uptime stamps.
- Per build directory (`$ZMK_WS/ws/build/<image name>/`; canon:
  `~/.cache/zmk-canon/cfgrepo/build/<image name>/`): `build.log` (west's output,
  FLASH/RAM at its end), `zephyr/.config`, `zephyr/zephyr.dts`, `zephyr/zmk.map`
  (what was linked, in which order), `zephyr/zmk.elf`.
- Harmless warnings in every build: `The choice symbol SETTINGS_NVS ... no symbol
  ended up as the choice selection` (the board defconfig selects it, settings are
  off; a shield `=n` does not silence it) and `Deprecated symbol KSCAN is enabled`.

Log lines; the periodic ones come every 60 s, the first a minute after boot:

- Observer (Prospector Dongle): `N status payloads in 60 s; so far N keystrokes,
  N counter restarts (last jump N)`. 264-280 payloads a minute with the passive
  scan (sprite logging images, hardware 2026-09-29; 239-269 with the active scan
  before it), 300 at most (one per `BEACON_PAYLOAD_INTERVAL_MS`). A restart is
  the keyboard rebooting (a reflashed Imprint Dongle jumped 182, 2026-09-29).
- Screen (Prospector Dongle): `screen left L right R (fresh, payload N ms ago, N
  keystrokes)` on every HP bar change and once a minute; `stale` (grey) after 60 s.
- Sprite (sprite builds): `sprite N decoded (N us avg, N us max), N presses for
  N key steps and N frames dropped, N invalidated, N renders of N ms in 60 s,
  speed 75%, side N after N switches, lvgl pool N allocated, N max, of N`.
  Decoded counts
  pictures inflated (a frame whose picture is on the canvas decodes nothing);
  side 1 is the back view (a modifier held on the keyboard), switches the
  side changes in the minute;
  presses × 8 = key steps + frames dropped, up to the queue carried across the
  minute's edge. Renders is what the panel showed (LVGL merges invalidations).
  A render took 27-51 ms and a 153x94 picture 6-9 ms (hardware 2026-10-06);
  the pool peaked 4.9 KB above the sprite's block (2026-09-27, 2026-10-06).
- Screen off (Prospector Dongle): `screen off: no key press for N s` a second
  (the fade) after the last key press went N s back, or N + 1 s after boot
  without one, and `screen on: key press` at the next press. While the screen is
  dark the sprite decodes nothing. To watch
  both without waiting five minutes, build with
  `--kconfig CONFIG_BEACON_SCREEN_OFF_AFTER_S=20` (off at 00:00:21.0 and on
  at the next press, log and eye, hardware 2026-10-04).
- Broadcaster (Imprint Dongle): `advertising, N updates ok, N failed, N start
  failures, battery L/R, N keystrokes, N modifiers confirmed, in 60 s`.
  Healthy: `advertising`, 0
  failed, 0 start failures (hardware 2026-09-27); its keystrokes and the
  observer's grow alike (50 and 50 in a minute, 2026-09-29). Modifiers
  confirmed counts, since boot, the modifier holds that reached the payload
  (a thumb's tap is not one).
- Receiver (Prospector Dongle, per transfer): `connected`, `scan paused (0)`,
  `parameters: interval 12 latency 0 timeout 400` (about a second later: the
  central took 15 ms), `begin: N bytes expected, N pages erased in N ms (0)`
  (about 220 ms a page), `end: N of N bytes, flash N ms (0), result 1`
  (`result`: 1 shown, 2 short, 3 not a pack, 4 refused by the player, 5 flash
  error, 6 disconnected; the sprite's `sprite WxH ...` and `sprite as ...` lines
  come between), `disconnected: reason 0x13, N of N bytes`, `scanning`,
  `advertising, connectable, every 1022 ms`. A 206 KB pack: erase 11.1 s,
  transfer 8.5 s, 24 kB/s (hardware 2026-10-12).
- Once: `scanning`, `advertising, connectable, every 1022 ms`, `sprite room: a
  whole pack` or `no pack`, `battery left L right R` (on change), `sprite WxH,
  N bpp, N colours, N pictures, N frames, side N of N, N
  bytes of the lvgl pool for canvas and dictionary` and `sprite as WxH (N.NNx)
  from a N byte pack` (also after each received pack; `sprite: none` when
  nothing plays), `advertising every 200 ms` (the broadcaster), `<uart>: 1200
  baud touch, rebooting into the bootloader`, `screen dump: N bands, N bytes
  in N ms` (after each `shot`).

## Invariants

- **Never set a dongle's serial port to 1200 baud unless you mean to flash it.**
  Any program that sets the rate (a serial monitor, `stty`) reboots the
  Prospector Dongle, and canon's Imprint Dongle, into the UF2 bootloader
  ([src/bootloader_on_1200_baud.c](src/bootloader_on_1200_baud.c): why a warm
  reboot after `bootmode_set()`); 2400 starts a screen dump, through the one rate
  callback per device that the class driver keeps and that file owns. The option
  selects `RETENTION_BOOT_MODE` (a board without retention fails at Kconfig) but
  depends on `USB_CDC_ACM`: without it canon's `=y` is dropped with only a `was
  assigned the value 'y'` warning, so after a ZMK bump grep
  `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD=y` in canon's
  `build/imprint_dongle/zephyr/.config`.
- **Never have both dongles in the bootloader at once, nor enter it while
  canon's `flash-watch.sh` / `flash-reset.sh` run**: both mount as `XIAO-SENSE`,
  and those scripts copy `imprint_dongle.uf2` onto any `XIAO-SENSE` mount.
- Keep `CONFIG_USB_DEVICE_PRODUCT="Prospector Dongle"` (shield conf): canon's
  `dongle.py` and `flash-dongle.sh` find the device by that string alone (VID/PID
  are ZMK's defaults, the Imprint Dongle's too; a `/dev/cu.usbmodem*` name
  follows the USB location). Change canon's `DEVICES` table with it.
- Keep exactly one CDC ACM port: each okay `zephyr,cdc-acm-uart` node adds a USB
  serial function; canon's `dongle.py` opens a device's first port, and two
  ports broke canon's earlier flash script (canon #219, hardware 2026-09-26). The
  shield uses the board's node ([prospector.overlay](boards/shields/prospector/prospector.overlay)).
- Change the payload layout only with a new version byte, both ends in one
  commit ([src/status_payload.h](src/status_payload.h)): an older observer then
  shows no data instead of wrong numbers; canon bumps its pin once.
- Build canon's `imprint_dongle` against a change to the broadcaster, the
  payload or their Kconfig before merging it (runbook row 2): CI here never
  compiles `src/status_broadcaster.c`. Without `CONFIG_BT_EXT_ADV_MAX_ADV_SET=2`
  in the consumer a `BUILD_ASSERT` fails.
- Keep the broadcaster's listener ahead of ZMK's: it counts a press before a
  hold-tap or a combo captures it because ZMK calls listeners in link order and
  the module's objects link first (re-raised presses:
  [src/status_broadcaster.c](src/status_broadcaster.c)). After a ZMK bump, in an
  `imprint_dongle` build directory, `grep -A1 '^ \.event_subscription$' zephyr/zmk.map
  | grep -o '([a-z_]*\.c\.obj)' | head -1` prints `(status_broadcaster.c.obj)` (2026-09-29).
- Remove or rename a Kconfig symbol together with the canon change that stops
  setting it: Kconfig aborts on an assignment to an undefined symbol (canon's
  `config/prospector.conf` and `BEACON_SPRITE_FILL`, 2026-09-29).
- Keep `CONFIG_ZMK_BLE=n` (shield conf): it removes ZMK's `bt_enable()` callers
  and its connectable advertisement; the observer brings Bluetooth up and scans
  passively, and the sprite receiver advertises on its own
  ([src/ble_receiver.c](src/ble_receiver.c)). Never scan with a `BT_LE_SCAN_*`
  helper: they set `FILTER_DUPLICATE` and each address is reported once
  ([src/status_observer.c](src/status_observer.c)).
- The receiver erases and writes flash only from its control callbacks, while
  the central waits for a response: a page written from the data callback,
  with more data arriving, hung the device (2026-10-05, twice, not diagnosed).
  Keep `SOC_FLASH_NRF_PARTIAL_ERASE` (the option selects it, the file asserts
  it): a blocking erase of 51 pages (4.4 s) outlasted the 4 s supervision
  timeout (2026-10-06); sliced, the same erase takes 11 s and the link holds.
- The sprite room is `sprite_partition` in the shield overlay, 0x80000-0xdffff
  (384 KiB, above it the UF2 bootloader's staging), cut from `code_partition`,
  which ends at 0x80000 (356 KiB): the linker stops an image that would reach
  the room. A logging sprite build with the receiver is at 96% of it
  (2026-10-12); if a ZMK bump overflows, a logging build can drop the embedded
  sprite before the room shrinks. The player reads the room in place
  (memory-mapped flash), so BEGIN freezes it before the erase
  ([src/sprite.c](src/sprite.c)).
- Change the receiver's protocol or report only with a new `REPORT_VERSION`
  ([src/ble_receiver.c](src/ble_receiver.c)) and the sender's `Report` and
  `Transfer` (tools/sprite-sender's `SpriteSenderCore`) in the same change:
  the sender rejects a report of another version.
- Touch LVGL only on ZMK's display work queue (`LV_USE_OS=0`): the scan callback
  runs on the BT RX queue and only copies under a spinlock; the screen reads the
  state from an `lv_timer` ([src/prospector_screen.c](src/prospector_screen.c)).
  Display driver calls too: a flush is several SPI transfers, and the blanking
  command of [src/screen_off.c](src/screen_off.c) must not land between them.
- Keep `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE` off (ZMK's default for this panel):
  ZMK's activity returns to active only on the device's own key, sensor and
  pointing events, so a device without keys would blank 30 s after boot for
  good, and the stopped display tick would stop every `lv_timer` with it.
  [src/screen_off.c](src/screen_off.c) turns the screen off instead, on the
  keyboard's key presses; [src/prospector_screen.c](src/prospector_screen.c)
  asserts the option off.
- Opt every LVGL widget and font in: ZMK implies `LV_CONF_MINIMAL`, and a widget
  used without its `CONFIG_LV_USE_*` fails at link time, not in Kconfig (the HP
  bar's are set in the shield conf, the sprite's selected by `BEACON_SPRITE`).
  The katakana font is the module's own (`src/font_pkmn.c`, built with
  `BEACON_HP_BAR_NAME_ROW`); its line is taller than unscii 16's, and
  `BEACON_HP_BAR_BOX_H` follows its header (hp_bar.h).
- A pack's name is 15 bytes of UTF-8 in one of two alphabets, printable ASCII
  or `sprite_pack.KATAKANA_RANGES`, never both: `check_name` is the one rule
  (the converter runs in the build and in the sender), the device picks the
  font by a byte of 0x80 or above, and canon's `sprite-name.sh` and the
  sender's `SpriteName` must stay within it.
- Change the sprite pack only with a new magic (`SPK1`, tools/sprite_pack.py
  and src/sprite_pack.h together): the player rejects a file it does not know
  instead of drawing it wrong.
- Keep the sprite the screen's first child and never render the screen through a
  layer (`opa_layered`, a transform, a blend mode, a bitmap mask): `blit()`
  writes into the display buffer and relies on the screen's fill having landed
  ([src/sprite.c](src/sprite.c)).
- Size the LVGL pool in the shield's
  [Kconfig.defconfig](boards/shields/prospector/Kconfig.defconfig) (64 KiB with a
  sprite, the receiver included, 48 KiB without), never in a `.conf`: a conf
  line, the consumer's too, fixes one size for both. The player keeps two bytes
  a pixel at most (its canvas and dictionary); the 88 KiB of the gifdec player
  took a logging sprite build to 89.71% RAM. [CMakeLists.txt](CMakeLists.txt)
  refuses an embedded GIF whose player does not fit,
  [src/prospector_screen.c](src/prospector_screen.c) one larger than the box;
  a received pack beyond either is refused when opened, and the embedded
  sprite, or none, comes back.
- The panel's SPI runs at 32 MHz on SPIM3 with high drive on its pins (overlay),
  above the ST7789V data sheet's 15 MHz write cycle, as 16 MHz already was: a
  render takes about 43 ms against 63 at 16 MHz (hardware 2026-09-28). If the
  panel shows noise, set `mipi-max-frequency` back to 20 MHz (16 MHz effective).

## Privacy: the sprite

- **The sprite GIF is a personal file, and its name names the subject.** Never
  commit a GIF, a sprite pack, an `.inc`, frames, previews or anything made from
  one, and never write the GIF's path or file name, its subject, its source or
  the sprite name into a commit, a PR, a doc, a test or a workflow here (the
  user's collection is the user's; this repository is public). `.gitignore` has
  `*.[gG][iI][fF]` and `*.spk` (`core.ignorecase` is false on this
  case-sensitive volume). Tests use GIFs they write themselves.
- `CONFIG_BEACON_SPRITE_GIF` takes an absolute path. `build.sh --sprite` copies
  the GIF to `$ZMK_WS/sprite/sprite.gif`, and the build embeds it as
  `build/<image name>/modules/zmk-beacon/beacon_sprite_gif.inc` (build tree only).
- The name (`CONFIG_BEACON_SPRITE_NAME`, `--sprite-name`) goes with the GIF's
  path into the fragment `$ZMK_WS/sprite/sprite.conf`, never into `-DCONFIG_...`:
  west prints the whole cmake command line when the configure step fails
  (reproduced 2026-09-29). The script prints only the GIF's size and the name's
  length, `--kconfig` refuses `CONFIG_BEACON_SPRITE_*`, and no message echoes a
  path or a value. `BEACON_SPRITE_NAME` has no Kconfig dependency, so kconfig.py
  never prints it (its help says why).
- CI and `release.yml` build without a sprite or a name, so no release carries
  either. canon keeps the GIF in its ignored `assets/` and derives the name from
  the file name (`assets/sprite-name.sh` there).
- The katakana font's glyphs are whole blocks (`sprite_pack.KATAKANA_RANGES`),
  never a subset cut to the names in use: a font made of a name's characters
  would tell the subject. Tests and docs use made-up katakana.

## Commits, pull requests, fleet files

- glyph: `<:code:>[(scope)]<sigil> <subject>`, English
  ([CONTRIBUTING.md](https://github.com/akira-toriyama/.github/blob/main/CONTRIBUTING.md),
  codes: `glyph emoji`); `glyph lint --range origin/main..HEAD` before pushing,
  `glyph hook install` once per clone. PR bodies end with the `SetStatus-task:` line.
- Public repository on free Actions minutes: PR gates are deterministic checks
  only; never add a workflow that calls a paid API.
- Hardware claims in docs and task bodies carry a date. Repeat a check only when
  the first result failed or was ambiguous.
- Fleet-managed, never edited here (fleet-sync in akira-toriyama/.github
  overwrites them from its `fleet/`):
  `.github/workflows/{actionlint,commit-lint,repo-policy,taplo,task-status,version-preview,zizmor}.yml`,
  `.github/zizmor.yml`, `.github/dependabot.yml`, `docs/commit-convention.md`.
  `glyph.toml` came from `glyph init --gemoji`: edit it, never regenerate it.
