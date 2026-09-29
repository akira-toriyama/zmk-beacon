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
  the t-ogura module from canon) → t-rx4e (GIF sprite) → t-7c05 (a step per
  key press) → t-mxb7 (sprite updates).

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
  `config/prospector.conf` → `EXTRA_CONF_FILE` (the sprite fragment). Later
  wins: the board sets `CONFIG_ZMK_USB=y` and the shield conf's `=n` left it
  unset in `.config`. Shield-level facts (USB layout, display buffers) live
  in the shield conf, the LVGL pool in the shield's `Kconfig.defconfig`
  (below); canon-only facts (e.g. `CONFIG_ZMK_RGB_UNDERGLOW=n`, forced on by
  the Cyboard module) stay in canon's `config/prospector.conf`. This
  repository's own `config/prospector.conf` picks the HP bar and the fill
  layout (canon's screen) so that CI compiles `hp_bar.c`; before 2026-09-29
  the module's own build was the digits and never compiled it.
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
  layer index at 6, 4-byte layer name at 15, key press counter at 24). Only
  those bytes are written and read. Byte 24 is the module's WPM byte: it
  carried ZMK's WPM from 2026-09-27 to 09-28 for the sprite's tempo, was 0
  while the tempo was fixed, and since 2026-09-29 counts key presses modulo
  256 (t-7c05), the observer taking the difference between payloads. The
  version byte stayed 0x22 each time: a keyboard on an older image sends 0
  there, which reads as no presses, except one on the 09-27/28 images
  (adec978 to 3500c22), whose WPM changes would read as presses. A layout
  change is a new version byte (t-xe2q) and both ends move in one commit;
  canon then bumps its pin once.
- **The broadcaster runs only on a split central** (`ZMK_SPLIT_ROLE_CENTRAL`),
  so this repository's own build (`build.yaml`: the prospector shield) never
  compiles it. canon's `imprint_dongle` build is what checks it: after a change
  in `src/status_broadcaster.c`, build canon against the branch before
  merging. It selects `BT_EXT_ADV`; the consumer must set
  `CONFIG_BT_EXT_ADV_MAX_ADV_SET=2` (a `BUILD_ASSERT` in the source fails the
  build otherwise). The set is created from a settings commit handler at
  commit priority 1, after ZMK's first `bt_le_adv_start()`; the source header
  explains the ordering and the own work queue. A key press
  (`zmk_position_state_changed`, either half) kicks that queue's tick, so the
  payload with the new count goes on air at the next advertising event; the
  kick is skipped until the set advertises, so that it never creates the set
  early or retries a failed start at typing rate. The module's listener is
  linked first (`zmk.map`, 2026-09-29: `status_broadcaster.c.obj` before
  `behavior_hold_tap`, `combo`, `keymap`), so it counts a press before a
  hold-tap or combo captures it, and skips the second and later captured
  presses that `combo.c release_pressed_keys()` re-raises from the first
  listener, by their (position, timestamp).
  Hardware 2026-09-27 (t-eray): reconnects unchanged in 5 reboots and a half
  power cycle, 0 advertising errors, 255-269 payloads a minute at the
  Prospector Dongle.
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
  sprite's 10 ms timer runs there too.
- **A logging build drops its boot log unless the port is opened within a
  second or so.** ZMK sets the CDC ACM ring buffer to 1024 bytes and the boot
  banner fills it, so later lines are lost until the host reads. For a boot
  log, build once by hand with `-DCONFIG_USB_CDC_ACM_RINGBUF_SIZE=8192` next to
  `-DCONFIG_ZMK_USB_LOGGING=y`. Read the port at any rate but 1200 (see
  below). Logging builds print one observer line (payloads per minute), one
  screen line (the halves' raw levels and the payload age; absent with
  `BEACON_READINGS_NONE` while a sprite shows) and, with a sprite, one sprite line (frames
  decoded, invalidations, screen renders and their average time, speed, LVGL
  pool allocated and peak) every minute; the sprite line also counts the key
  steps, the observer line the presses received, and the broadcaster's minute
  line on the Imprint Dongle the presses counted. "Invalidated" is not
  "shown": LVGL merges invalidations between two renders, so renders is the
  shown count.
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
  selects the image and GIF widgets, `BEACON_READINGS_HP_BAR` the bar widget
  and the unscii 16 font). A widget used without its symbol fails at link
  time, not in Kconfig.
