# CLAUDE.md

Claude Code notes for this repository: what breaks, and the procedures that
work. Usage for people is in [README.md](README.md). Everything committed here
is English (fleet doc-consistency policy); conversation and furrow tasks stay
Japanese.

## What this repository is

- A ZMK module for the **Prospector Dongle**, the companion display of the
  Cyboard Imprint, plus the firmware build for it. Device names follow canon's
  [docs/glossary.md](https://github.com/akira-toriyama/canon/blob/main/docs/glossary.md):
  Cyboard Imprint (the halves) / Imprint Dongle (the split central) /
  Prospector Dongle (this device). Never "XIAO dongle" or "scanner".
- Two roles in one tree ([config/west.yml](config/west.yml) explains): a
  Zephyr module (root `zephyr/module.yml`, `boards/`, `src/`, `Kconfig`) and a
  zmk-config (`config/west.yml` + `build.yaml`) that CI and
  [scripts/build.sh](scripts/build.sh) build.
- Consumer: [akira-toriyama/canon](https://github.com/akira-toriyama/canon)
  pins this repository by commit in its `config/west.yml` for both ends: the
  Prospector Dongle (shield `prospector`, since 2026-09-26) and the Imprint
  Dongle's broadcaster (`CONFIG_BEACON_STATUS_BROADCAST`, canon task t-k8pk),
  which replaced t-ogura/prospector-zmk-module there. Symbol names were chosen
  while both modules shared canon's manifest: Kconfig `BEACON_*` (theirs:
  `PROSPECTOR_*`, `ZMK_STATUS_ADV_*`) and the shield `prospector` (theirs:
  `prospector_scanner`).
- Roadmap = furrow, projects epic e-7n2v: t-5gxp (own BLE observer + battery
  screen + BLE silence, on hardware 2026-09-26, 19 h run) → t-eray (second
  advertising set spike, on hardware 2026-09-27) → t-k8pk (broadcaster, drop
  the t-ogura module from canon) → t-rx4e (GIF sprite) → t-mxb7 (sprite
  updates).

## Fragile points

- **ZMK tracks `main`, never a tag.** canon must build against zmk@main (its
  Cyboard board needs it, canon CLAUDE.md), and this module has to compile on
  the same tree. The weekly schedule in `build.yml` is the early warning.
- **The west topdir is not the module root.** CI: `$RUNNER_TEMP/ws` holds
  `config/` and the west projects, `$GITHUB_WORKSPACE` goes in as
  `-DZMK_EXTRA_MODULES`. Local: `~/.cache/zmk-beacon/{ws,module}`. This is what
  ZMK's `build-user-config.yml` does for a config repo that carries a
  `zephyr/module.yml`. Never `west init` inside the repository.
- **Kconfig merge order** (build log, 2026-09-26): board defconfig
  (`xiao_ble_zmk_defconfig`) → ZMK `app/prj.conf` → the shield's
  `boards/shields/prospector/prospector.conf` → the consumer's
  `config/prospector.conf`. Later wins: the board sets `CONFIG_ZMK_USB=y` and
  the shield conf's `=n` left it unset in `.config`. Shield-level facts (USB
  layout, display memory) live in the shield conf; canon-only facts (e.g.
  `CONFIG_ZMK_RGB_UNDERGLOW=n`, forced on by the Cyboard module) stay in
  canon's `config/prospector.conf`.
