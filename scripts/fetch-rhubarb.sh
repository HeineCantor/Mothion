#!/usr/bin/env bash
# Puts Rhubarb Lip Sync (for the lip sync from the audio) beside a Linux build of Mothion, where Mothion looks for it:
# a "rhubarb" folder next to the mothion executable, with the program and its "res" folder (the speech models).
# The releases for Windows come with it (see .github/workflows/release.yml); a build of one's own needs this, or
# MOTHION_RHUBARB set to a rhubarb found elsewhere.
#
#   scripts/fetch-rhubarb.sh            # into build/src/rhubarb (what run-linux.sh builds)
#   scripts/fetch-rhubarb.sh <dir>      # into <dir>/rhubarb (the folder of the mothion executable)
set -euo pipefail

version=1.14.0
url="https://github.com/DanielSWolf/rhubarb-lip-sync/releases/download/v$version/Rhubarb-Lip-Sync-$version-Linux.zip"
sha256=a9a9074862cff47b2d59b8bf399a678a3b0b74f9452ad6ad94cb292913dd8667

scripts="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$scripts")"
target="${1:-$repo/build/src}/rhubarb"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
echo "==> Downloading Rhubarb Lip Sync $version"
curl -fsSL -o "$work/rhubarb.zip" "$url"
echo "$sha256  $work/rhubarb.zip" | sha256sum -c --quiet - || { echo 'Rhubarb: the download is not the one expected' >&2; exit 1; }
unzip -q "$work/rhubarb.zip" -d "$work/x"
src="$(find "$work/x" -mindepth 1 -maxdepth 1 -type d | head -1)"
rm -rf "$target"
mkdir -p "$target"
cp "$src/rhubarb" "$target/"
cp -r "$src/res" "$target/"
cp "$src/LICENSE.md" "$target/"
chmod +x "$target/rhubarb"
echo "==> Rhubarb Lip Sync is in $target"
