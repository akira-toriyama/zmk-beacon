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
  layout, display buffers) live in the shield conf, the LVGL pool in the
  shield's `Kconfig.defconfig` (below); canon-only facts (e.g.
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
  layer index at 6, 4-byte layer name at 15, WPM at 24). Only those bytes are
  written and read. WPM (added 2026-09-27 for the sprite) is a v2.2.3 field,
  so the version byte stayed `0x22`; `BEACON_STATUS_BROADCAST` selects
  `ZMK_WPM` for it. A layout change is a new version byte (t-xe2q) and both
  ends move in one commit; canon then bumps its pin once.
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
  `zmk_display_status_screen()`, which runs on ZMK's display queue; the
  sprite's 10 ms timer and `beacon_sprite_set_speed()` (called from that
  refresh timer) run there too.
- **A logging build drops its boot log unless the port is opened within a
  second or so.** ZMK sets the CDC ACM ring buffer to 1024 bytes and the boot
  banner fills it, so later lines are lost until the host reads. For a boot
  log, build once by hand with `-DCONFIG_USB_CDC_ACM_RINGBUF_SIZE=8192` next to
  `-DCONFIG_ZMK_USB_LOGGING=y`. Read the port at any rate but 1200 (see
  below). Logging builds print one observer line (payloads per minute), one
  screen line (what it shows) and, with a sprite, one sprite line (frames
  decoded, invalidations, screen renders and their average time, speed, LVGL
  pool allocated and peak) every minute. "Invalidated" is not "shown": LVGL
  merges invalidations between two renders, so renders is the shown count.
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
  widget and font is opt-in: in `prospector.conf` (`CONFIG_LV_USE_*`,
  `CONFIG_LV_FONT_*`), or selected by a `BEACON_*` symbol (`BEACON_SPRITE`
  selects the image and GIF widgets). A widget used without its symbol fails
  at link time, not in Kconfig.
- **Sprite images are local-only.** The GIF is a personal file:
  `CONFIG_BEACON_SPRITE_GIF` names an absolute path, `build.sh --sprite`
  copies it to `~/.cache/zmk-beacon/sprite/sprite.gif`, and the build embeds
  it as `build/<target>/modules/zmk-beacon/beacon_sprite_gif.inc` (build tree
  only, `generate_inc_file_for_target`). `.gitignore` has `*.[gG][iI][fF]`
  (`core.ignorecase` is false on this case-sensitive volume); never
  commit the GIF, an `.inc`, frames or previews, and never write its path or
  its subject into this repository. CI and `release.yml` build without one,
  so no release carries a sprite image.
- **`CONFIG_LV_GIF_CACHE_DECODE_DATA=y` is a correctness requirement**
  (`BEACON_SPRITE` selects it): in this LVGL (9.3.0-dev, zmk 9ebbeff0) the
  `=n` `read_image_data()` bound check is `frm_off + str_len >= frm_size`
  (upstream corrected it to `>` in 660b41df9), so every frame's last LZW token
  fails, the stream desynchronizes and the code table leaks. The `=y` path is
  a separate implementation: host-checked against Pillow 12 on 2026-09-27
  with the real `gifdec.c` from the build cache, three full passes of the
  test GIF pixel-identical, 0 stray pixels. The harness lived in the session
  scratchpad, not here.
- **Two canvas fixes live in `src/sprite.c`, not in gifdec**: `gif_open()`
  fills the canvas with the background colour at alpha 0xFF, which stays
  visible wherever no frame paints (an opaque band of 6 rows with the test
  GIF), so the canvas is cleared to transparent after open; and at the
  trailer gifdec seeks back to `anim_start` without clearing, so the player
  clears the canvas when the read pointer moves backwards (otherwise stray
  pixels under the first frames of every pass, host check 2026-09-27).
  `loop_count` is 1 for the first decode (a GIF without frames then returns 0
  instead of spinning in gd_get_frame() forever) and 0 afterwards (loop
  forever; gifdec would stop after one pass of a GIF without a NETSCAPE
  block). The stock `lv_gif` has none of this and invalidates every frame;
  about half the consecutive canvases of the test GIF were identical, which
  the player's FNV-1a hash skips.
