# CLAUDE.md

Claude Code notes: what breaks, where the reasoning lives (source headers), how to
build, flash and read the devices. Usage: [README.md](README.md). English only.

## What this repository is

- A ZMK module for both ends of the status advertisement: the **Prospector
  Dongle** (shield `prospector`: a BLE observer showing the keyboard's battery as
  an HP bar under a sprite, which a Mac swaps over BLE into the sprite room,
  `CONFIG_BEACON_SPRITE_RECEIVE`) and the broadcaster for a keyboard's split
  central (`CONFIG_BEACON_STATUS_BROADCAST`; canon's **Imprint Dongle** is the
  one on hardware). Device names follow canon's
  [docs/glossary.md](https://github.com/akira-toriyama/canon/blob/main/docs/glossary.md):
  Cyboard Imprint (the halves), Imprint Dongle (the split central), Prospector
  Dongle; never "XIAO dongle" or "scanner".
- Two roles in one tree ([config/west.yml](config/west.yml)): a Zephyr module
  (`zephyr/module.yml`, `boards/`, `src/`, `Kconfig`, `CMakeLists.txt`) and the
  zmk-config (`config/`, `build.yaml`) that CI and [scripts/build.sh](scripts/build.sh)
  build: the Prospector Dongle's image, and the broadcaster's check build, ZMK's
  Corne on the nice!nano with its left half the central and README's three
  lines in `config/corne_left.conf` (canon t-4hw7, 2026-11-08), so that CI
  compiles the broadcaster on a stock split central against unpatched ZMK
  `main` and a change that breaks it there fails here first. That image is
  built, never attached to a release (release.yml's asset guard names it) and
  never run on hardware: canon's Imprint Dongle stays the one broadcaster on
  hardware, built with canon's ZMK patches and two peripherals.
- Three repositories pin this one by commit SHA. [canon](https://github.com/akira-toriyama/canon),
  the one keyboard on hardware, for both dongles (its
  `config/imprint_dongle.conf`: `CONFIG_BEACON_STATUS_BROADCAST=y`,
  `CONFIG_BT_EXT_ADV_MAX_ADV_SET=2`,
  `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`,
  `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD=y`); a merge here reaches the dongles
  with canon's pin bump. zmk-config-lantern-check, README's recipe under
  ZMK's own workflow (Build). prospector-sprite-template, which calls the
  reusable `prospector-sprite.yml` (Build).
- README is written for a consumer, in the order a consumer reads it (the
  least, then the options, then what it does, then the sprite), as
  zmk-lantern's: no canon task ids, dates or history there (canon t-4hw7,
  2026-11-08); they live here. canon appears there as an example (its scripts,
  its dongle's conf) or as the one keyboard the module has run on. The
  hardware record, the last item of Invariants, is where a hardware fact and
  its date go. Two passages stay as they were until canon t-t63a replaces
  them: the colour paragraphs of "The sprite pack" and "The sprite sender",
  which name canon's `dongle.py palette` and t-0967. The Pokémon section is
  canon t-tw7z's, not yet written: README keeps its slot between "Using it in
  a ZMK config" and "What it does", carries no rights lines of its own, and
  names the keyboard step "The least, the keyboard" for it to point at.
- `tools/`: `sprite_pack.py` (the sprite pack, [docs/sprite-pack.md](docs/sprite-pack.md):
  a GIF as its distinct pictures, cut to the box they ink, for the player and
  the over-the-air swap) on
  `gifpics.py` (GIF composition as browsers do it), standard library only so
  that macOS's `/usr/bin/python3` (3.9) and the ZMK build image both run them;
  `sprite_colours.py` (the colours a sprite's GIFs, its front and its back,
  show most, for the key LEDs' palette of canon's keyboard: its docstring
  holds the rule and the numbers measured on the user's collection) on the
  same two;
  `name_dictionary.py` (its docstring) prints one sprite's name, by National
  Pokédex number, from PokeAPI's species names CSV, for the sender at each run
  and for `sprite_fetch.py`: the one rule of which name a sprite gets (`--lang
  ja`, the sender's: katakana when the bar shows it, else English; `en`: English
  alone; an English name folded first, the gender signs and the typographic
  apostrophe, nothing else); its output names a species, so it goes down a
  pipe only; `sprite_fetch.py` (its docstring) fetches one GIF and the CSV from
  their repositories at fixed commits and writes the build's Kconfig fragment,
  for the reusable workflow `prospector-sprite.yml` (below);
  `test_sprite_pack.py`, `test_sprite_colours.py`, `test_name_dictionary.py` and
  `test_sprite_fetch.py` write their own GIFs and CSV, the last serving them
  from an HTTP server of its own (`python3 -m unittest discover
  tools`, CI `tools-test.yml`); `pkmn_font.py` (its docstring) renders the
  name row's katakana font, `src/font_pkmn.{c,h}`, from the pkmn TTF, which
  is not in the repository (clone nue-of-k/pkmn): run it again rather than
  edit them, and its `preview` prints glyphs as pixels. The 1,298-GIF checks against Pillow and the
  round trips are local only: the GIFs are personal files. `sprite-sender/`
  (Swift, no dependencies; README "The sprite sender"): `SpriteSenderCore` is
  pure (config, which refuses a config naming the retired `order` with any
  value but null, SpriteSender then exiting 2, and ignores any other key it
  does not know, rotation, the name's last resort,
  report v1, page plan; `swift test` on
  CI's macOS runner, `tools-test.yml`, as a Mac with only CommandLineTools
  has no XCTest) and `SpriteSender` does files, the Python tools as child
  processes (`Tools.swift`: the pack converter, the colours, the name) and
  CoreBluetooth (Swift 5 language mode: CoreBluetooth's delegate types are
  not Sendable). `package.sh` makes `SpriteSender.app` with the Python
  tools in its Resources, signed with the `chord-dev` identity (canon
  t-3cf6: the Bluetooth permission is keyed to identity and bundle id).
  Nothing it logs names a GIF or the sprite, nor the GIF's number, which
  names the subject, nor the name's
  length, which a rare one does against the public list of names: of the
  name it logs the alphabet, which still says whether the katakana name
  fit (with a complete CSV, `ASCII` is a name of
  six katakana or more). That and the pack's size it logs can point to
  the GIF, so its lines stay out of anything public (`Log.swift`).
  **A test never runs `daily` without `--dry-run`, nor `send`**: any build on
  this Mac, a bare `swift build` in a scratch copy too, reaches the real
  Prospector Dongle over BLE (TCC attributes it to the terminal or editor that
  started it), and the sprite room keeps no copy of what it overwrites
  (2026-11-05, canon e-ehzy: a review's scripts overwrote it four times, one
  assuming its runs stopped before the send, one with a stub patch that failed
  without stopping the script). For the path past `--dry-run`, stub `Sender` in
  a copy and check that the stub took. `report` only reads.
- Tasks: on canon's board for now (`furrow list -r canon`, epic e-3sm7; the
  2026-09-29 refactor: epic e-vgt5).

## Build

- `./scripts/build.sh [shield | board:shield] [--logging] [--sprite <gif>
  [--sprite-name <text>]] [--kconfig CONFIG_X=V]... [--tag <name>] [--update]`
  (flags: its header). Docker, workspace `$ZMK_WS` (default `~/.cache/zmk-beacon`),
  images `firmware/<shield>[-sprite][-logging][-kconfig][-<tag>].uf2`, then a
  summary (sha256 prefix, FLASH, RAM, revisions). `--update` moves zmk@main.
  With no target it builds both build.yaml images, `prospector.uf2` and
  `corne_left.uf2` (`--kconfig`, `--tag` and `--update` keep both);
  `--logging` and `--sprite` build the `prospector` shield only (without a
  target the run takes build.yaml's `prospector`; a named other target stops
  it with exit 2): the nice!nano has no CDC ACM node for the log
  (`CONFIG_ZMK_USB_LOGGING`'s select of `USB_CDC_ACM` would abort Kconfig;
  read, not built), and the sprite options are the `prospector` shield's
  (`BEACON_SPRITE_GIF` depends on it).
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
- Sizes (local Docker builds of every build.yaml target, ZMK main 4dd95d78
  unpatched, 2026-11-08): `prospector.uf2` FLASH 283,992 B of the 356 KB code
  partition (the sprite room cut from it), RAM 217,608 B of 256 KB;
  `corne_left.uf2` FLASH 267,504 B of 792 KB, RAM 65,280 B. An embedded pack
  has what the code leaves: about 80 KB with the receiver, about 150 KB
  without (the code took 213,928 B without it, zmk main of 2026-11). Before
  the room, on the whole 788 KB (zmk 5b51501f, 2026-10-06): plain 25.30% /
  67.98%, a 89x91 sprite (a 39 KB pack from an 84 KB GIF) 30.72% / 75.65%, a
  153x94 one with `--logging` 32.48% / 81.70%; the gifdec player's 84 KB
  sprite took 36.26% / 83.60% and 39.36% / 89.71% with logging (2026-10-04).
- CI: `build.yml` → the local reusable `zmk-build.yml` (it says why not ZMK's),
  every build.yaml target, the broadcaster's check among them;
  `tools-test.yml` runs `tools/`' unit tests (Python on Ubuntu, the sprite
  sender's `swift build` and `swift test` on macOS).
  `release.yml` keeps a rolling draft release of `prospector.uf2`, tagged when
  published by hand; its Collect step is an allowlist that lets
  `corne_left.uf2` be built and never attached, and a third build.yaml target
  fails it until named there. Its draft job runs after a partial build
  (`!cancelled()`, with a version job that succeeded), so a red corne_left no
  longer leaves the draft's notes over an older `prospector.uf2`, and a
  missing `prospector.uf2` still stops it (the `cp` under `set -e`).
  zmk-build.yml fails a leg when a `CONFIG_` line of `config/<shield>.conf` is
  not in the build's `.config` verbatim (an `=n` line when `.config` has a
  `CONFIG_X=` line: Kconfig writes `# CONFIG_X is not set`, or nothing for a
  symbol whose dependencies are unmet): Zephyr only warns about a value that
  does not take, so a broadcaster whose dependency stopped being met would leave
  the check green and checking nothing (canon t-4hw7, 2026-11-08). A ruleset requires
  `build / Build (xiao_ble/nrf52840/zmk, prospector)`; the check build's job is
  `build / Build (nice_nano/nrf52840/zmk, corne_left)`, required only once the
  ruleset names it (the ruleset edit is the user's).
- The consumer's shape under ZMK's own CI is
  [zmk-config-lantern-check](https://github.com/akira-toriyama/zmk-config-lantern-check),
  zmk-lantern's check repository, a fork of ZMK's `unified-zmk-config-template`
  (zmk-lantern's CLAUDE.md says why a fork), which carries this module too
  since canon t-4hw7 (2026-11-08): README's manifest with both modules pinned
  by commit
  (zmk-beacon at e69d831), the Corne on the nice!nano, its left half the
  central with README's three lines and `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD=y`
  on the CDC ACM port its overlay adds for zmk-lantern, and the template's
  `build.yml` pointed at `build-user-config.yml@main`. The run of its commit
  b52ce3c (https://github.com/akira-toriyama/zmk-config-lantern-check/actions/runs/42452649801,
  green in about three minutes, nothing patched): corne_left's Kconfig step
  lists `CONFIG_BEACON_STATUS_BROADCAST=y`, `CONFIG_BT_EXT_ADV=y`,
  `CONFIG_BT_EXT_ADV_MAX_ADV_SET=2`,
  `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`,
  `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD=y`, `CONFIG_RETENTION_BOOT_MODE=y` and
  `CONFIG_USB_CDC_ACM=y`, with `status_broadcaster.c` and
  `bootloader_on_1200_baud.c` compiled; corne_right, a peripheral, has no
  broadcaster line. So the nice!nano central half takes the 1200 baud entry
  once it has a port (built, not tried on hardware). Its pin is bumped by hand
  when README's least changes, never on each merge here (its build is the
  proof that the recipe builds, not a gate). README's zmk#3522 caveat says the
  fix zmk#3556 is open: true on 2026-11-08; when it merges, the caveat goes
  from README, the `BEACON_STATUS_BROADCAST` help, the header of
  `src/status_broadcaster.c` and the invariant on building canon's
  `imprint_dongle` below, and from zmk-lantern's README, and canon's patch
  with it (canon's `patches/zmk/README.md`).
- `prospector-sprite.yml` is a reusable for other people's runs (README "A
  sprite image from GitHub Actions"; the template repository
  `akira-toriyama/prospector-sprite-template` calls it by full SHA, and the
  hub's fleet-sync excludes that repository): it checks this repository out at
  `job.workflow_repository` / `job.workflow_sha` (waived in
  `.github/actionlint.yaml`), builds the shield from this `config/west.yml`
  with a `prospector.conf` of `CONFIG_BEACON_SPRITE_RECEIVE=n` and
  `CONFIG_BEACON_SCREEN_OFF_AFTER_S=0`, and embeds the GIF `sprite_fetch.py`
  fetched. **Never call it from this repository's CI**: its artifact holds a
  picture, and this repository's runs are public. Its first live run: canon
  t-hkc6 (2026-11-08). The template's README sends its readers to sections
  of this README by name (the flashing step, the keyboard's config, "The
  sprite"): a heading change here is a template change in the same sitting
  (canon t-4hw7 renamed "Flashing" and "Using the module in a ZMK config";
  the template follows after the merge).

## Debugging and device operations

The device tools are canon's `scripts/` (their headers document every flag). A
dongle's serial port is silent unless it runs a `--logging` image.

| Goal | Command | Healthy result |
| --- | --- | --- |
| Prospector image from this tree | `./scripts/build.sh --logging` (the `prospector` shield alone: `--logging` and `--sprite` never take corne_left) | `images:` line with FLASH/RAM, `revisions: ... zmk-beacon <describe>` |
| The broadcaster's check build from this tree: a stock split central, unpatched ZMK main, never flashed | `./scripts/build.sh corne_left` | `firmware/corne_left.uf2`; `build/corne_left/zephyr/.config` has `CONFIG_BEACON_STATUS_BROADCAST=y`, `CONFIG_BT_EXT_ADV=y`, `CONFIG_BT_EXT_ADV_MAX_ADV_SET=2`, `CONFIG_BT_CTLR_ADV_SET=2`, `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`, `CONFIG_ZMK_BATTERY_REPORTING=y`, `CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=1`; `status_broadcaster.c.obj` linked, first in `.event_subscription` of its zmk.map; no warning beyond `Deprecated symbol KSCAN is enabled` (below; 2026-11-08) |
| Both dongles' images against canon's exact tree (ZMK main at canon's cached commit, its patches, two peripherals): the broadcaster as it runs on hardware | canon: `./scripts/build-zmk.sh prospector imprint_dongle --module <this checkout> --logging` (`--beacon` until canon #267) | `revisions:` has `zmk-beacon <dir> @ <describe> (--module)`; the images carry `-beacon` |
| Flash either dongle, no double-tap | canon: `./scripts/flash-dongle.sh <image.uf2>` (any directory; named `<device>.uf2` or `<device>-*.uf2`; `--dry-run` checks only) | last line `DONE <device> ... sha256=<the build summary's 12 hex>`, exit 0 |
| Find the dongles | canon: `python3 scripts/dongle.py list` | each dongle with its `/dev/cu.*` port and `port free` |
| Read the logs | canon: `python3 scripts/dongle.py log prospector imprint_dongle --seconds 130 [--grep RE] [--out FILE]` | the lines below, each after a timestamp and the device |
| See the Prospector's screen | canon: `python3 scripts/dongle.py shot --out <scratch>/shot.png`, then Read the PNG | stdout the PNG's path, stderr `280x240 in N bands, CRC-32 ok` |

- `dongle.py log` uses 115200 only, follows a dongle through a reboot (started
  before a flash, it catches the boot log) and drops key-event lines unless `--raw`.
  `shot` sets 2400, then 115200 (the dump: [src/screen_dump.c](src/screen_dump.c)),
  refuses a port another process holds (stop the log reader) and a path in any
  git work tree: a picture shows the sprite playing, the sprite room's included.
- `--logging` keeps the boot log in a 4 KiB CDC ring until the port opens (70 s
  after boot it still held the whole boot log; ZMK's 1 KiB cut it, hardware
  2026-09-29). A late opener gets the oldest 4 KiB first: check the uptime stamps.
- Per build directory (`$ZMK_WS/ws/build/<image name>/`; canon:
  `~/.cache/zmk-canon/cfgrepo/build/<image name>/`): `build.log` (west's output,
  FLASH/RAM at its end), `zephyr/.config`, `zephyr/zephyr.dts`, `zephyr/zmk.map`
  (what was linked, in which order), `zephyr/zmk.elf`.
- Harmless warnings: `Deprecated symbol KSCAN is enabled` in every build, and in
  the `prospector`'s also `The choice symbol SETTINGS_NVS ... no symbol ended up
  as the choice selection` (the board defconfig selects it, settings are off
  there; a shield `=n` does not silence it; corne_left has settings on and no
  such line, 2026-11-08).

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
  `./scripts/build.sh prospector --kconfig CONFIG_BEACON_SCREEN_OFF_AFTER_S=20`
  (off at 00:00:21.0 and on
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
  payload or their Kconfig before merging it (runbook row 3): CI's check build
  compiles `src/status_broadcaster.c` on a stock central against unpatched ZMK
  `main` (since canon t-4hw7; before it CI never compiled the file), but
  canon's dongle is the one on hardware, with canon's patches and two
  peripherals. Without `CONFIG_BT_EXT_ADV_MAX_ADV_SET=2` in the consumer a
  `BUILD_ASSERT` fails; without
  `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING` nothing fails and the
  battery bytes stay 0, on purpose (the file's header: the key presses and the
  modifiers work without it, and a central that takes Bluetooth hosts may
  leave it off for zmk#3522), where zmk-lantern asserts it.
- Keep the broadcaster's listener ahead of ZMK's: it counts a press before a
  hold-tap or a combo captures it because ZMK calls listeners in link order and
  the module's objects link first (re-raised presses:
  [src/status_broadcaster.c](src/status_broadcaster.c)). After a ZMK bump, in a
  broadcaster's build directory (canon's `imprint_dongle`, or `corne_left` here),
  `grep -A1 '^ \.event_subscription$' zephyr/zmk.map | grep -o '([a-z_]*\.c\.obj)'
  | head -1` prints `(status_broadcaster.c.obj)` (2026-09-29; corne_left 2026-11-08).
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
  sprite before the room shrinks; every pack of the user's collection fits the
  room, the largest 319 KB, the floor for any shrink. The player reads the room
  in place
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
  font by a byte of 0x80 or above, and canon's `sprite-name.py` and the
  sender's `SpriteName` must stay within it. `SpriteName.katakanaRanges`
  copies `KATAKANA_RANGES` (Swift cannot read the Python): change the two
  together, and `tools/pkmn_font.py` renders whatever the ranges say. Which
  name a GIF gets is `tools/name_dictionary.py`'s rule (canon t-rq9h, the
  user's rule: katakana when it can be shown, else as before; here the English
  name, whole, keyed by the GIF's number, folded first by its `fold_english`,
  the three rules of the user's former table of ASCII names, canon t-c03v);
  the sender and `sprite_fetch.py` only fall back to the number. canon's
  `assets/sprite-name.py` copies the rule and the fold for a local build: it
  runs before the pin's checkout and on a machine with no sender, so change
  the two together.
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
  panel shows noise, set `mipi-max-frequency` back to 20 MHz (16 MHz effective;
  README offers a consumer a `config/prospector.overlay` for it, untried).
- Hardware claims here and in task bodies carry a date; README carries none
  (it is the consumer's). Repeat a check only when the first result failed or
  was ambiguous. **The hardware record**: the Prospector Dongle is the beekeeb
  pre-soldered unit (no APDS9960, the touch panel unwired), the only unit the
  shield has run on; the observer has shown the keyboard's battery on it since
  2026-09-26, and the broadcaster has sent the advertisement from canon's
  Imprint Dongle, a USB-powered split central with two peripherals and no
  Bluetooth host, since 2026-09-27, where it replaced
  t-ogura/prospector-zmk-module's; no other central has run it. The embedded
  sprite stepping with the key presses: since 2026-09-29. A sprite over BLE
  into the sprite room: since 2026-10-12 (the transfer's figures: Debugging,
  the receiver's lines); a pack showed about a tenth of a second a KB after
  BEGIN (2026-10-13); the daily swap runs from a LaunchAgent in the user's
  dotfiles repository (not canon) since 2026-10-13, and since 2026-10-21 it
  hands the sender's `colours` to the keyboard's palette after each swap that
  succeeded (canon's CLAUDE.md). The Actions
  route's first live run took about four minutes (2026-11-08, canon t-hkc6).
  Not on hardware: a central half (the check build and the check repository
  build it), a central next to a Bluetooth host, the broadcaster on battery
  (its drain unmeasured), the nice!nano's 1200 baud entry (the check
  repository builds it), the panel-clock fallback, any Prospector but
  beekeeb's, and an image without the fetching line (README's `--/100` for it
  is read from the code).

## Privacy: the sprite

- **The sprite GIF is a personal file, and its name names the subject.** Never
  commit a GIF, a sprite pack, an `.inc`, frames, previews or anything made from
  one, and never write the user's GIF's path or file name, its number, its
  subject or the sprite name into a commit, a PR, a doc, a test or a workflow
  here (the user's collection is the user's; this repository is public). The
  public sources the Actions route fetches from by default (PokeAPI's two
  repositories, pinned in `tools/sprite_fetch.py`) are named in the workflow
  and the README (canon t-4555, 2026-11-08): what stays out is which of their
  files the user shows. `.gitignore` has
  `*.[gG][iI][fF]` and `*.spk` (`core.ignorecase` is false on this
  case-sensitive volume). Tests use GIFs they write themselves. The picture ban
  enforces it (canon t-gm39, 2026-11-08): the hub's `picture-ban` reusable
  (caller `.github/workflows/picture-ban.yml`) fails a PR on a tracked picture by
  name or first bytes (GIF, PNG, JPEG, WebP, BMP), a sprite pack (`.spk`,
  `beacon_sprite_pack.inc`), a path naming the sprites' source or a `sprites/`
  directory, or a binary of 16 KiB or more (the repository tracks no binary),
  and the same script is every clone's pre-commit hook: once per clone,
  `bash ../.github/scripts/picture-ban-check.sh --install-hook` from the hub's
  clone beside this one (the hook fails a commit when that clone has moved, and
  says how to reinstall), so a picture never reaches history, where a red PR
  would still leave it.
- `CONFIG_BEACON_SPRITE_GIF` takes an absolute path. `build.sh --sprite` copies
  the GIF to `$ZMK_WS/sprite/sprite.gif`, and the build embeds it as
  `build/<image name>/modules/zmk-beacon/beacon_sprite_pack.inc` (build tree only).
- The name (`CONFIG_BEACON_SPRITE_NAME`, `--sprite-name`) goes with the GIF's
  path into the fragment `$ZMK_WS/sprite/sprite.conf`, never into `-DCONFIG_...`:
  west prints the whole cmake command line when the configure step fails
  (reproduced 2026-09-29). Of the sprite, the script prints only the GIF's size
  and whether the name is ASCII or katakana, never its length, and CMake its
  `Sprite:` line of sizes and frames; `--kconfig` refuses
  `CONFIG_BEACON_SPRITE_*`, and no message echoes a path or a value. `BEACON_SPRITE_NAME` has no Kconfig dependency, so kconfig.py
  never prints it (its help says why).
- CI and `release.yml` build without a sprite or a name, so no release carries
  either. canon builds from a GIF off its repository and derives the name from
  the sender's name dictionary or the file name (`assets/sprite-name.py` there).
- The katakana font's glyphs are whole blocks (`sprite_pack.KATAKANA_RANGES`),
  never a subset cut to the names in use: a font made of a name's characters
  would tell the subject. Tests and docs use made-up katakana.

## Commits, pull requests, fleet files

- glyph: `<:code:>[(scope)]<sigil> <subject>`, English
  ([CONTRIBUTING.md](https://github.com/akira-toriyama/.github/blob/main/CONTRIBUTING.md),
  codes: `glyph emoji`); `glyph lint --range origin/main..HEAD` before pushing,
  `glyph hook install` and the picture ban's hook (Privacy) once per clone. PR
  bodies end with the `SetStatus-task:` line.
- Public repository on free Actions minutes: PR gates are deterministic checks
  only (`picture-ban` among them, Privacy); never add a workflow that calls a
  paid API.
- LICENSE is the bare MIT text under the three copyright lines, no prose
  between them and the grant: the "Portions derived from..." paragraph made
  GitHub's licensee read NOASSERTION (canon t-4hw7, 2026-11-08: the old file
  71 % MIT, the new 100 %); which parts derive from where is README's Credits.
- Fleet-managed, never edited here (fleet-sync in akira-toriyama/.github
  overwrites them from its `fleet/`):
  `.github/workflows/{actionlint,commit-lint,repo-policy,taplo,task-status,version-preview,zizmor}.yml`,
  `.github/zizmor.yml`, `.github/dependabot.yml`, `docs/commit-convention.md`.
  `glyph.toml` came from `glyph init --gemoji`: edit it, never regenerate it.
