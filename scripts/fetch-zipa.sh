#!/usr/bin/env bash
# Puts the ZIPA phone recognizer (Zhu et al., ACL 2025; CC BY 4.0), which the lip sync from the audio hears the voice
# with, beside a Linux build of Mothion, where Mothion looks for it: a "zipa" folder next to the mothion executable,
# with the model (ZIPA-CR small, int8) and its phones. The releases for Windows come with it (see
# .github/workflows/release.yml); a build of one's own needs this (and scripts/fetch-onnxruntime.sh before building),
# or MOTHION_ZIPA set to a folder that has them.
#
#   scripts/fetch-zipa.sh            # into build/src/zipa (what run-linux.sh builds)
#   scripts/fetch-zipa.sh <dir>      # into <dir>/zipa (the folder of the mothion executable)
set -euo pipefail

# The model's repository, at the revision these hashes are of.
revision=dfe87c97662606fa6db90ad06f627d4363314744
base="https://huggingface.co/anyspeech/zipa-small-crctc-500k/resolve/$revision"
model_sha256=d0e28b68164e8b1fbd6105100c01798828aa0855000ce9bbbd1a2cec233adf13
tokens_sha256=f8e042a0c9130532b22d03ec7cae2f75a23fbec70c450c31a8efb51787b2b8fe

scripts="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$scripts")"
target="${1:-$repo/build/src}/zipa"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
echo "==> Downloading ZIPA (small, int8)"
curl -fsSL -o "$work/model.int8.onnx" "$base/model.int8.onnx"
curl -fsSL -o "$work/tokens.txt" "$base/tokens.txt"
echo "$model_sha256  $work/model.int8.onnx" | sha256sum -c --quiet - || { echo 'ZIPA: the model is not the one expected' >&2; exit 1; }
echo "$tokens_sha256  $work/tokens.txt" | sha256sum -c --quiet - || { echo 'ZIPA: the phones are not the ones expected' >&2; exit 1; }
rm -rf "$target"
mkdir -p "$target"
cp "$work/model.int8.onnx" "$work/tokens.txt" "$target/"
cat > "$target/LICENSE.txt" <<'EOF'
ZIPA-CR small (anyspeech/zipa-small-crctc-500k), by Jian Zhu, Farhan Samir, Eleanor Chodroff and David R. Mortensen:
"ZIPA: A family of efficient models for multilingual phone recognition", ACL 2025.
https://github.com/lingjzhu/zipa - https://huggingface.co/anyspeech/zipa-small-crctc-500k
Licensed under the Creative Commons Attribution 4.0 International license: https://creativecommons.org/licenses/by/4.0/
Mothion uses it as it is (model.int8.onnx, the int8 export the authors published).
EOF
echo "==> ZIPA is in $target"
