#!/usr/bin/env bash
#
# Build this repository's firmware in Docker (zmkfirmware/zmk-build-arm:stable,
# the image ZMK's own CI uses) and copy the images to ./firmware/.
#
#   ./scripts/build.sh                    # every target in build.yaml
#   ./scripts/build.sh prospector         # one shield; its board comes from build.yaml
#   ./scripts/build.sh <board>:<shield>   # a pair that build.yaml does not list
#   ./scripts/build.sh --logging
#   ./scripts/build.sh --sprite <gif> --sprite-name <text>
#   ./scripts/build.sh --kconfig CONFIG_LV_USE_SYSMON=y \
#     --kconfig CONFIG_LV_USE_PERF_MONITOR=y --tag perf
#   ./scripts/build.sh --update           # west update first
#   ./scripts/build.sh --clean            # delete the workspace and exit
#
# Options (local builds only: CI and releases build the plain image):
#   --logging         USB CDC logging: CONFIG_ZMK_USB_LOGGING=y, a 4 KiB CDC ring
#                     buffer that holds the boot log until the host opens the
#                     port (ZMK's 1 KiB default keeps about 1 KB of it), and ZMK
#                     at INFO level (CONFIG_ZMK_LOGGING_MINIMAL=y; this module's
#                     own lines are INFO either way). For ZMK's DEBUG add
#                     --kconfig CONFIG_ZMK_LOGGING_MINIMAL=n.
#   --sprite <gif>    embed a GIF sprite (CONFIG_BEACON_SPRITE_GIF). Sprite GIFs
#                     are personal files: copied into the workspace only, never
#                     into a repository, CI or a release, and never printed by
#                     path.
#   --sprite-name <text>
#                     the sprite's name under the HP bar (CONFIG_BEACON_SPRITE_NAME,
#                     needs --sprite): printable ASCII without a double quote, a
#                     backslash or "??". It names the GIF's subject, so only its
#                     length is printed.
#   --kconfig CONFIG_NAME=VALUE
#                     one more Kconfig line, merged after the sprite and logging
#                     lines. Repeatable. CONFIG_BEACON_SPRITE_* go only through
#                     --sprite and --sprite-name.
#   --tag <name>      appended to the build directory and the image name.
#   --update          west update before building: moves zmk@main and its modules.
#   --clean           delete the workspace and exit.
#
# Images: firmware/<shield>[-sprite][-logging][-<tag>].uf2 (git-ignored), copied
# once every target of the run has built. The run ends with one line per image
# (sha256, FLASH and RAM use) and the revisions it built from.
#
# Workspace under $ZMK_WS (default ~/.cache/zmk-beacon), kept between runs so
# that west update runs once:
#   ws/       west topdir: ws/config (a copy of this repository's config/),
#             .west/, zmk/, zephyr/, modules/, and build/<image name>/ with
#             build.log, zephyr/.config, zephyr/zephyr.dts and zephyr/zmk.map
#   module/   this repository's working tree, the Zephyr module root
#   sprite/   this run's GIF as sprite.gif and its Kconfig fragment sprite.conf
#   kconfig/  this run's logging.conf (--logging) and extra.conf (--kconfig)
#   output/   the images of the last run, emptied at its start
# The topdir stays outside the module root on purpose (config/west.yml). CI runs
# the same west build (.github/workflows/zmk-build.yml); keep the arguments they
# share identical.
#
# Environment: ZMK_WS (workspace), ZMK_IMAGE (build image).
set -euo pipefail

# A relative --sprite path is the caller's.
CALLER_DIR="$PWD"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

WS="${ZMK_WS:-$HOME/.cache/zmk-beacon}"
IMAGE="${ZMK_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
TOP="$WS/ws"
MOD="$WS/module"
FORCE_UPDATE=0
LOGGING=0
SPRITE=""
SPRITE_NAME=""
TAG=""
KCONFIG=()
SHIELDS=()

# Under CONFIG_ZMK_USB_LOGGING ZMK defaults to a 1 KiB CDC ring and ZMK at DEBUG
# (zmk app/Kconfig). With 1 KiB a capture started after boot ended after about
# 1 KB of the boot log; with 4 KiB a port opened 70 s after boot returned all of
# it (hardware 2026-09-29). The ring is allocated twice (RX and TX, Zephyr
# cdc_acm.c): 6 KiB more RAM.
LOGGING_CONF=(
  CONFIG_ZMK_USB_LOGGING=y
  CONFIG_USB_CDC_ACM_RINGBUF_SIZE=4096
  CONFIG_ZMK_LOGGING_MINIMAL=y
)