- **`CONFIG_USB_DEVICE_PRODUCT="Prospector Dongle"` is a contract** with
  canon's `scripts/flash-prospector.sh`, which finds the device by that exact
  string (VID/PID are ZMK's defaults and the Imprint Dongle shares them). Set
  in `prospector.conf`; change both sides together.
- **Exactly one CDC ACM port.** The xiao_ble board defines
  `board_cdc_acm_uart`; the shield adds no node and points `zephyr,console` at
  the board's. Every okay `zephyr,cdc-acm-uart` node becomes one more USB
  serial function, and two ports broke the flash script (canon #219, hardware
  2026-09-26). `CONFIG_BOARD_SERIAL_BACKEND_CDC_ACM=n` keeps the board's
  console set off, so the port is silent unless `--logging`
  (`CONFIG_ZMK_USB_LOGGING=y`) turns the console on.
- **The status payload is `src/status_payload.h`**, shared by the broadcaster
  and the observer: 26 bytes, the prospector-zmk-module v2.2.3 layout kept
  byte for byte (`FF FF AB CD`, version `0x22`, left half at 5, right at 12,
  layer index at 6, 4-byte layer name at 15). Only those bytes are written and
  read. A layout change is a new version byte (t-xe2q) and both ends move in
  one commit; canon then bumps its pin once.
- **The broadcaster runs only on a split central** (`ZMK_SPLIT_ROLE_CENTRAL`),
  so this repository's own build (`build.yaml`: the prospector shield) never
  compiles it. canon's `imprint_dongle` build is what checks it: after a change
  in `src/status_broadcaster.c`, build canon against the branch before
  merging. It selects `BT_EXT_ADV`; the consumer must set
  `CONFIG_BT_EXT_ADV_MAX_ADV_SET=2` (a `BUILD_ASSERT` in the source fails the
  build otherwise). The set is created from a settings commit handler at
  commit priority 1, after ZMK's first `bt_le_adv_start()`; the source header
  explains the ordering and the own work queue. Hardware 2026-09-27 (t-eray):
  reconnects unchanged in 5 reboots and a half power cycle, 0 advertising
  errors, 255-269 payloads a minute at the Prospector Dongle.
- **The observer owns Bluetooth.** `CONFIG_ZMK_BLE=n` (shield conf) removes
  ZMK's `bt_enable()` callers, its connectable advertisement (a Mac saw it as
  "Prospect", HID + BAS, before 2026-09-26), SMP and settings; the shield conf
  sets `BT=y` and `BT_OBSERVER=y` by hand. The scan is ACTIVE (a keyboard on
  prospector-zmk-module carries the payload in ZMK's scan response; the own
  broadcaster's AD would do with a passive scan, a follow-up under t-xe2q) and
  without `BT_LE_SCAN_OPT_FILTER_DUPLICATE`, which every `BT_LE_SCAN_*` helper
  sets (the controller then reports each address once and the numbers freeze).
  Measured 2026-09-26: 475-507 payloads a minute; a 70 s CoreBluetooth scan
  from the Mac saw no advertisement from the device. The build warns that the
  `SETTINGS_NVS` choice has no selection: the board defconfig selects it and
  settings are off. Harmless, and a `=n` in the shield conf does not silence it.
- **LVGL only from the display work queue.** LVGL is not thread-safe
  (`LV_USE_OS=0`); the scan callback runs on the BT RX work queue. The screen
  reads `beacon_status_get()` from an `lv_timer` created in
  `zmk_display_status_screen()`, which runs on ZMK's display queue.
- **A logging build drops its boot log unless the port is opened within a
  second or so.** ZMK sets the CDC ACM ring buffer to 1024 bytes and the boot
  banner fills it, so later lines are lost until the host reads. For a boot
  log, build once by hand with `-DCONFIG_USB_CDC_ACM_RINGBUF_SIZE=8192` next to
  `-DCONFIG_ZMK_USB_LOGGING=y`. Read the port at any rate but 1200 (see
  below). Logging builds print one observer line (payloads per minute) and one
  screen line (what it shows) every minute.
- **1200 baud bootloader entry** (`BEACON_BOOTLOADER_ON_1200_BAUD`, default y
  under the shield): `bootmode_set()` + warm reboot, not `sys_reboot(0x57)`;
  the reasons are in [src/bootloader_on_1200_baud.c](src/bootloader_on_1200_baud.c).
  Kconfig drops a `=y` whose dependency broke with only a warning
  (`was assigned the value 'y' but got the value 'n'`), so after a ZMK bump
  check `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD=y` in
  `~/.cache/zmk-beacon/ws/build/prospector/zephyr/.config` (Zephyr
  `scripts/kconfig/kconfig.py:123-125`, read 2026-09-26).
- **Both dongles mount as `XIAO-SENSE`.** Never put the Imprint Dongle and the
  Prospector Dongle in the bootloader at the same time; canon's
  `flash-watch.sh` / `flash-reset.sh` copy `imprint_dongle.uf2` onto any
  `XIAO-SENSE` mount.
- **`LV_CONF_MINIMAL=y`** is implied by ZMK's display Kconfig, so every LVGL
  widget and font is opt-in in `prospector.conf` (`CONFIG_LV_USE_*`,
  `CONFIG_LV_FONT_*`). A widget used without its symbol fails at link time,
  not in Kconfig.
- **Memory** (observer + battery screen, 2026-09-26, zmk 9ebbeff0): FLASH
  293,516 B of 788 KB (36.38%), RAM 177,948 B of 256 KB (67.88%); `--logging`
  39.62% / 71.64%. The skeleton had 363,948 B / 202,740 B with ZMK's BLE
  stack and Montserrat 16 + 28; Montserrat 48, now the only font linked, is
  about 97 KB of flash (from its source tables). RAM is mostly LVGL: VDB
  30% × 2 and the 48 KiB pool. A GIF decoder will draw on that pool (t-rx4e,
  t-mxb7).
- **Fleet-managed files, do not edit here**:
  `.github/workflows/{actionlint,commit-lint,repo-policy,taplo,task-status,version-preview,zizmor}.yml`,
  `.github/zizmor.yml`, `.github/dependabot.yml`, `docs/commit-convention.md`.
  fleet-sync in akira-toriyama/.github overwrites them from its `fleet/`
  directory. `glyph.toml` was written by `glyph init --gemoji`: edit, never
  regenerate.

## Build

- `./scripts/build.sh [shield] [--logging] [--update]` — Docker
  (`zmkfirmware/zmk-build-arm:stable`), workspace `~/.cache/zmk-beacon`, output
  `firmware/` (gitignored). `--update` refreshes zmk@main; without it the
  cached checkout is reused.
- CI: `build.yml` → `zmk-build.yml` (local reusable; the file says why not
  ZMK's). `release.yml`: glyph computes the next version and notes on every
  push to `main` and upserts one rolling draft release with `prospector.uf2`
  attached; publishing by hand creates the tag. `dry_run=true` on
  `workflow_dispatch` previews without writing a draft.
- The status check name `build / Build (xiao_ble/nrf52840/zmk, prospector)` is
  what a main ruleset references; keep the job names.

## Commits and pull requests

- glyph-driven: `<:code:>[(scope)]<sigil> <subject>`, English. Run
  `glyph lint --range origin/main..HEAD` before pushing, `glyph hook install`
  once per clone. Codes: `glyph emoji`. Convention:
  [CONTRIBUTING.md](https://github.com/akira-toriyama/.github/blob/main/CONTRIBUTING.md).
- PR bodies end with
  `SetStatus-task: https://github.com/akira-toriyama/projects/blob/main/.furrow/bodies/<id>.md <lane>`.
- Public repository on free Actions minutes: PR gates are deterministic checks
  only; never add a workflow that calls a paid API.
- Hardware claims in docs and task bodies carry a date and were checked twice
  on the Prospector Dongle.