- **Sprite images are local-only.** The GIF is a personal file:
  `CONFIG_BEACON_SPRITE_GIF` names an absolute path, `build.sh --sprite`
  copies it to `~/.cache/zmk-beacon/sprite/sprite.gif`, and the build embeds
  it as `build/<target>/modules/zmk-beacon/beacon_sprite_gif.inc` (build tree
  only, `generate_inc_file_for_target`). `.gitignore` has `*.[gG][iI][fF]`
  (`core.ignorecase` is false on this case-sensitive volume); never
  commit the GIF, an `.inc`, frames or previews, and never write its path or
  its subject into this repository. The sprite's name
  (`CONFIG_BEACON_SPRITE_NAME`, `build.sh --sprite-name`) is the subject too:
  `build.sh` writes it with the GIF's path into the Kconfig fragment
  `~/.cache/zmk-beacon/sprite/sprite.conf` and passes only that path
  (`EXTRA_CONF_FILE`; with `-DCONFIG_...` west's message on a failed configure
  step, which quotes the whole cmake command line, showed the name, reproduced
  2026-09-29), prints only its length, and it never goes into a commit, a PR
  or a doc here. CI and `release.yml` build without either, so no release
  carries a sprite image or a name.
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
- **The screen is two siblings** (`src/prospector_screen.c`): a battery
  readings style along the bottom (`src/readings.h`, a three-call table; the
  `BEACON_READINGS` Kconfig choice picks `readings_digits.c` or `hp_bar.c`,
  and `BEACON_READINGS_NONE` shows the digits only when the build has no
  sprite, so a consumer's conf stays valid for CI) and the sprite above it,
  never over it. The readings keep a 12 px margin: the panel's corners are
  rounded and a box 2 px from the edge lost its bottom corners (hardware
  2026-09-28). A filling sprite (`BEACON_SPRITE_FILL`) keeps 2 px. No LVGL
  theme is installed, so every widget sets its styles itself. The HP bar's
  box is 28 px, or 46 px with a sprite name (`CONFIG_BEACON_SPRITE_NAME`:
  the value "65/100" sits on the bar in white and the name on a second row,
  the user's pick from twelve trials, canon t-er81, 2026-09-28/29);
  `hp_bar.c` and the `sprite_readings_top` of `CMakeLists.txt` (200 / 182)
  carry the same two heights, change both together. Every text there is
  unscii 16: a 16 px cell on a 17 px line; capitals and digits ink rows
  1..14 of the cell (most 12 px wide, some 14 or the full 16, `/` included),
  and a descender, `,` `;` `_` reach row 16, which is why the name row sits
  at 24 in a 42-row content area.
- **The sprite draws itself** (`src/sprite.c` `blit()`, since 2026-09-28): a
  plain transparent object whose `LV_EVENT_DRAW_MAIN` handler scales the
  gifdec canvas by nearest neighbour straight into the layer's RGB565 buffer.
  That is safe only under conditions the screen must keep (all read in this
  LVGL checkout, 9.3.0-dev, review 2026-09-28): `LV_USE_OS` is NONE (a
  `BUILD_ASSERT`), so `lv_draw_finalize_task_creation()` dispatches and the
  sw unit renders each draw task synchronously, one per task created; the
  sprite is the screen's first child, so the only task before its event is
  the screen's fill; and nothing drawn before it renders through a layer
  (`opa_layered`, a transform, a blend mode, a bitmap mask, an `lv_bar`
  indicator shorter than its radius), whose blend task runs one task late
  and would land on top of the sprite. The refresh renders an invalid area
  in VDB-sized parts and sends the event once per part with
  `layer->buf_area` / `_clip_area` set to it, and `LV_COLOR_16_SWAP` is
  applied at flush (`lv_refr.c`, `lv_draw_sw_rgb565_swap()`), so the layer
  holds native RGB565. Before it, lv_image with `LV_IMAGE_ALIGN_STRETCH` cost 105 ms a
  render of the 2x box (about 70 ms of it LVGL's software transform,
  `LV_DRAW_SW_ASM_NONE`) once its `LV_EVENT_REFR_EXT_DRAW_SIZE` claim of an
  extra object size on every side had been zeroed (152 ms and the whole
  panel before that, hardware 2026-09-27). Invalidating only the frames'
  touched rectangles instead of the whole box changed nothing: the test
  GIF's frames cover 65-76 % of the canvas and two consecutive ones nearly
  all of it.
- **The panel's SPI runs at 32 MHz** (`mipi-max-frequency` in the shield
  overlay; SPIM3 is the one nRF52840 instance that can) **with high drive on
  the SPIM pins** (`nordic,drive-mode = <NRF_DRIVE_H0H1>` in `spi3_default`:
  nrfx sets H0H1 itself at 32 MHz, but Zephyr's SPIM driver leaves the pins
  to pinctrl, `skip_gpio_cfg`). A render of the 2.2x sprite box above the HP
  bar, flush included, takes about 43 ms against 63 at 16 MHz (20 MHz in the
  overlay rounded down), hardware 2026-09-28. The observer then counted
  239-269 payloads a minute against 255-269 with fewer renders: the display
  thread now takes about 63 % of the CPU and decoding 33 %, and the host's
  scan callback occasionally waits. Above the ST7789V data sheet's 15 MHz
  write cycle, as 16 MHz already was; set 20 MHz back if the panel ever
  shows noise.
- **The sprite's CPU budget** (hardware 2026-09-28): a gifdec decode costs
  about 11 ms a frame and a render about 43 ms, so at the idle tempo (75 %
  of the GIF's own since 2026-09-29, after 100 % and 50 % the same day and
  150 % from 09-28, all the user's picks after a WPM-driven tempo on
  09-27/28) the screen shows the GIF's every step. The
  player keeps the tempo by merging frames when drawing falls behind, up to
  `CATCHUP_MAX` frames a tick; with the old 105 ms renders and `CATCHUP_MAX`
  16, 300 % and 500 % requested both reached about 2.8x at 3.5 renders a
  second. While the keyboard is typed on (`KEY_GRACE_MS` after the last
  press) the tempo stops and each press owes the sprite `FRAMES_PER_PRESS`
  (8, the user's pick after trying 1, 2 and 4) frames that change the canvas
  (`STEP_DECODE_MAX` decodes at most each, the test GIF needs two), stepped
  once per render (`LV_EVENT_RENDER_READY` gates the next): one frame while
  the queue holds a press's worth or less, otherwise as many as drain it in
  `DRAIN_RENDERS` (6) renders, within `DECODES_PER_TICK` (8) decodes a tick
  and `FRAMES_QUEUE_MAX` (4 presses) queued; `KEY_GRACE_MS` (200) after the
  last press arrived the queue is dropped and the tempo resumes. That grace
  equals the advertising interval, so a lone press shows three or four of
  its eight frames and a stream of presses keeps the queue at one payload's
  presses: the look the user picked (t-7c05, after 1 s); the observer
  accumulates the payload's 8-bit counter, the sprite consumes it. A
  keyboard reboot restarts the counter: the observer drops a difference
  above `KEYS_DELTA_MAX` (32) as that restart, a smaller one queues that
  many presses' frames once. Hardware 2026-09-29, logging images: on the
  one-frame-per-press build (4ca0f2e, 1 s grace) a minute of 50 presses
  counted 50 on the Imprint Dongle and arrived as 50 presses and 50 steps on
  the Prospector Dongle; on the eight-frame build (1 s grace) a minute of
  typing decoded 2,305 frames for 961 steps at 555 renders with 261-266
  payloads a minute received; reflashing the Imprint Dongle under a running
  observer read as 182 presses, the case the guard now drops. "Key steps" in
  the sprite's minute line counts frames, not presses.
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
  Montserrat 16 + 28; Montserrat 48 is about 97 KB of flash (from its source
  tables) and stays linked by the shield conf even when the HP bar, which
  draws its text with unscii 16, is the readings style. RAM is mostly LVGL:
  VDB 30% × 2 and the pool.
- **Fleet-managed files, do not edit here**:
  `.github/workflows/{actionlint,commit-lint,repo-policy,taplo,task-status,version-preview,zizmor}.yml`,
  `.github/zizmor.yml`, `.github/dependabot.yml`, `docs/commit-convention.md`.
  fleet-sync in akira-toriyama/.github overwrites them from its `fleet/`
  directory. `glyph.toml` was written by `glyph init --gemoji`: edit, never
  regenerate.

## Build

- `./scripts/build.sh [shield] [--logging] [--sprite <gif> [--sprite-name <text>]] [--update]` —
  Docker (`zmkfirmware/zmk-build-arm:stable`), workspace `~/.cache/zmk-beacon`,
  output `firmware/<shield>[-sprite][-logging].uf2` (gitignored). `--update`
  refreshes zmk@main; without it the cached checkout is reused. `--sprite` and
  `--sprite-name` are local-only (above); canon's `scripts/build-zmk.sh`
  carries `--sprite` and derives the name from the GIF's file name
  (`assets/sprite-name.sh` there).
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