- **The stretched `lv_image` must not report an extra draw area**:
  `LV_IMAGE_ALIGN_STRETCH` scales around pivot (0,0), and lv_image's own
  `LV_EVENT_REFR_EXT_DRAW_SIZE` handler transforms the already stretched size
  again, claiming about one object size on every side. `lv_obj_invalidate()`
  and `lv_obj_invalidate_area()` both widen to that area, so every changed
  frame redrew the whole panel: hardware 2026-09-27, about 67,000 px and
  152 ms a frame (18 ms per 16,800-px flush, SPIM at 16 MHz: 20 MHz in the
  overlay is not an nRF frequency), 5 renders a second. `src/sprite.c` sets
  the claim back to 0 in a callback that runs after the class handler. After
  that fix, same day: 106 ms a render of the 2x sprite box (about 35 ms of it
  flush, the rest LVGL's software transform), 209 renders in 30 s of play at
  100 %, the observer unchanged at 262-273 payloads a minute. The player keeps
  the tempo by merging frames when drawing falls behind.
- **The WPM path works end to end** (hardware 2026-09-27): with the
  broadcaster's image on the Imprint Dongle, typing moved the sprite's logged
  speed to 148 % (WPM 24). That build froze the sprite 30 s after the last
  typed key; since 2026-09-28 it never stops and falls back to 100 % instead
  (user's choice).
- **The LVGL pool is sized in `Kconfig.defconfig`, not in `prospector.conf`**:
  `LV_Z_MEM_POOL_SIZE` defaults to 90112 (88 KiB) with `BEACON_SPRITE` and to
  49152 without; a `.conf` line, the consumer's included, would fix it for
  both. The sprite's one allocation is `sizeof(gd_GIF)` + 5·w·h + 16 KiB
  (about 58.5 KB for a 90x90 px GIF, exact number in the sprite's boot log
  line); `CMakeLists.txt` refuses a GIF whose allocation plus an 8 KiB reserve
  for the screen exceeds the pool. Hardware 2026-09-27: the pool peaked 4.9 KB
  above the decoder's allocation. 96 KiB would put a logging sprite build over
  90% RAM.
- **Memory** (2026-09-27, zmk 9ebbeff0, from the build logs): plain
  FLASH 293,516 B of 788 KB (36.38%), RAM 177,948 B of 256 KB (67.88%),
  unchanged by the sprite change; `--logging` 39.62% / 71.64%
  (2026-09-26). A sprite build adds the GIF's own size plus about 7 KB of
  code to FLASH and 40 KiB of pool to RAM: with an 80 KB GIF about 48% /
  84%, and 51% / 87% with logging. The skeleton had 363,948 B / 202,740 B with ZMK's BLE stack and
  Montserrat 16 + 28; Montserrat 48, the only font linked, is about 97 KB of
  flash (from its source tables). RAM is mostly LVGL: VDB 30% × 2 and the
  pool.
- **Fleet-managed files, do not edit here**:
  `.github/workflows/{actionlint,commit-lint,repo-policy,taplo,task-status,version-preview,zizmor}.yml`,
  `.github/zizmor.yml`, `.github/dependabot.yml`, `docs/commit-convention.md`.
  fleet-sync in akira-toriyama/.github overwrites them from its `fleet/`
  directory. `glyph.toml` was written by `glyph init --gemoji`: edit, never
  regenerate.

## Build

- `./scripts/build.sh [shield] [--logging] [--sprite <gif>] [--update]` —
  Docker (`zmkfirmware/zmk-build-arm:stable`), workspace `~/.cache/zmk-beacon`,
  output `firmware/<shield>[-sprite][-logging].uf2` (gitignored). `--update`
  refreshes zmk@main; without it the cached checkout is reused. `--sprite` is
  local-only (above); canon's `scripts/build-zmk.sh` carries the same flag.
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
