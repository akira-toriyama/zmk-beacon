#!/usr/bin/env bash
#
# Build the firmware in Docker (zmkfirmware/zmk-build-arm:stable, the image
# ZMK's own CI uses).
#
#   ./scripts/build.sh                  # every target in build.yaml
#   ./scripts/build.sh prospector       # one shield (its board comes from build.yaml)
#   ./scripts/build.sh --logging        # CONFIG_ZMK_USB_LOGGING=y variants (<shield>-logging.uf2)
#   ./scripts/build.sh --update         # force west update (refresh zmk@main and its modules)
#   ./scripts/build.sh --clean          # delete the workspace and exit
#
# Workspace layout under $ZMK_WS (default ~/.cache/zmk-beacon), persistent
# across runs so west update happens once:
#   ws/       west topdir: ws/config (a copy of this repo's config/), .west/,
#             zmk/, zephyr/, modules/, build/<shield>/
#   module/   this repository, synced with rsync (the Zephyr module root)
#   output/   the .uf2 files of the last run
# The topdir stays outside the module root on purpose (config/west.yml). The
# same build runs in CI (.github/workflows/zmk-build.yml); keep the west build
# arguments identical.
#
# Output: ./firmware/<shield>[-logging].uf2 (gitignored). Only the targets built
# in this run are copied, so a stale file never shadows a fresh one.
#
# Environment: ZMK_WS (workspace), ZMK_IMAGE (build image).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

WS="${ZMK_WS:-$HOME/.cache/zmk-beacon}"
IMAGE="${ZMK_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
TOP="$WS/ws"
MOD="$WS/module"
FORCE_UPDATE=0
LOGGING=0

SHIELDS=()
for arg in "$@"; do
  case "$arg" in
    --clean)   echo "removing workspace: $WS"; rm -rf "$WS"; exit 0 ;;
    --update)  FORCE_UPDATE=1 ;;
    --logging) LOGGING=1 ;;
    -h|--help) awk 'NR>1 && /^#/{sub(/^# ?/,"");print;next} NR>1{exit}' "${BASH_SOURCE[0]}"; exit 0 ;;
    -*) echo "unknown option: $arg" >&2; exit 2 ;;
    *)  SHIELDS+=("$arg") ;;
  esac
done

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

NEED_UPDATE=0
[ ! -d "$TOP/.west" ]     && NEED_UPDATE=1
[ ! -d "$TOP/zmk/app" ]   && NEED_UPDATE=1
[ "$FORCE_UPDATE" -eq 1 ] && NEED_UPDATE=1

SUFFIX=""
[ "$LOGGING" -eq 1 ] && SUFFIX="-logging"
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

docker run --rm \
  -v "$WS:/workspace" \
  -w /workspace/ws \
  -e ZEPHYR_BASE=/workspace/ws/zephyr \
  -e NEED_UPDATE="$NEED_UPDATE" \
  -e TARGETS="$TARGET_LIST" \
  -e LOGGING="$LOGGING" \
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
  if [ "$LOGGING" = "1" ]; then EXTRA="-DCONFIG_ZMK_USB_LOGGING=y"; SUFFIX="-logging"; fi
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