die() {
  local rc=$1
  shift
  echo "build.sh: $*" >&2
  exit "$rc"
}

while [ $# -gt 0 ]; do
  arg="$1"
  shift
  case "$arg" in
    --clean)
      echo "removing the workspace $WS"
      rm -rf "$WS"
      exit 0
      ;;
    --update) FORCE_UPDATE=1 ;;
    --logging) LOGGING=1 ;;
    --sprite | --sprite-name | --kconfig | --tag)
      if [ $# -eq 0 ] || [ -z "$1" ]; then die 2 "$arg needs a value"; fi
      case "$arg" in
        --sprite) SPRITE="$1" ;;
        --sprite-name) SPRITE_NAME="$1" ;;
        --kconfig) KCONFIG+=("$1") ;;
        --tag) TAG="$1" ;;
      esac
      shift
      ;;
    -h | --help)
      awk 'NR>1 && /^#/{sub(/^# ?/,""); print; next} NR>1{exit}' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    # A value glued to an option, or a stray argument, can be the GIF's path
    # or the name: printed only up to a '=' or a space, or not at all.
    -*[=\ ]*) die 2 "unknown option ${arg%%[= ]*}... (an option's value is the next argument; see --help)" ;;
    -*) die 2 "unknown option $arg (see --help)" ;;
    *:*) SHIELDS+=("$arg") ;;
    *[!A-Za-z0-9_-]*) die 2 "an argument that is no shield name (not printed; a GIF goes after --sprite)" ;;
    *) SHIELDS+=("$arg") ;;
  esac
done

if [ -n "$SPRITE" ]; then
  case "$SPRITE" in /*) ;; *) SPRITE="$CALLER_DIR/$SPRITE" ;; esac
  # Never the path in a message: a GIF's file name names its subject.
  if [ ! -f "$SPRITE" ] || [ ! -r "$SPRITE" ]; then die 2 "--sprite: no readable GIF at that path"; fi
fi
if [ -n "$SPRITE_NAME" ] && [ -z "$SPRITE" ]; then die 2 "--sprite-name needs --sprite"; fi
# What Kconfig takes unescaped, the C preprocessor leaves alone (a trigraph
# "??x" would change in autoconf.h) and the HP bar's font can draw (Kconfig
# help). grep reads lines, so a newline is checked apart.
case "$SPRITE_NAME" in *$'\n'*) die 2 "--sprite-name: one line" ;; esac
if printf '%s' "$SPRITE_NAME" | LC_ALL=C grep -Eq '[^ -~]|["\\]|\?\?'; then
  die 2 "--sprite-name: printable ASCII only, without a double quote, a backslash or \"??\""
fi
# The messages name the symbol at most, never a value.
for kv in ${KCONFIG[@]+"${KCONFIG[@]}"}; do
  case "$kv" in
    *$'\n'*) die 2 "--kconfig takes one line" ;;
    CONFIG_BEACON_SPRITE_*) die 2 "--kconfig: CONFIG_BEACON_SPRITE_* go only through --sprite and --sprite-name" ;;
    CONFIG_?*=?*) ;;
    *) die 2 "--kconfig takes CONFIG_NAME=VALUE, not ${kv%%=*}..." ;;
  esac
  case "${kv%%=*}" in *[!A-Za-z0-9_]*) die 2 "--kconfig: not a Kconfig symbol: ${kv%%=*}" ;; esac
done
case "$TAG" in *[!A-Za-z0-9._-]*) die 2 "--tag takes letters, digits, '.', '_' and '-'" ;; esac

# "board<TAB>shield" per build.yaml include: entry (board/shield in either
# order, comment lines skipped). The same awk derives the CI matrix in
# .github/workflows/zmk-build.yml; change both together.
_build_pairs() {
  awk '
    /^[[:space:]]*#/ { next }
    /^[[:space:]]*-[[:space:]]/ { if (b != "") print b "\t" s; b=""; s="" }
    /^[[:space:]]*(-[[:space:]]*)?board:[[:space:]]/  { t=$0; sub(/.*board:[[:space:]]*/,  "", t); b=t }
    /^[[:space:]]*(-[[:space:]]*)?shield:[[:space:]]/ { t=$0; sub(/.*shield:[[:space:]]*/, "", t); s=t }
    END { if (b != "") print b "\t" s }
  ' build.yaml
}

