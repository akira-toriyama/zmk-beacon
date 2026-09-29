# CLAUDE.md

Claude Code notes: what breaks, where the reasoning lives (source headers), how to
build, flash and read the devices. Usage: [README.md](README.md). English only.

## What this repository is

- A ZMK module for both ends of the status advertisement: the **Prospector
  Dongle** (shield `prospector`: a BLE observer showing the keyboard's battery as
  an HP bar, optionally under a GIF sprite) and the **Imprint Dongle**'s
  broadcaster (`CONFIG_BEACON_STATUS_BROADCAST`). Device names follow canon's
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
- FLASH / RAM of 788 / 256 KB (zmk 9ebbeff0, 2026-09-29): plain 25.11% / 67.88%,
  `--logging` 27.95% / 73.99%, an 84 KB sprite 36.07% / 83.56%, both 39.06% / 89.61%.
- CI: `build.yml` → the local reusable `zmk-build.yml` (it says why not ZMK's).
  `release.yml` keeps a rolling draft release of `prospector.uf2`, tagged when
  published by hand. A ruleset requires `build / Build (xiao_ble/nrf52840/zmk, prospector)`.

## Debugging and device operations

The device tools are canon's `scripts/` (their headers document every flag). A
dongle's serial port is silent unless it runs a `--logging` image.

| Goal | Command | Healthy result |
| --- | --- | --- |
| Prospector image from this tree | `./scripts/build.sh --logging` | `images:` line with FLASH/RAM, `revisions: ... zmk-beacon <describe>` |
| Both dongles' images from this tree; the only build of the broadcaster | canon: `./scripts/build-zmk.sh prospector imprint_dongle --beacon <this checkout> --logging` | `revisions:` ends `zmk-beacon <dir> @ <describe> (--beacon)` |
| Flash either dongle, no double-tap | canon: `./scripts/flash-dongle.sh <image.uf2>` (any directory; named `<device>.uf2` or `<device>-*.uf2`; `--dry-run` checks only) | last line `DONE <device> ... sha256=<the build summary's 12 hex>`, exit 0 |
| Find the dongles | canon: `python3 scripts/dongle.py list` | each dongle with its `/dev/cu.*` port and `port free` |
| Read the logs | canon: `python3 scripts/dongle.py log prospector imprint_dongle --seconds 130 [--grep RE] [--out FILE]` | the lines below, each after a timestamp and the device |

- `dongle.py log` uses 115200 only, follows a dongle through a reboot (started
  before a flash, it catches the boot log) and drops key-event lines unless `--raw`.
- `--logging` keeps the boot log in a 4 KiB CDC ring until the port opens: a
  port opened 70 s after boot returned the whole boot log (hardware
  2026-09-29); ZMK's 1 KiB default cut captures after about 1 KB.
- Per build directory (`$ZMK_WS/ws/build/<image name>/`; canon:
  `~/.cache/zmk-canon/cfgrepo/build/<image name>/`): `build.log` (west's output,
  FLASH/RAM at its end), `zephyr/.config`, `zephyr/zephyr.dts`, `zephyr/zmk.map`
  (what was linked, in which order), `zephyr/zmk.elf`.
- Harmless warnings in every build: `The choice symbol SETTINGS_NVS ... no symbol
  ended up as the choice selection` (the board defconfig selects it, settings are
  off; a shield `=n` does not silence it) and `Deprecated symbol KSCAN is enabled`.

Log lines; the periodic ones come every 60 s, the first a minute after boot:

- Observer (Prospector Dongle): `N status payloads in 60 s; so far N keystrokes,
  N counter restarts (last jump N)`. 275-280 payloads a minute with the passive
  scan (sprite logging image at 3721e16, hardware 2026-09-29; 239-269 with the
  active scan before it), 300 at most (one per `BEACON_PAYLOAD_INTERVAL_MS`). A restart is
  the keyboard rebooting (a reflashed Imprint Dongle jumped 182, 2026-09-29).
- Screen (Prospector Dongle): `screen left L right R (fresh, payload N ms ago, N
  keystrokes)` on every HP bar change and once a minute; `stale` (grey) after 60 s.
- Sprite (sprite builds): `sprite N decoded, N presses for N key steps and N
  frames dropped, N invalidated, N renders of N ms in 60 s, speed 75%, lvgl pool
  N allocated, N max, of N`. Presses × 8 = key steps + frames dropped, up to the
  queue carried across the minute's edge. Renders is what the panel showed (LVGL
  merges invalidations). A render took about 43 ms and a decode 11 ms a frame
  (hardware 2026-09-28); the pool peaked 4.9 KB above the decoder (2026-09-27).
