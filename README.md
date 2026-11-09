# zmk-beacon

A [ZMK](https://zmk.dev) module for both ends of one status advertisement:
the **Prospector Dongle**, a Seeed XIAO nRF52840 with a Waveshare 1.69"
240x280 LCD (the
[beekeeb pre-soldered Prospector](https://shop.beekeeb.com/products/pre-soldered-prospector-zmk-dongle))
that sits next to a keyboard and shows its battery as an HP bar under a
sprite, and the broadcaster that sends the advertisement from the keyboard's
central, the device that runs the keymap: a split's central (a half or a
dongle), or the keyboard itself when it is not a split. The Prospector Dongle
listens passively for the keyboard and advertises connectably for the
sprite, which a Mac can swap over BLE; it never pairs. Its USB-C carries
power and one serial port, used to reflash it, to take a picture of its
screen and, in a logging build, for its log.

The name: a beacon shows others where you are; a lantern lights what is at
hand. The sibling module
[zmk-lantern](https://github.com/akira-toriyama/zmk-lantern), the keyboard's
per-key LEDs, is the lantern.

## Using it in a ZMK config

### The least

1. `config/west.yml`: ZMK at `main`, and the module as a project, pinned by
   commit:

   ```yaml
   manifest:
     remotes:
       - name: zmkfirmware
         url-base: https://github.com/zmkfirmware
       - name: akira-toriyama
         url-base: https://github.com/akira-toriyama
     projects:
       - name: zmk
         remote: zmkfirmware
         revision: main
         import: app/west.yml
       - name: zmk-beacon
         remote: akira-toriyama
         revision: <commit sha>
         path: modules/zmk-beacon
     self:
       path: config
   ```

   The module needs ZMK `main` (Zephyr 4.1, LVGL 9): it is built against
   `main` only, and ZMK's config template pins the `v0.3` release (Zephyr
   3.5, LVGL 8), where it is untried (its sources use Zephyr 4.1 and LVGL 9
   APIs, and the Prospector Dongle's board name exists on `main` only), so a
   config made from the template moves `defaults: revision: v0.3` to `main`
   as above, calls ZMK's workflow at `main` too (Building it with ZMK's CI,
   below) and names `build.yaml`'s boards as `main` does, each with its `zmk`
   variant: `nice_nano_v2` is `nice_nano/nrf52840/zmk` there and
   `seeeduino_xiao_ble` is `xiao_ble/nrf52840/zmk` (ZMK's Zephyr 4.1 post,
   https://zmk.dev/blog/2025/12/09/zephyr-4-1); `main` has no `nice_nano_v2`,
   and its workflow stops a board built without its `zmk` variant (`Missing
   ZMK Compat`), the template's own `nice_nano` included. `revision` is a
   commit of this repository's `main` (a release
   tag names one as well; v0.1.0 predates most of this page). Build both
   ends from one commit: they share the payload's layout (The status
   advertisement, below), and a field added without a new version byte is
   missing from an older broadcaster while a newer Prospector Dongle still
   accepts its payload.

2. The keyboard: three lines in the central's own conf, the central half's
   (`config/corne_left.conf` for ZMK's Corne, whose left half is the
   central by its shield's default) or the dongle's
   (`config/<shield>_dongle.conf`), and the first two in the conf of a
   keyboard that is not a split (`config/reviung41.conf` for ZMK's
   Reviung41). ZMK merges every conf whose name prefixes the shield
   (`corne.conf` and `corne_left.conf` for `corne_left`), so the central's
   own conf reaches the central alone; on a peripheral the lines drop out
   with Kconfig warnings, since the broadcaster exists only on the central,
   and so does the third on a keyboard that is not a split, since the
   fetching exists only on a split central.

   ```
   CONFIG_BEACON_STATUS_BROADCAST=y
   CONFIG_BT_EXT_ADV_MAX_ADV_SET=2
   CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y
   ```

   The first builds the broadcaster. The second is required with it: the
   broadcaster selects `CONFIG_BT_EXT_ADV`, under which ZMK's own
   connectable advertising takes one set of the same pool, whose default is
   1 (asserted at build time). The third is the halves' batteries on a
   split: ZMK's peripheral battery events, which a central raises only with
   this line, off by ZMK's default; without it the build passes and the
   halves' readings stay empty, so a dongle's Prospector Dongle shows an
   empty bar and `--/100` (read from the code, not tried). The batteries are
   a choice: the broadcaster works without the line (the key presses and the
   modifiers, which the screen and the sprite follow).

   The payload carries two batteries, the left half's and the right half's.
   A central half, or a keyboard that is not a split, sends its own level,
   ZMK's battery level (`CONFIG_ZMK_BATTERY_REPORTING`, on by default with
   BLE; without it no battery is sent at all, and ZMK's own split code does
   not link with the third line), as the left half's, and the
   half it connects to fills the other: so the bar is the mean of both
   halves on a keyboard whose central is a half, and the keyboard's own
   level on one that is not a split. A dongle sends none of its own: on a
   split central with two peripherals or more, the first two fill both, in
   the order the central first heard them. A keyboard whose central is its
   right half adds `CONFIG_BEACON_OWN_BATTERY_RIGHT=y` (its left half then
   fills the left one), and a dongle with one peripheral
   `CONFIG_BEACON_OWN_BATTERY_NONE=y`.

   ZMK `main` compiles the fetching with a write it does not bounds-check
   ([zmk#3522](https://github.com/zmkfirmware/zmk/issues/3522); the fix,
   [zmk#3556](https://github.com/zmkfirmware/zmk/pull/3556), is open): the
   disconnect of any Bluetooth connection that is not a half's, a host's,
   makes the central write a 0 to index 234 of an array of one byte per
   peripheral (a single byte on a keyboard whose central is a half), which
   on the issue's keyboard zeroed a variable of the Bluetooth controller
   (late radio events, then a fatal controller assert); canon carries the
   fix as a patch, which a config repository built by ZMK's reusable
   workflow cannot, so a central that hosts connect to over Bluetooth
   carries the bug until the fix merges.

   The broadcaster has run only on a USB-powered dongle with no Bluetooth
   host; on a central half like the Corne's, on a keyboard that is not a
   split like the Reviung41, next to a host over Bluetooth and on battery,
   it is built, not tried. The advertisement goes out in plaintext to anyone
   in range; what it tells, and the untried cases, are in The status
   advertisement, below. The check builds' confs are these three lines,
   [config/corne_left.conf](config/corne_left.conf), and the first two,
   [config/reviung41.conf](config/reviung41.conf); canon's
   [config/imprint_dongle.conf](https://github.com/akira-toriyama/canon/blob/main/config/imprint_dongle.conf)
   carries them on a dongle, beside lines that only canon's ZMK and Zephyr
   patches define and canon's own choices: take the three lines from it,
   not the file.

3. The Prospector Dongle: a device of its own, with its own target in
   `build.yaml` next to the keyboard's:

   ```yaml
   include:
     - board: xiao_ble/nrf52840/zmk
       shield: prospector
   ```

   The hardware is the Prospector's: a Seeed XIAO nRF52840 driving the
   Waveshare 1.69" ST7789V panel over SPI, with the backlight on D6 (P1.11);
   the pin mapping and the panel parameters are prospector-zmk-module's (the
   shield's `prospector.overlay`), and the beekeeb pre-soldered unit is the
   one it has run on. The shield is a display and nothing else: its conf
   turns ZMK's split, Bluetooth HID and USB HID off, so the device is never
   a split central, a split peripheral or a keyboard (its one BLE peripheral
   role is the sprite receiver's), and `prospector` is never a
   second shield of a keyboard's build (its conf would turn the keyboard's
   split off, and its scan and its advertising would take what a central
   needs; read from the code). A Prospector that is today a keyboard's
   dongle, as other Prospector modules make it, needs another central first:
   a dongle, or a central half (a keyboard that is not a split is its own).
   A `config/prospector.conf` in the
   config repository overrides any line of the shield's own
   `prospector.conf` (Then, as you want them, below); none is needed. The
   screen itself is `CONFIG_BEACON_SCREEN`, which the shield turns on;
   another display device that is not a keyboard can take it in its own
   conf, with ZMK's custom status screen and Bluetooth, and the screen's
   options follow it (the option's help says what each needs; read from the
   code: the shield is the only display it has run on).

4. Flashing. Both devices take a UF2 image. The Prospector Dongle's first
   flash is the double-tap (the 1200 baud entry below exists once this
   module's image runs): double-tap its reset button, and its bootloader
   mounts as `XIAO-SENSE`; then `cp -X <image>.uf2 /Volumes/XIAO-SENSE/`
   (ZMK's workflow names the image `prospector-xiao_ble_nrf52840_zmk-zmk.uf2`
   in its `firmware` artifact, this repository's own build `prospector.uf2`;
   the `cp` ends with an I/O error: that is the device rebooting into the
   new firmware). Once this module's image runs, a program that sets its
   serial port to 1200 baud on purpose (`stty -f <its port> 1200` on macOS,
   the port found by its product string) reboots it into the bootloader
   with no hands on the device, and the copy is the same (The serial port,
   below; canon's `./scripts/flash-dongle.sh <image.uf2>` does the whole of
   it). `XIAO-SENSE` is the factory bootloader's volume name (the XIAOs this
   module has run beside mount so too; Adafruit's upstream build for the
   plain XIAO nRF52840 names it `XIAO-BOOT`): put one XIAO in its bootloader
   at a time. The keyboard's central is flashed as its board says; with the
   1200 baud entry (Then, as you want them, below) it takes the same touch.

   Until the keyboard broadcasts, the screen shows a grey `HP` and `--/100`,
   with an empty name row and nothing above it (this image has no sprite
   until one is sent or embedded: Then, as you want them, a sprite), and
   five minutes after boot with no key press it goes dark
   (`CONFIG_BEACON_SCREEN_OFF_AFTER_S`, default 300; `=0` in
   `config/prospector.conf` keeps it lit); the first key press the keyboard
   reports lights it. With the keyboard heard, `HP` turns from grey to gold,
   the bar stays empty with `--/100` until a half's reading arrives (the
   third line of the keyboard's conf, above), and then fills in colour (The
   screen, below).

### Then, as you want them

- **A sprite.** A GIF the screen plays above the HP bar, stepped by the key
  presses (The sprite, below). Three routes, none of which puts the GIF into
  a repository: a local build with the GIF embedded,
  `./scripts/build.sh --sprite <gif>` (Building this repository, below); a
  GitHub Actions run that embeds it, with neither a Mac nor Docker (A sprite
  image from GitHub Actions, below); or the default image's sprite room,
  which the Mac sprite sender writes over BLE, the image unchanged
  (Swapping the sprite over BLE and The sprite sender, below).
- **No receiver: `CONFIG_BEACON_SPRITE_RECEIVE=n`** in
  `config/prospector.conf`. With the receiver on (the default), anyone in
  range can connect, replace the sprite and read the name of the sprite
  playing (no pairing, no encryption), and the status pauses while they stay
  connected; with it off the device only
  listens and never transmits over BLE, the sprite sender cannot reach it,
  and an embedded pack may take the flash the receiver would (about 150 KB
  in place of about 80 KB).
- **The screen-off time: `CONFIG_BEACON_SCREEN_OFF_AFTER_S`** (default 300,
  seconds without a key press; 0 keeps the screen lit).
- **The brightness: `CONFIG_BEACON_BACKLIGHT_BRIGHTNESS`** (default 80,
  percent). A lit screen has that one brightness: the shield reads no
  ambient light sensor and no touch panel (the beekeeb unit has no sensor
  and its panel is not wired; on a unit that has them, both go unused).
- **The panel's clock.** The panel's SPI runs at 32 MHz, above the ST7789V
  data sheet's write cycle, as is the 20 MHz fallback (16 MHz effective).
  Should the panel show noise, a `config/prospector.overlay`, which ZMK
  applies over the shield's, sets `mipi-max-frequency` to 20 MHz (read from
  the code, not tried):

  ```dts
  &st7789v {
      mipi-max-frequency = <20000000>;
  };
  ```

- **The keyboard's reflash from the host:
  `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD=y`** in the central's conf. The
  central then reboots into its UF2 bootloader when a host sets its USB
  serial port to 1200 baud, as the Prospector Dongle does by default (The
  serial port, below). It needs a USB CDC ACM port and the board's retained
  boot mode: the XIAO BLE's devicetree carries both, so a central on it
  needs nothing more; the nice!nano has the boot mode and no port, so a
  central on it adds the port in its overlay (ZMK reads one config overlay
  a build and tries the shield's shared name first, `corne.overlay` before
  `corne_left.overlay`, so with a shared overlay the central gets one of its
  own over a dtsi both include, as zmk-lantern's README says in item 2 of
  The least) and gives `CONFIG_USB_CDC_ACM` the `CONFIG_SERIAL` it depends
  on, as the same README shows under "The host"
  ([zmk-config-lantern-check](https://github.com/akira-toriyama/zmk-config-lantern-check)'s
  central half does so, Building it with ZMK's CI, below; built, not tried
  on hardware). Without a port the line drops out with a warning. Any
  program that sets the rate reboots the keyboard, a serial monitor
  included.

### Building it with ZMK's CI

A config repository made from
[ZMK's template](https://github.com/zmkfirmware/unified-zmk-config-template)
builds with ZMK's reusable workflow, the module included, once its
`config/west.yml` is the one above, its `.github/workflows/build.yml` calls
the workflow at `main`, as the manifest tracks ZMK `main`, and its
`build.yaml` names the boards as `main` does (step 1 of The least):

```yaml
jobs:
  build:
    uses: zmkfirmware/zmk/.github/workflows/build-user-config.yml@main
```

`build.yaml` lists the keyboard's targets (`board: nice_nano/nrf52840/zmk`
with `shield: corne_left` and `corne_right`, say) and the Prospector
Dongle's (`xiao_ble/nrf52840/zmk` with `prospector`). West fetches the
module as a project of the manifest and Zephyr finds it by its
`zephyr/module.yml`; nothing of ZMK is patched.
[zmk-config-lantern-check](https://github.com/akira-toriyama/zmk-config-lantern-check)
is that repository for the keyboard's side: the Corne on the nice!nano, the
left half the central with the three lines, the 1200 baud entry and
zmk-lantern, built green by ZMK's workflow
([the run](https://github.com/akira-toriyama/zmk-config-lantern-check/actions/runs/42452649801)):
the central half's `.config` lists the broadcaster's lines, the right
half's none of them. This repository's own checks are the stock Corne left
half with the three lines alone
([config/corne_left.conf](config/corne_left.conf); no port, no 1200 baud
entry, no zmk-lantern) and the stock Reviung41 with the first two
([config/reviung41.conf](config/reviung41.conf)), built with the Prospector
Dongle by its own reusable workflow, which runs the same west build as
ZMK's (Building this repository, below).

## Pokémon sprites

A Pokémon above the HP bar is what most people will want, and this section
is how to get one there. The pictures are not in this repository, its CI,
its releases or the template repository, and never will be: each person
fetches them for their own device, from the public sources below, and
nothing made from them (a sprite pack, an image with a sprite embedded, a
run's artifact, a picture of the screen) is for publishing. A check on every
pull request here, the picture ban, keeps a picture out of this repository
by its first bytes, its name and its path.

The pictures are [PokeAPI/sprites](https://github.com/PokeAPI/sprites)'
Black/White animated sprites, `<number>.gif` under
`sprites/pokemon/versions/generation-v/black-white/animated/`, the back views
in its `back/`, the number a species' National Pokédex number (from 10001
up, a form's id); the names are
[PokeAPI/pokeapi](https://github.com/PokeAPI/pokeapi)'s
`data/v2/csv/pokemon_species_names.csv`. The commits this module was tried
with are `DEFAULTS` in `tools/sprite_fetch.py`. The sprites are © The Pokémon
Company (PokeAPI/sprites' `LICENCE.txt`); the names come from a BSD-3-Clause
data set; Pokémon and the Pokémon character names are trademarks of
Nintendo. Build the image, or send the
sprite, for your own device and your own use; do not publish the artifact,
the firmware or anything made from the pictures, and keep a repository that
builds with them private.

Three routes, by what you have (Then, as you want them: a sprite, above):

- **Neither a Mac nor Docker: a GitHub Actions run.** The template repository
  builds an image with the sprite of one number embedded, named in English or
  katakana (A sprite image from GitHub Actions, below). One sprite an image:
  another sprite is another run and another flash.
- **Docker: a local build.** `python3 tools/sprite_fetch.py --number <n>
  --out <dir>` fetches the number's GIF as `<dir>/sprite.gif` and writes its
  name into `<dir>/sprite.conf` (`--lang ja` for the katakana name, as the
  sender names it; the Actions route's repository inputs are its flags), and
  `./scripts/build.sh --sprite <dir>/sprite.gif --sprite-name <that name>`
  builds `firmware/prospector-sprite.uf2` (Building this repository, below).
  The image keeps the receiver, so the embedded pack is bounded at about
  80 KB (Limits, below), and a pack in the sprite room plays in its place.
- **A Mac: the sprite sender.** The image stays as it is and the sprite
  changes over BLE, a different one every morning from a LaunchAgent, below.

### The Mac route

What it takes, beyond The sprite sender, below:

- **A device with the receiver**: the default image, the one your config's
  CI builds (The least, steps 3 and 4) or `./scripts/build.sh prospector`
  (no published release has the receiver yet: v0.1.0 predates it). The
  Actions route's image has no receiver, so a device that runs it is flashed
  with the default image first, and the room, which that image left alone,
  plays again.
- **A Mac on macOS 26 or later** (the app's minimum, below which it does not
  run) with that macOS's Swift 6 toolchain (Xcode or the Command Line Tools)
  and `/usr/bin/python3`, which runs the tools. There is no prebuilt app:
  `cd tools/sprite-sender && ./package.sh` builds and signs
  `SpriteSender.app`.
- **A signing identity**, so that the Bluetooth permission survives a
  rebuild: `package.sh` signs with the self-signed identity `chord-dev` of
  the login keychain when it finds one, and `SIGN_IDENTITY=<name>` names
  another; without one it signs ad hoc, and macOS asks again after every
  build. Keychain Access makes one (Certificate Assistant, Create a
  Certificate, of the type Code Signing; untried here), and
  [chord](https://github.com/akira-toriyama/chord)'s `setup-signing-cert.sh`
  made this one (`chord-dev`, the default here; it needs OpenSSL 3 on the
  PATH, not macOS's LibreSSL, and then `security import`).
- **The Bluetooth dialog answered once, for the app itself**: macOS asks the
  first time the app uses Bluetooth and keys the answer to what started the
  process, so the first run goes through the LaunchAgent, below, while you
  are at the Mac (`launchctl kickstart`), not through a terminal, which would
  get the permission in the app's place. The app waits ten minutes for the
  answer and exits 1 without one, as it does when refused (System Settings,
  Privacy & Security, Bluetooth); a morning run finds nobody to answer.
- **The collection**: a sparse, blobless clone of the sprites' directory
  (the whole repository is about 27 GB; this clone's `.git` is about 100 MB
  and takes about 15 s on a fast line):

  ```sh
  git clone --filter=blob:none --sparse https://github.com/PokeAPI/sprites.git <dir>
  git -C <dir> sparse-checkout set --no-cone '/sprites/pokemon/versions/generation-v/black-white/animated/'
  git -C <dir> checkout <commit>   # DEFAULTS' sprites_ref: the Actions route's pictures; or stay at HEAD
  ```

  `front` is then
  `<dir>/sprites/pokemon/versions/generation-v/black-white/animated` and
  `back` its `back/`. The names CSV is one file: `sprite_fetch.py` writes it
  as `names.csv` beside the GIF it fetches (the local build, above), from the
  pinned commit.
- **`sender.json`** (The sprite sender, below): `front`, `back`, `names`, a
  `state` file of your own (its directory is made) and `range`. `"1 ~ 649"`
  is the longest run from 1 in which every number has a front view, a back
  view and a species row; above 649 some numbers have no back view, and the
  forms numbered from 10001 up have no row in the CSV, so the sender names
  them by their number. The name is the katakana one when it fits the bar,
  else the English one: the sender has no language setting (the Actions
  route defaults to English).
- **Your Prospector Dongle alone in range**: the sender connects to the
  first device it finds that advertises the receiver's service, without
  telling which, and writes its room, which keeps nothing of the pack it had
  (read from the code, not tried with two): a second Prospector Dongle in
  range, someone else's or a spare, can get your sprite instead.

Then a dry run from a terminal,
`SpriteSender.app/Contents/MacOS/sprite-sender daily --config <sender.json> --dry-run`,
which reads the config and the collection, packs the next GIF and sends
nothing, Bluetooth untouched; and the LaunchAgent,
`~/Library/LaunchAgents/local.sprite-sender.plist`, with your paths (launchd
runs no shell: absolute paths, no `~`):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>local.sprite-sender</string>
  <key>ProgramArguments</key>
  <array>
    <string>/Users/you/zmk-beacon/tools/sprite-sender/SpriteSender.app/Contents/MacOS/sprite-sender</string>
    <string>daily</string>
    <string>--config</string>
    <string>/Users/you/sprite-sender/sender.json</string>
  </array>
  <key>StartCalendarInterval</key>
  <dict>
    <key>Hour</key>
    <integer>6</integer>
    <key>Minute</key>
    <integer>0</integer>
  </dict>
  <key>RunAtLoad</key>
  <false/>
  <key>StandardOutPath</key>
  <string>/Users/you/Library/Logs/sprite-sender.log</string>
  <key>StandardErrorPath</key>
  <string>/Users/you/Library/Logs/sprite-sender.log</string>
</dict>
</plist>
```

`launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/local.sprite-sender.plist`
loads it, `launchctl kickstart gui/$(id -u)/local.sprite-sender` runs it
now (the first run, which asks for Bluetooth, above), and
`launchctl bootout gui/$(id -u)/local.sprite-sender` unloads it until the
next login (remove the file to be rid of it).
`StartCalendarInterval` runs it at six every morning, and a run the Mac
slept through happens at the next wake; `RunAtLoad` is false so that loading
the agent sends nothing. A morning that fails (no device in range within
30 s, a GIF the device refuses: exit 1) is skipped, and the next morning
takes the next number. The log's lines tell things about the sprite (The
sprite sender, below), and `~/Library/Logs` is read by you alone: keep the
log out of anything public. The agent ends with the swap; for the keys of
a keyboard running zmk-lantern to take the sprite's colours, the agent runs
a script in the sender's place, the swap first (zmk-lantern's README, The
host: every morning).

Other pictures go the same ways: any directory of `<number>.gif` files runs
through the sender, their back views in a directory of the same names;
without `names` the name is the number, and a CSV without PokeAPI's columns
is a config error (exit 2). The Actions route and `sprite_fetch.py` take
another repository of such files with its commit (`sprites_repo`,
`sprites_ref`, `sprites_dir`) and name the number from the default CSV
unless `name` is given.

The keyboard is the same for every route: the bar shows its battery once
the central broadcasts (The least, the keyboard); until then the
screen shows a grey `HP` and `--/100`, and the default image, a local
build's too, goes dark five minutes after boot without a key press (The
least, step 4), where the Actions route's stays lit (A sprite image from
GitHub Actions, below).

## What it does

### The screen

One battle-screen HP bar in a dark box along the bottom: `HP` and a bar with
its value on it (`65/100`). Its length is the keyboard's battery: the mean
of the payload's two batteries, or the one that has a reading (a dongle's
two halves; a central half's own and the other half's; the keyboard's own
when it is not a split). The bar is green, yellow under 50 % and red
under 20 %. A build that plays a named sprite (a received pack's name, or
`CONFIG_BEACON_SPRITE_NAME`, `--sprite-name`, for the embedded one) shows
its name on the box's first row, centred, above `HP` and the bar: an ASCII
name in unscii 16, like
`HP`, a katakana one in the letters of the first Game Boy games (PKMN Strict
from [pkmn](https://github.com/nue-of-k/pkmn), two pixels a dot and a pixel
bolder, the dakuten above the cell; Credits below). A name is at most 15
bytes of UTF-8, all printable ASCII or all katakana: 15 ASCII characters, or
5 katakana (`tools/sprite_pack.py` says which characters).

| Shows | Meaning |
| --- | --- |
| A coloured bar and `65/100` | The keyboard's battery (above), from a status advertisement received in the last minute. |
| An empty bar and `--/100` | The keyboard is heard, but neither battery has a reading: none of the central's own (a dongle sends none; a central without a battery reading has none yet) and none of the halves (off or out of range, not reported since they connected, or not fetched by the central: the third line of The least, the keyboard). |
| Grey `HP` and `--/100` | No status advertisement in the last minute, or none since the Prospector Dongle started; a broadcaster of another payload version counts as none. |
| Nothing: the screen is dark | No key press on the keyboard for five minutes. |

The screen turns off when the keyboard has not been typed on for five minutes
(`CONFIG_BEACON_SCREEN_OFF_AFTER_S`, default 300; 0 keeps it lit): the
backlight fades out within a second, and the next key press, any key, either
half's on a split, lights it at once. Only the keyboard's key presses count, so the screen
also goes dark while only a pointing device is in use, and five minutes after
the Prospector Dongle started when no key press has reached it by then.

The name row shows the name of the sprite playing: the one a received pack
carries, or the embedded sprite's (`CONFIG_BEACON_SPRITE_NAME`). The row
exists in every build that can receive a sprite, empty until a named sprite
plays (an embedded sprite built without a name leaves it empty), and in a
build with a named embedded sprite.

### The status advertisement

What the keyboard's central sends and the Prospector Dongle reads: 26
bytes of manufacturer data in a legacy, non-connectable, non-scannable
advertisement on a second advertising set next to ZMK's own, every 200 ms
for as long as the central runs. A key press, a confirmed modifier and its
release refresh the payload, which goes on air at the next advertising
event (at most about 210 ms later) and adds no advertisement. The layout is
[src/status_payload.h](src/status_payload.h), prospector-zmk-module v2.2.3's
offsets under the same prefix and version byte. The fields written: two
batteries, the left half's and the right half's, 0 for no reading (the
central's own level in the one `CONFIG_BEACON_OWN_BATTERY` names, the left
by default and neither on a split central with two peripherals or more, a
dongle; its peripherals in the others, in ZMK's slot order, the order the
central first heard them: a dongle's two halves; a central half's own and
the other half's; the keyboard's own when it is not a split), the highest
active layer's index and the first four bytes of its name, a count of key
presses (every key, either half's on a split, modulo 256) and the modifiers
held, once
a hold is confirmed (pressed for 0.2 s, or a non-modifier key pressed under
it; a layer key or another modifier confirms nothing), each kept on air
until 0.25 s after its confirmation even when released sooner. Nothing
pauses it for idle; ZMK's deep sleep (`CONFIG_ZMK_SLEEP`, off by default)
powers the central off and stops it with the rest, and the wake is a boot,
which starts the count over.

It goes out in plaintext, with no pairing: anyone with a Bluetooth scanner in
range reads the batteries, the layer and its name, how many keys were pressed
in each 200 ms and which modifiers are held, never which keys; and the
keyboard's address stays the same for a whole boot, so a session can be
followed. The Prospector Dongle takes the payload of any keyboard in range,
with no filter: two broadcasting keyboards in one room mix on its screen, the
battery following whichever it heard last and the presses miscounted, and a
keyboard running t-ogura's prospector-zmk-module v2.2.x, which sends the same
prefix and version, shows too, its fields read as this layout's (read from
the code, not tried).

The cost on the central: `CONFIG_BT_EXT_ADV`, about 34 KB of flash and 11 KB
of RAM on an nRF52840, and the host on the extended HCI commands; no
Bluetooth connection, and no set of ZMK's. With it a split central's scan
for its halves is an extended scan that follows nearby AuxPtr fields, and
ZMK's Zephyr fork lacks upstream 5ce9d0c621, so an invalid AuxPtr channel
index can trip a controller assert while a half is away (read from the
code, never seen). The one central it has run on is a USB-powered dongle
with no Bluetooth host: on a central half, on a keyboard that is not a
split, next to a host connected over Bluetooth, and on battery, it is built
and untried, and its draw on a battery is unmeasured.

### The serial port

On USB the Prospector Dongle is one CDC ACM serial port, product string
`Prospector Dongle`, and no keyboard: no HID device at all, under ZMK's
default USB IDs, which most ZMK devices keep (canon's Imprint Dongle does).
Find its port by that product string (canon's
`python3 scripts/dongle.py port prospector`, for example),
never by a `/dev/cu.usbmodem*` name, which follows the USB socket it is
plugged into, nor by its USB IDs. The port's rate is a command:

- 1200 baud reboots the device into its UF2 bootloader, whose volume is
  `XIAO-SENSE` with the factory bootloader (The least, step 4)
  (`CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD`, on by default for the shield): a
  host flashes it with no hands on the device by setting the
  rate, waiting for the volume, copying the image onto it and waiting for
  the device to come back (canon's `./scripts/flash-dongle.sh <image.uf2>`
  does this, and checks that the volume belongs to this device). A
  keyboard's central with the option takes the same touch.
- 2400 baud makes the device render its screen once more and send it over
  the port, band by band and closed by a CRC-32 (`CONFIG_BEACON_SCREEN_DUMP`,
  on by default for the shield; the format is the header of
  [src/screen_dump.c](src/screen_dump.c)). canon's
  `python3 scripts/dongle.py shot` sets 2400, writes the bands into a PNG,
  prints its path and sets 115200 again. The screen stands still while the
  picture goes out, and a logging build leaves out the log lines of that
  moment. The picture is what the device draws, not what the backlight
  shows: a dark screen gives the picture it would show when lit. It shows
  whatever sprite plays, the sprite room's included (the default image
  plays the room's pack), and its name: keep any picture of a device that
  plays a sprite out of repositories, like the GIF.

So never set the port to either rate by accident: any program that sets it,
a serial monitor or `stty` included, does the same. A logging build
(`--logging`, Building this repository) writes its log on the port at
115200; a plain build keeps the port silent.

## The sprite

The screen plays a sprite pack (The sprite pack, below) above the HP bar:
centred, standing on the bar, and scaled to the largest size of its own
proportions that fits the space above it, 2 px in from the panel's top and
sides (a 90x90 px GIF shows at 198x198, or 170x170 above a bar with a name
row). A transparent margin in the GIF is cut off when the pack is made, so
the drawing is the pixels the GIF inks, at whichever size the GIF padded them
to. The pack comes from one of two places: the sprite room in flash, where a
Mac put it over BLE (Swapping the sprite over BLE, below), or the image, when
`CONFIG_BEACON_SPRITE_GIF="<absolute path>"` made the build convert a GIF and
embed the pack. At boot the room's pack plays when it is whole, else the
embedded one, else nothing until a pack arrives.

While nobody types it plays at three quarters of the GIF's own tempo and stops
only while the screen is off. While the keyboard is typed on it runs on the key
presses instead: every
press, any key, either half's on a split, queues eight visibly different
frames. A render
steps one of them while the queue holds one press's worth or less, and several
at a time (skipping frames, so the sprite runs faster) as presses pile up.
0.2 s after the last press arrived, the frames still queued are dropped and
the tempo resumes from the frame shown; the sprite never steps back. As that
0.2 s equals the advertising interval, a lone press shows three or four of its
frames before the tempo takes over. Every physical press counts once, at the
press, whether or not a hold-tap or a combo later holds it back or consumes it.

A pack with a back view (the pack section) shows it while a modifier is held
on the keyboard and the front view otherwise. The keyboard reports a
modifier once it is a hold: its keycode held 0.2 s (the broadcaster's fixed
confirm time, never the keymap's tapping term), or a non-modifier key pressed
while it is down. So a lone modifier key flips the sprite 0.2 s late. A
mod-tap held alone sends its keycode at ZMK's hold decision and flips it 0.2 s
after that (0.4 s after the press with ZMK's default `&mt`), or 0.2 s after
the press when it holds while undecided, which then also flips on a tap held
longer; a mod-tap whose hold another key decides flips it at once. A quick
Shift+letter flips the sprite for about half a second; a release flips it
back within an advertising interval or two. Each switch starts the other
view from its first frame. Both views are drawn at one
scale, the one at which the larger fills the space, so the switch changes
the drawing's size but not its pixels'. A keyboard that falls silent for a
second leaves the front view.

The sprite draws itself: the player inflates the frame's picture into a
packed canvas and scales it straight into the display buffer by nearest
neighbour (`src/sprite.c`), and the panel's SPI runs at 32 MHz. Redrawing a
sprite shown at about 2x takes 27-51 ms, flush included, and inflating a
picture of about a fifth of the panel's pixels 6-9 ms; a frame whose picture
is already on the canvas costs nothing, and catching up after a slow render
moves the frame index without decoding. 32 MHz is above the ST7789V data
sheet's write cycle; should the panel show noise, the fallback is 20 MHz
(16 MHz effective; Then, as you want them: the panel's clock).

Limits:

- Any GIF a browser plays (GIF87a or GIF89a, disposal 0-3, transparency,
  interlace, local colour tables), with at least one frame and at most 255
  colours in all, no larger than the sprite box once its transparent margin
  is cut off: 276x198, or 276x170 with the name row (a sprite name, or the
  receiver, which every build has unless its conf turns it off). A larger
  GIF stops the build with `static assertion failed:
  "CONFIG_BEACON_SPRITE_GIF is wider than the sprite box: ..."` or `"... is
  taller than the sprite box: ..."`. A GIF whose player (its canvas and
  dictionary: two bytes a pixel with more than 15 colours, one otherwise, plus
  8 KiB) does not fit the LVGL pool next to the screen stops it in CMake, and
  the message names the `CONFIG_LV_Z_MEM_POOL_SIZE` that would fit. The
  shield's pool grows from 48 KiB to 64 KiB when a sprite is configured: room
  for any GIF of the box at 15 colours, or about 200x140 px beyond that.
- Frame delays of 0 and 10 ms play as 100 ms, as browsers do (the converter
  writes them so).
- An embedded pack must fit the image's flash next to the code: about 150 KB
  with the receiver off (`CONFIG_BEACON_SPRITE_RECEIVE=n`), about 80 KB with
  it (the code partition is 356 KiB, the sprite room cut from it;
  `sprite_pack.py pack` prints a pack's bytes). One too large fails at the
  link, with ``region `FLASH' overflowed by N bytes``, not in CMake.
- The GIF is a personal file and never enters a repository: build locally with
  `./scripts/build.sh --sprite <gif>` (`firmware/prospector-sprite.uf2`), or in
  your own GitHub Actions run (A sprite image from GitHub Actions, below).
  `--sprite-name <text>` adds the name (15 bytes of UTF-8 at most, printable
  ASCII or katakana, without a double quote, a backslash or `??`); it names
  the GIF's subject, so it stays out of
  repositories like the GIF, and the script prints neither. CI and the release
  build without an embedded sprite or a name: that image shows the sprite
  received last, or none.
- A received pack has the same limits, checked when it is opened: one that
  exceeds the box or the pool is refused, and the report says so.

### Swapping the sprite over BLE

With `CONFIG_BEACON_SPRITE_RECEIVE` (on by default for the shield; `=n` in a
consumer's `prospector.conf` turns it off) the device advertises connectably
as `Prospector Dongle` about once a second and offers one GATT service through
which a central writes a sprite pack into the sprite room, a 384 KiB flash
partition the shield overlay cuts from the code partition; any pack up to
that size goes in. The new sprite plays as soon
as the transfer ends, named as the pack says, and plays again after a reboot
or a reflash (the UF2 bootloader leaves the room alone). During the transfer
the last picture stays on the screen; a transfer that fails, or a pack that
does not fit the box or the LVGL pool, brings the embedded sprite back, or no
sprite. The keyboard's status is not received while a central is connected,
and the screen greys only after a minute without it.

There is no pairing and no encryption: anyone in range can connect, write the
room and read the report (below), which gives the name of the sprite playing,
where it came from, and the last transfer since boot with the pack's size;
nothing else. A pack takes about a tenth of a second a KB from the first
command to the picture, about half of it erasing flash and the
rest transferring at 24 kB/s; a small pack shows within a second.

The protocol, for a sender (the sprite sender below is one), is the header
of `src/ble_receiver.c`: a control
characteristic takes BEGIN with the pack's length (the device erases the room
and answers), COMMIT after every 4 KiB written to the data characteristic
(the device writes the page to flash and answers), and END (the device checks
the pack and shows it, and its report says how it went); the control
characteristic's read is that report, with the name of the sprite playing.
The device asks the central for a 15 ms connection interval itself, a second
into the connection; a Mac applies it within another second, and a sender
that waits for it (the report shows the interval) transfers at full speed.

### The sprite pack

`tools/sprite_pack.py` turns a GIF, and optionally a second one for the back
view, into a sprite pack (`.spk`): the distinct pictures the GIF shows, packed
and deflated against the first, with the order and delays, the name, a length
and a CRC-32. It is what the player plays, embedded by the build from
`CONFIG_BEACON_SPRITE_GIF`, and the unit a sender writes over BLE;
[docs/sprite-pack.md](docs/sprite-pack.md) explains the format and what was
measured. Standard library Python 3.9 or
later:

```sh
python3 tools/sprite_pack.py pack --name <text> -o <out.spk> <front.gif> [<back.gif>]
python3 tools/sprite_pack.py check <pack.spk> <front.gif> [<back.gif>]   # every frame against the GIFs
python3 tools/sprite_pack.py info <pack.spk>
```

The GIF, the pack and the name are personal and stay out of repositories,
as above; the tool prints sizes and the name's alphabet only.

`tools/sprite_colours.py [--count N] <front.gif> [<back.gif>]` prints the
colours a sprite shows most, one line of five (or N) `RRGGBB` words: in each
GIF the pixels of each colour summed over the frames, each weighted by its
delay, transparent pixels left out and dark ones too (value under a fifth:
the outline, which the LEDs would light white) unless the sprite shows
nothing else; the GIFs (the sprite's views, the front first) scaled to one
total, the front then counted twice the back (the screen shows the back only
while a modifier is held), and added;
shades within a distance of a heavier colour merged into it, the heaviest
lively one (saturation and value at least a quarter) first and the rest
heaviest after it, cycling through them when the sprite shows fewer. The
key LEDs of a keyboard running
[zmk-lantern](https://github.com/akira-toriyama/zmk-lantern) take the line
as their palette, slot 0 first: its `tools/lantern_port.py palette` writes it
on the keyboard's port (zmk-lantern's README, The host); the docstring has
the numbers and why.

### The sprite sender

`tools/sprite-sender/` is the Mac side of the swap: a Swift app that picks the
next GIF of a collection, packs it (with its back view when the collection has
one) through `sprite_pack.py`, and writes the pack into the sprite room over
BLE. The collection is a directory of `<number>.gif` files; it goes through
them in number order, or through a range of the numbers only, one GIF a run,
around and around, once a day from a LaunchAgent, say.
The collection stays off every repository, and so does the state file, whose
number names the GIF:

```sh
cd tools/sprite-sender && ./package.sh          # SpriteSender.app, signed (its header)
SpriteSender.app/Contents/MacOS/sprite-sender daily --config <sender.json> [--dry-run]
SpriteSender.app/Contents/MacOS/sprite-sender send <pack.spk>   # one pack as it is
SpriteSender.app/Contents/MacOS/sprite-sender report            # the device's report
SpriteSender.app/Contents/MacOS/sprite-sender colours --config <sender.json> [--count N]
```

`sender.json` holds absolute paths, or paths relative to itself:
`{"front": "<dir>", "back": "<dir>", "state": "<file>", "range": "<first> ~ <last>", "names": "<csv>"}`
(`back`, `range` and `names` optional). A GIF of the collection is a file of
`front` named a positive integer as plain digits and `.gif`, exactly:
`<number>-<form>.gif`, any other name and the subdirectories stay out. Its back
view is the file of the same name in `back`; without one the pack holds the
front only. `range` keeps only those numbers: `"1 ~ 151"` goes from the first
to the 151st and back to the first, and `"100 ~"` runs from 100 to the last
number there is. Give a range when `front` holds more than the set you want:
without one every number is in the rotation, and in a directory of National
Pokédex GIFs not every number is a species. A state outside the range continues
at the next number in it, wrapping to the first.

The sprite name comes from `names`, PokeAPI's
`pokemon_species_names.csv`, with the GIF's number as the species' National
Pokédex number: each run, `tools/name_dictionary.py <csv> --number <n>` prints
the katakana name, composed (NFC), when it is 15 bytes of UTF-8 at most and
every character is in the katakana font (`sprite_pack.py`'s
`KATAKANA_RANGES`), else the English one when it is 15 bytes of printable
ASCII, whole, else nothing; an English name is first folded where the
collection's names need it and nowhere else (a gender sign at its end to a
space and `F` or `M`, a typographic apostrophe to `'`), and `--lang en` gives
the English name alone (the Actions route's default, below). With nothing, and
without the key, the name is the GIF's number. The log says which alphabet
(`name: katakana from the name dictionary` or `name: ASCII`), never the name,
its length or whether an ASCII name is the number; a CSV the config names but
that cannot be read or does not have the columns is a config error (exit 2).

The state file holds the number of the GIF tried last, written before the
transfer, so a GIF the device refuses costs one day; its directory is made
when missing, and a state that is no number starts the rotation over. Exit 0
when the device shows the pack, 1 when it does not (no device in range, a
config or collection that cannot be read, a refused pack: the next day tries
the next GIF), 2 for bad usage or config.

`colours` prints the colours of the GIF the state names, the sprite on
the device after a `daily` that succeeded, and of its back view when the
collection has one, weighed as `sprite_colours.py` (above) says, and sends
nothing; it exits 1 with no state, no readable front GIF, a config it cannot
read or the colour tool failing, 2 for bad usage or config. A LaunchAgent
that runs it after each successful swap and hands the line to the keyboard
is in zmk-lantern's README (The host: every morning).

No line the sender logs names a GIF, the sprite, the name's length or a GIF's
number: a number, or a rare length matched against the public list of names,
names the subject. Lines still tell something of the GIF: the name's alphabet
(the `name:` line, the device's report's `name=`) says whether the katakana
name fit the HP bar, which holds five katakana, so with a complete CSV `ASCII`
is a name of six or more, a length, and a run of such days can place the GIF;
the pack's size (the `pack:`, `start` and `end answered:` lines, the erase
time of `begin answered`, the report's `expected` and `received`) can name the
GIF to someone who makes packs from the same collection. So keep the sender's
lines out of anything public.

macOS asks once for Bluetooth; `package.sh` signs the app with a persistent
self-signed identity when the login keychain has one (`chord-dev` by
default, `SIGN_IDENTITY=<name>` for another; its header), so that the answer
survives a rebuild, and without one it signs ad hoc, and macOS asks again
after every build. A pack shows about a tenth of a second a KB after BEGIN.

### A sprite image from GitHub Actions

Without a Mac or Docker, a sprite image comes out of a GitHub Actions run:
the reusable workflow `.github/workflows/prospector-sprite.yml` of this
repository fetches one GIF and its name and builds `prospector-sprite.uf2`
with them embedded, in the caller's run, where alone the picture exists (this
repository's CI and release never carry one, and never call it). Three steps,
from the template repository
[prospector-sprite-template](https://github.com/akira-toriyama/prospector-sprite-template):

1. Make a repository from the template ("Use this template"), **private**: the
   artifact of a public repository's run can be downloaded by anyone, and it
   holds the picture.
2. Run its `prospector-sprite` workflow (Actions, "Run workflow") with the
   sprite's number, and `en` or `ja` for the name. About four minutes.
3. Download the run's artifact `prospector-sprite` and flash the
   `prospector-sprite.uf2` in it (The least, above, step 4). Another sprite
   is another run; the artifact stays for GitHub's 90 days.

The inputs and their defaults are `tools/sprite_fetch.py`'s docstring. The
number is the file `<number>.gif` of a directory of the sprites repository at a
fixed commit, and the species row of PokeAPI's `pokemon_species_names.csv` at
another, each fetched from raw.githubusercontent.com in one request; the
defaults are PokeAPI's two repositories at the commits this module was tried
with, and another repository (a mirror, a fork, a private copy with the
`sprites_token` secret) needs its commit given too. The name is the CSV's for
the number: with `en` the English name, with `ja` the katakana name when the
HP bar shows it, else the English one (The sprite sender, above, for the fold
and the limits); the `name` input replaces it, and with none that fits, the
number. The pictures are yours to fetch for your own device: nothing of them
enters this repository, the template or any release.

The image is for a device that gets its sprites this way. It has no receiver
(`CONFIG_BEACON_SPRITE_RECEIVE=n`): it neither advertises nor connects, so the
sprite sender cannot reach it, the sprite room's pack is left alone and the
embedded one plays, and the pack may take the flash the receiver would
(Limits, above: about 150 KB). Its screen never turns off
(`CONFIG_BEACON_SCREEN_OFF_AFTER_S=0`), keyboard heard or not, so the sprite
shows before the broadcaster is set up on the keyboard (The least, above). The
`extra_conf` input appends Kconfig lines to its `prospector.conf`
(`CONFIG_BEACON_SCREEN_OFF_AFTER_S=300` for the five minutes, say). The
firmware is built from ZMK `main` and `zmkfirmware/zmk-build-arm:stable` as
they are at the time of the run: the picture and its name are pinned by the
inputs' commits, the rest moves with ZMK.

A private config repository of your own calls the same workflow from a job
of its own, the workflow pinned by a commit of this repository (in a public
one anyone can download the run's artifact, which holds the picture: step
1 above):

```yaml
on:
  workflow_dispatch:
    inputs:
      number:
        description: "The sprite's number"
        required: true
jobs:
  sprite:
    uses: akira-toriyama/zmk-beacon/.github/workflows/prospector-sprite.yml@<commit sha>
    with:
      number: ${{ inputs.number }}
```

It builds with this repository's own manifest, never the caller's: the
caller's `build.yaml`, ZMK pin and `config/` stay out of it. A repository
whose settings require every action to be pinned by SHA may refuse the tag
references inside the reusable (`actions/checkout@v7`); untried.

A GIF that does not fit fails the run in one of three places, the log naming
sizes and never the picture: CMake (`the GIF does not convert to a sprite
pack`, or `needs about N bytes of the LVGL pool`), the compiler (`static
assertion failed: "CONFIG_BEACON_SPRITE_GIF is wider than the sprite box`, or
taller) or the linker (``region `FLASH' overflowed``; Limits, above).

## What the module provides

| Path | What |
| --- | --- |
| `boards/shields/prospector/` | The shield: ST7789V panel over SPI3, PWM backlight on D6 (P1.11), a dummy kscan (ZMK needs one), one USB CDC ACM port and no HID device, with the status screen on (`CONFIG_BEACON_SCREEN`). `prospector.conf` holds the defaults a consumer can override. |
| `src/status_observer.c` | The BLE observer. It brings Bluetooth up itself (`CONFIG_ZMK_BLE=n` in the shield, so ZMK never advertises), scans passively without a duplicate filter, and reads each half's battery, the key press count and the modifiers held from the status payload. The scan pauses while the sprite receiver has a central connected. |
| `src/ble_receiver.c` | `CONFIG_BEACON_SPRITE_RECEIVE`: advertises connectably, takes a sprite pack over one GATT service into the sprite room (the shield overlay's `sprite_partition`, 384 KiB), shows it, and gives it back at boot. The protocol is its header. |
| `src/status_broadcaster.c` | `CONFIG_BEACON_STATUS_BROADCAST`: on the keyboard's central, a split's or the keyboard itself when it is not a split, sends the status payload as manufacturer data on a second, legacy, non-connectable advertising set next to ZMK's own, every 200 ms; a key press, a modifier confirmed as held (held 0.2 s, the broadcaster's fixed confirm time, or a non-modifier key pressed under it) and its release refresh the payload for the next advertising event. |
| `src/status_payload.h` | The payload both sides share: 26 bytes at prospector-zmk-module v2.2.3's offsets, of which the battery bytes, the active layer's index and name, the key press counter and the modifier byte are used. |
| `src/prospector_screen.c` | `CONFIG_BEACON_SCREEN`: ZMK custom status screen (LVGL 9), the HP bar along the bottom and the sprite above it, fed the observer's state every 500 ms; checks at compile time that an embedded GIF fits. |
| `src/hp_bar.c`, `src/hp_bar.h` | The HP bar: `HP`, the bar with its value and, with the name row, the playing sprite's name on the row above; and the mapping of the payload's two batteries to its one level. |
| `src/sprite.c`, `src/sprite_pack.c` | `CONFIG_BEACON_SPRITE`: the sprite pack player, with a tempo, eight frames per key press, one invalidation per changed picture, an endless loop, the back view while a modifier is held, and its own nearest-neighbour draw into the display buffer; it plays the room's pack, the embedded one or a received one, and names the HP bar. The pack's reader and inflater are plain C that also compiles on the host. |
| `src/bootloader_on_1200_baud.c` | `CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD`: setting the serial port to 1200 baud reboots the device into its UF2 bootloader. On by default for the shield; a keyboard's central turns it on in its conf (Then, as you want them). |
| `src/screen_dump.c` | `CONFIG_BEACON_SCREEN_DUMP`: setting the serial port to 2400 baud makes the device render its screen once more and send it over the port, band by band, with a CRC-32. On by default for the shield. |
| `src/screen_off.c` | `CONFIG_BEACON_SCREEN_OFF_AFTER_S`: fades the backlight out and blanks the panel once no key press has arrived for that long, and lights both at the next key press. The screen keeps being drawn while it is dark; only the sprite stands still. |
| `src/backlight.c` | Lights the backlight (`CONFIG_BEACON_BACKLIGHT_BRIGHTNESS`) at boot, before LVGL and ZMK's display start, and sets it for `src/screen_off.c`. |
| `Kconfig` | The `BEACON_*` options: `BEACON_BACKLIGHT_BRIGHTNESS`, `BEACON_BOOTLOADER_ON_1200_BAUD`, `BEACON_OWN_BATTERY`, `BEACON_SCREEN`, `BEACON_SCREEN_DUMP`, `BEACON_SCREEN_OFF_AFTER_S`, `BEACON_SPRITE_GIF`, `BEACON_SPRITE_NAME`, `BEACON_SPRITE_RECEIVE`, `BEACON_STATUS_BROADCAST`. |

## Building this repository

```sh
./scripts/build.sh              # every target in build.yaml: firmware/prospector.uf2, corne_left.uf2 and reviung41.uf2
./scripts/build.sh prospector   # one
./scripts/build.sh --logging    # firmware/prospector-logging.uf2: the log on the serial port
./scripts/build.sh --sprite ~/a.gif --sprite-name "A"  # firmware/prospector-sprite.uf2 (local only)
./scripts/build.sh prospector --kconfig CONFIG_LV_USE_SYSMON=y --kconfig CONFIG_LV_USE_PERF_MONITOR=y --tag perf  # firmware/prospector-kconfig-perf.uf2
./scripts/build.sh --update     # refresh zmk@main and its modules first
./scripts/build.sh --help       # every option
```

Docker is required (`zmkfirmware/zmk-build-arm:stable`). The west workspace
lives in `~/.cache/zmk-beacon`; the first run downloads ZMK and its modules.
Each run ends with the images' sha256, flash and RAM use, and the revisions
they were built from. `--logging` and `--sprite` build the `prospector`
shield only: without a target the run takes build.yaml's `prospector`, and a
run that names another target stops.

The images are build checks as much as firmware: `prospector.uf2`, the
Prospector Dongle's image and the one the release attaches; `corne_left.uf2`,
the broadcaster on ZMK's stock split central, the Corne's left half on the
nice!nano, with the three lines of The least
([config/corne_left.conf](config/corne_left.conf)); and `reviung41.uf2`, the
broadcaster on ZMK's stock Reviung41 on the nice!nano, a keyboard that is
not a split, with the first two
([config/reviung41.conf](config/reviung41.conf)). The last two are built so
that a change which breaks the broadcaster on stock ZMK fails here (CI also
checks that every `CONFIG_` line of `config/<shield>.conf` took, since Zephyr
only warns about one that does not), never attached to a release, never run
on hardware. CI
builds every `build.yaml` target on pull requests and on `main`, and once a
week against the current ZMK `main`, nothing patched. Every push to `main`
refreshes one rolling draft release with `prospector.uf2` attached, and it
alone; publishing the draft by hand creates the tag.

The one keyboard this module has run on is the Cyboard Imprint of
[akira-toriyama/canon](https://github.com/akira-toriyama/canon), through its
Imprint Dongle, the split central whose config carries the real conf and pins
this module by commit; what was seen on its hardware, and when, is in
[CLAUDE.md](CLAUDE.md).

## Credits and license

MIT ([LICENSE](LICENSE)). The overlay, the panel parameters and the backlight
initialization derive from
[t-ogura/prospector-zmk-module](https://github.com/t-ogura/prospector-zmk-module)
(MIT), itself derived from
[carrefinho's Prospector](https://github.com/carrefinho/prospector-zmk-module).
The katakana font (`src/font_pkmn.c`, generated by `tools/pkmn_font.py`) is
PKMN Strict from [nue-of-k/pkmn](https://github.com/nue-of-k/pkmn) (MIT,
Copyright (c) 2009-2015 Nue); the file carries its notice.
