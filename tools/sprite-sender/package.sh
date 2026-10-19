#!/usr/bin/env bash
# package.sh: build the release binary and assemble SpriteSender.app beside
# this script, with tools/sprite_pack.py, gifpics.py and sprite_colours.py in
# its Resources (the tools the sender runs; Tools.swift looks there first).
#
#   ./package.sh                      # SpriteSender.app
#   SIGN_IDENTITY=<name> ./package.sh # another signing identity
#
# macOS keys the Bluetooth permission to the signing identity and the bundle
# id, and an ad-hoc signature changes with every build, so the app is signed
# with a persistent self-signed identity when the login keychain has one:
# `chord-dev` by default, the one chord's setup-signing-cert.sh creates and
# canon t-3cf6 measured the permission surviving a rebuild with. Without it
# the app is signed ad hoc and macOS asks again after every build.
# `security find-identity -p codesigning` lists trusted identities only, so a
# self-signed one is looked up with find-certificate.
set -euo pipefail
cd "$(dirname "$0")"

app=SpriteSender.app
contents="$app/Contents"
identity="${SIGN_IDENTITY:-chord-dev}"

swift build -c release >&2

rm -rf "$app"
mkdir -p "$contents/MacOS" "$contents/Resources"
cp .build/release/sprite-sender "$contents/MacOS/sprite-sender"
cp Info.plist "$contents/Info.plist"
cp ../sprite_pack.py ../gifpics.py ../sprite_colours.py "$contents/Resources/"

if security find-certificate -c "$identity" "$HOME/Library/Keychains/login.keychain-db" >/dev/null 2>&1; then
  codesign --force --options runtime --sign "$identity" "$app"
else
  echo "package.sh: no '$identity' identity in the login keychain; signing ad hoc (the Bluetooth permission will not survive a rebuild)" >&2
  codesign --force --options runtime --sign - "$app"
fi
codesign --verify --strict "$app"
echo "$PWD/$app"