- Broadcaster (Imprint Dongle): `advertising, N updates ok, N failed, N start
  failures, battery L/R, N keystrokes, in 60 s`. Healthy: `advertising`, 0
  failed, 0 start failures (hardware 2026-09-27); its keystrokes and the
  observer's grow alike (50 and 50 in a minute, 2026-09-29).
- Once: `scanning`, `battery left L right R` (on change), `sprite WxH as WxH
  (N.NNx) from a N byte GIF, N bytes of the lvgl pool` (measured),
  `advertising every 200 ms`, `<uart>: 1200 baud touch, rebooting into the bootloader`.

## Invariants

- **Never set a dongle's serial port to 1200 baud unless you mean to flash it.**
  Any program that sets the rate (a serial monitor, `stty`) reboots the
  Prospector Dongle, and canon's Imprint Dongle, into the UF2 bootloader
  ([src/bootloader_on_1200_baud.c](src/bootloader_on_1200_baud.c): why a warm
  reboot after `bootmode_set()`). The option selects `RETENTION_BOOT_MODE` (a
  board without retention fails at Kconfig) but depends on `USB_CDC_ACM`: without
  it canon's `=y` is dropped with only a `was assigned the value 'y'` warning,
  so after a ZMK bump grep `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD=y` in canon's
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
  passively. Never scan with a `BT_LE_SCAN_*` helper: they set `FILTER_DUPLICATE`
  and each address is reported once ([src/status_observer.c](src/status_observer.c)).
- Touch LVGL only on ZMK's display work queue (`LV_USE_OS=0`): the scan callback
  runs on the BT RX queue and only copies under a spinlock; the screen reads the
  state from an `lv_timer` ([src/prospector_screen.c](src/prospector_screen.c)).
- Opt every LVGL widget and font in: ZMK implies `LV_CONF_MINIMAL`, and a widget
  used without its `CONFIG_LV_USE_*` fails at link time, not in Kconfig (the HP
  bar's are set in the shield conf, the sprite's selected by `BEACON_SPRITE`).
- Keep `CONFIG_LV_GIF_CACHE_DECODE_DATA=y` with the sprite (`BEACON_SPRITE`
  selects it): the other gifdec path of this LVGL drops every frame's last LZW
  token ([src/sprite.c](src/sprite.c)).
- Keep the sprite the screen's first child and never render the screen through a
  layer (`opa_layered`, a transform, a blend mode, a bitmap mask): `blit()`
  writes into the display buffer and relies on the screen's fill having landed
  ([src/sprite.c](src/sprite.c)).
- Size the LVGL pool in the shield's
  [Kconfig.defconfig](boards/shields/prospector/Kconfig.defconfig) (88 KiB with a
  sprite, 48 KiB without), never in a `.conf`: a conf line, the consumer's too,
  fixes one size for both. More pool takes a logging sprite build (89.61% RAM)
  over 90%. [CMakeLists.txt](CMakeLists.txt) refuses a GIF whose decoder does not
  fit, [src/prospector_screen.c](src/prospector_screen.c) one larger than the box.
- The panel's SPI runs at 32 MHz on SPIM3 with high drive on its pins (overlay),
  above the ST7789V data sheet's 15 MHz write cycle, as 16 MHz already was: a
  render takes about 43 ms against 63 at 16 MHz (hardware 2026-09-28). If the
  panel shows noise, set `mipi-max-frequency` back to 20 MHz (16 MHz effective).

## Privacy: the sprite

- **The sprite GIF is a personal file, and its name names the subject.** Never
  commit a GIF, an `.inc`, frames, previews or anything made from one, and never
  write the GIF's path or file name, its subject or the sprite name into a
  commit, a PR or a doc here. `.gitignore` has `*.[gG][iI][fF]`
  (`core.ignorecase` is false on this case-sensitive volume).
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
