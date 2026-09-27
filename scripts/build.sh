#!/usr/bin/env bash
#
# Build the firmware in Docker (zmkfirmware/zmk-build-arm:stable, the image
# ZMK's own CI uses).
#
#   ./scripts/build.sh                  # every target in build.yaml
#   ./scripts/build.sh prospector       # one shield (its board comes from build.yaml)
#   ./scripts/build.sh --logging        # CONFIG_ZMK_USB_LOGGING=y variants (<shield>-logging.uf2)
#   ./scripts/build.sh --sprite <gif>   # embed a GIF sprite (<shield>-sprite[-logging].uf2); local builds only
#   ./scripts/build.sh --update         # force west update (refresh zmk@main and its modules)
#   ./scripts/build.sh --clean          # delete the workspace and exit
#
# Workspace layout under $ZMK_WS (default ~/.cache/zmk-beacon), persistent
# across runs so west update happens once:
#   ws/       west topdir: ws/config (a copy of this repo's config/), .west/,
#             zmk/, zephyr/, modules/, build/<shield>/
#   module/   this repository, synced with rsync (the Zephyr module root)
#   sprite/   the GIF of the current --sprite run, as sprite.gif
#   output/   the .uf2 files of the last run
# The topdir stays outside the module root on purpose (config/west.yml). The
# same build runs in CI (.github/workflows/zmk-build.yml); keep the west build
# arguments identical.
#
# --sprite copies the GIF into the workspace (never into the repository: sprite
# GIFs are personal files, .gitignore) and passes it to the build as
# CONFIG_BEACON_SPRITE_GIF. CI and the release never build with a sprite.
#
# Output: ./firmware/<shield>[-sprite][-logging].uf2 (gitignored). Only the
# targets built in this run are copied, so a stale file never shadows a fresh
# one.
#
# Environment: ZMK_WS (workspace), ZMK_IMAGE (build image).
set -euo pipefail

# A relative --sprite path is the caller's, so keep the caller's directory.
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

SHIELDS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --clean)   echo "removing workspace: $WS"; rm -rf "$WS"; exit 0 ;;
    --update)  FORCE_UPDATE=1 ;;
    --logging) LOGGING=1 ;;
    --sprite)
      if [ $# -lt 2 ] || [ -z "$2" ]; then echo "--sprite needs a GIF path" >&2; exit 2; fi
      SPRITE="$2"; shift ;;
    -h|--help) awk 'NR>1 && /^#/{sub(/^# ?/,"");print;next} NR>1{exit}' "${BASH_SOURCE[0]}"; exit 0 ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
    *)  SHIELDS+=("$1") ;;
  esac
  shift
done

case "$SPRITE" in "" | /*) ;; *) SPRITE="$CALLER_DIR/$SPRITE" ;; esac
if [ -n "$SPRITE" ] && [ ! -r "$SPRITE" ]; then
  echo "--sprite: cannot read $SPRITE" >&2
  exit 1
fi

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
    if [ -z "$board" ]; then
      echo "shield '$arg' is not in build.yaml (pass board:shield to build it anyway)" >&2
      exit 1
    fi
    TARGETS+=("$board	$arg")
  done
fi
if [ ${#TARGETS[@]} -eq 0 ]; then
  echo "no build targets (check build.yaml)" >&2; exit 1
fi

if ! docker info >/dev/null 2>&1; then
  echo "the Docker daemon is not running (open -a Docker)" >&2
  exit 1
fi

mkdir -p "$TOP/config" "$MOD"
# `/.git` without a trailing slash: in a git worktree .git is a file.
rsync -a --delete --exclude '/.git' --exclude '/.claude/' --exclude '/firmware/' "$REPO"/ "$MOD"/
rsync -a --delete "$REPO"/config/ "$TOP"/config/

# A fixed name inside the container: the path goes through an unquoted word
# list below, and the GIF's own name may hold spaces.
SPRITE_IN_CONTAINER=""
rm -rf "$WS/sprite"
if [ -n "$SPRITE" ]; then
  mkdir -p "$WS/sprite"
  cp "$SPRITE" "$WS/sprite/sprite.gif"
  SPRITE_IN_CONTAINER="/workspace/sprite/sprite.gif"
fi

NEED_UPDATE=0
[ ! -d "$TOP/.west" ]     && NEED_UPDATE=1
[ ! -d "$TOP/zmk/app" ]   && NEED_UPDATE=1
[ "$FORCE_UPDATE" -eq 1 ] && NEED_UPDATE=1

SUFFIX=""
[ -n "$SPRITE" ] && SUFFIX="-sprite"
[ "$LOGGING" -eq 1 ] && SUFFIX="$SUFFIX-logging"
TARGET_LIST=""
OUTPUTS=()
for row in "${TARGETS[@]}"; do
  TARGET_LIST+="${row%%	*}:${row##*	} "
  OUTPUTS+=("${row##*	}$SUFFIX.uf2")
done

echo "workspace   : $WS"
echo "image       : $IMAGE"
echo "west update : $([ "$NEED_UPDATE" -eq 1 ] && echo yes || echo 'no (cached)')"
echo "targets     : $TARGET_LIST"
[ "$LOGGING" -eq 1 ] && echo "logging     : CONFIG_ZMK_USB_LOGGING=y"
# Not the source path: a GIF's file name usually names its subject.
[ -n "$SPRITE" ] && echo "sprite      : $(wc -c <"$SPRITE" | tr -d ' ') byte GIF (copied to $WS/sprite/sprite.gif)"

docker run --rm \
  -v "$WS:/workspace" \
  -w /workspace/ws \
  -e ZEPHYR_BASE=/workspace/ws/zephyr \
  -e NEED_UPDATE="$NEED_UPDATE" \
  -e TARGETS="$TARGET_LIST" \
  -e LOGGING="$LOGGING" \
  -e SPRITE="$SPRITE_IN_CONTAINER" \
  "$IMAGE" bash -c '
set -euo pipefail
git config --global --add safe.directory "*"
if [ "$NEED_UPDATE" -eq 1 ]; then
  echo "=== west init/update ==="
  [ -d .west ] || west init -l config
  west update
fi
west zephyr-export
mkdir -p /workspace/output
for t in $TARGETS; do
  BOARD="${t%%:*}"; SH="${t##*:}"
  EXTRA=""; SUFFIX=""
  # Kconfig strings keep their quotes through the shell: -DCONFIG_X="value".
  if [ -n "$SPRITE" ]; then EXTRA="-DCONFIG_BEACON_SPRITE_GIF=\"$SPRITE\""; SUFFIX="-sprite"; fi
  if [ "$LOGGING" = "1" ]; then EXTRA="$EXTRA -DCONFIG_ZMK_USB_LOGGING=y"; SUFFIX="$SUFFIX-logging"; fi
  echo "=== BUILD $BOARD / $SH$SUFFIX ==="
  # shellcheck disable=SC2086
  west build -p -s zmk/app -d "build/$SH$SUFFIX" -b "$BOARD" -- \
    -DSHIELD="$SH" -DZMK_CONFIG=/workspace/ws/config -DZMK_EXTRA_MODULES=/workspace/module $EXTRA
  cp "build/$SH$SUFFIX/zephyr/zmk.uf2" "/workspace/output/$SH$SUFFIX.uf2"
done
'

mkdir -p "$REPO/firmware"
for f in "${OUTPUTS[@]}"; do
  cp "$WS/output/$f" "$REPO/firmware/$f"
done
echo
echo "done:"
for f in "${OUTPUTS[@]}"; do ls -lh "$REPO/firmware/$f"; done