TARGETS=()
if [ ${#SHIELDS[@]} -eq 0 ]; then
  while IFS= read -r line; do TARGETS+=("$line"); done < <(_build_pairs)
else
  for arg in "${SHIELDS[@]}"; do
    if [[ "$arg" == *:* ]]; then
      TARGETS+=("${arg%%:*}	${arg##*:}")
      continue
    fi
    board=""
    while IFS="$(printf '\t')" read -r b s; do
      if [ "$s" = "$arg" ]; then board="$b"; break; fi
    done < <(_build_pairs)
    [ -n "$board" ] || die 1 "shield $arg is not in build.yaml (pass <board>:<shield> to build it anyway)"
    TARGETS+=("$board	$arg")
  done
fi
[ ${#TARGETS[@]} -gt 0 ] || die 1 "no build targets (check build.yaml)"

# The one place that names the images: <shield>$SUFFIX is the build directory
# and the image, in the container and here. Kconfig fragments merge in list
# order after the shield conf and config/<shield>.conf, and -DCONFIG_* would
# merge after every fragment: --logging is a fragment so that --kconfig can
# still override it. Container paths: $WS is /workspace there.
SUFFIX=""
FRAGMENTS=()
if [ -n "$SPRITE" ]; then
  SUFFIX="$SUFFIX-sprite"
  FRAGMENTS+=(/workspace/sprite/sprite.conf)
fi
if [ "$LOGGING" -eq 1 ]; then
  SUFFIX="$SUFFIX-logging"
  FRAGMENTS+=(/workspace/kconfig/logging.conf)
fi
if [ ${#KCONFIG[@]} -gt 0 ]; then FRAGMENTS+=(/workspace/kconfig/extra.conf); fi
if [ -n "$TAG" ]; then SUFFIX="$SUFFIX-$TAG"; fi
FRAGMENT_LIST=""
if [ ${#FRAGMENTS[@]} -gt 0 ]; then FRAGMENT_LIST="$(IFS=';' && echo "${FRAGMENTS[*]}")"; fi

if ! docker info >/dev/null 2>&1; then
  die 1 "the Docker daemon is not running (open -a Docker)"
fi

mkdir -p "$TOP/config" "$MOD"
# `/.git` without a trailing slash: in a git worktree .git is a file.
rsync -a --delete --exclude '/.git' --exclude '/.claude/' --exclude '/firmware/' "$REPO"/ "$MOD"/
rsync -a --delete "$REPO"/config/ "$TOP"/config/

# The per-run inputs, written below only for the options of this run. The
# container sees only the workspace, so the GIF goes in under a fixed name, and
# its path and name reach the build in a Kconfig fragment, never as
# -DCONFIG_...: west prints the whole cmake command line when the configure
# step fails, and the name names the subject. Kconfig strings keep their quotes.
rm -rf "$WS/sprite" "$WS/kconfig"
if [ -n "$SPRITE" ]; then
  mkdir -p "$WS/sprite"
  cp "$SPRITE" "$WS/sprite/sprite.gif" 2>/dev/null || die 1 "--sprite: cannot copy the GIF into the workspace"
  {
    echo 'CONFIG_BEACON_SPRITE_GIF="/workspace/sprite/sprite.gif"'
    if [ -n "$SPRITE_NAME" ]; then echo "CONFIG_BEACON_SPRITE_NAME=\"$SPRITE_NAME\""; fi
  } >"$WS/sprite/sprite.conf"
fi
if [ "$LOGGING" -eq 1 ]; then
  mkdir -p "$WS/kconfig"
  printf '%s\n' "${LOGGING_CONF[@]}" >"$WS/kconfig/logging.conf"
fi
if [ ${#KCONFIG[@]} -gt 0 ]; then
  mkdir -p "$WS/kconfig"
  printf '%s\n' "${KCONFIG[@]}" >"$WS/kconfig/extra.conf"
fi

NEED_UPDATE=0
if [ ! -d "$TOP/.west" ] || [ ! -d "$TOP/zmk/app" ] || [ "$FORCE_UPDATE" -eq 1 ]; then
  NEED_UPDATE=1
fi

TARGET_LIST=""
for row in "${TARGETS[@]}"; do TARGET_LIST+="${row%%	*}:${row##*	} "; done

echo "workspace   : $WS"
echo "image       : $IMAGE"
echo "west update : $([ "$NEED_UPDATE" -eq 1 ] && echo yes || echo 'no (cached; --update forces it)')"
if [ "$LOGGING" -eq 1 ]; then echo "logging     : ${LOGGING_CONF[*]}"; fi
# Neither the GIF's path nor the name: both name the subject.
if [ -n "$SPRITE" ]; then echo "sprite      : a $(wc -c <"$SPRITE" | tr -d ' ') byte GIF"; fi
if [ -n "$SPRITE_NAME" ]; then echo "sprite name : ${#SPRITE_NAME} characters"; fi
if [ ${#KCONFIG[@]} -gt 0 ]; then echo "kconfig     : ${KCONFIG[*]}"; fi
echo "targets     :"
for row in "${TARGETS[@]}"; do echo "  ${row%%	*} / ${row##*	} -> firmware/${row##*	}$SUFFIX.uf2"; done

docker run --rm \
  -v "$WS:/workspace" \
  -w /workspace/ws \
  -e ZEPHYR_BASE=/workspace/ws/zephyr \
  -e NEED_UPDATE="$NEED_UPDATE" \
  -e TARGETS="$TARGET_LIST" \
  -e SUFFIX="$SUFFIX" \
  -e FRAGMENTS="$FRAGMENT_LIST" \
  "$IMAGE" bash -c '
set -euo pipefail
git config --global --add safe.directory "*"
if [ "$NEED_UPDATE" -eq 1 ]; then
  echo "=== west init/update ==="
  [ -d .west ] || west init -l config
  west update
fi
west zephyr-export
rm -rf /workspace/output
mkdir -p /workspace/output
# FRAGMENTS, not EXTRA_CONF_FILE, by name: Zephyr also reads that one from the
# environment (zephyr_get()).
EXTRA=()
if [ -n "$FRAGMENTS" ]; then EXTRA+=("-DEXTRA_CONF_FILE=$FRAGMENTS"); fi
for t in $TARGETS; do
  BOARD="${t%%:*}"; SH="${t##*:}"; DIR="build/$SH$SUFFIX"
  echo "=== BUILD $BOARD / $SH$SUFFIX ==="
  # Emptied first: the pristine step (-p) of an existing build would delete
  # build.log under tee. An empty directory is no build to clean.
  rm -rf "$DIR"
  mkdir -p "$DIR"
  west build -p -s zmk/app -d "$DIR" -b "$BOARD" -- \
    -DSHIELD="$SH" -DZMK_CONFIG=/workspace/ws/config -DZMK_EXTRA_MODULES=/workspace/module \
    "${EXTRA[@]}" 2>&1 | tee "$DIR/build.log"
  cp "$DIR/zephyr/zmk.uf2" "/workspace/output/$SH$SUFFIX.uf2"
done
'

mkdir -p "$REPO/firmware"
for row in "${TARGETS[@]}"; do
  name="${row##*	}$SUFFIX"
  cp "$WS/output/$name.uf2" "$REPO/firmware/$name.uf2"
done

# FLASH and RAM from the linker's memory table in build.log; left out rather
# than failing the run when the table is missing.
echo
echo "images:"
for row in "${TARGETS[@]}"; do
  name="${row##*	}$SUFFIX"
  sha="$(shasum -a 256 "$REPO/firmware/$name.uf2" | cut -c1-12)"
  mem="$(awk '
    $1 == "FLASH:" { f = "FLASH " $2 " " $3 " / " $4 " " $5 " " $6 }
    $1 == "RAM:"   { r = "RAM " $2 " " $3 " / " $4 " " $5 " " $6 }
    END { print f (f != "" && r != "" ? "  " : "") r }
  ' "$TOP/build/$name/build.log" 2>/dev/null || true)"
  echo "  firmware/$name.uf2  $sha  $mem"
done
zmk_rev="$(git -C "$TOP/zmk" log -1 --format='%h %cs' 2>/dev/null || true)"
beacon_rev="$(git -C "$REPO" describe --always --dirty 2>/dev/null || true)"
echo "revisions: zmk ${zmk_rev:-?}, zmk-beacon ${beacon_rev:-?}"
