#!/usr/bin/env bash
# Puts ONNX Runtime (MIT), which runs the ZIPA phone recognizer of the lip sync from the audio, where the build finds
# it: third_party/onnxruntime (include/, lib/). Without it Mothion builds all the same, with Rhubarb Lip Sync alone.
#
# It comes from Microsoft's NuGet package, pinned to a version and its hash: the one package that has the headers and
# the libraries of Linux and Windows together (the release for Windows takes it the same way, see
# .github/workflows/release.yml).
#
#   scripts/fetch-onnxruntime.sh            # into third_party/onnxruntime
#   scripts/fetch-onnxruntime.sh <dir>      # into <dir>
# then configure again (cmake finds it there, or pass -DMOTHION_ONNXRUNTIME_DIR=<dir>).
set -euo pipefail

version=1.23.2
url="https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/$version/microsoft.ml.onnxruntime.$version.nupkg"
sha256=25fe172f7fcdf34f2b5b02c8b997f6b88abc282956849d4af4335af23d0c4e4a

scripts="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$scripts")"
target="${1:-$repo/third_party/onnxruntime}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
echo "==> Downloading ONNX Runtime $version"
curl -fsSL -o "$work/ort.nupkg" "$url"
echo "$sha256  $work/ort.nupkg" | sha256sum -c --quiet - || { echo 'ONNX Runtime: the download is not the one expected' >&2; exit 1; }
unzip -q "$work/ort.nupkg" 'build/native/include/*' 'runtimes/linux-x64/native/*' LICENSE ThirdPartyNotices.txt -d "$work/x"
rm -rf "$target"
mkdir -p "$target/include" "$target/lib"
cp "$work/x/build/native/include/"*.h "$target/include/"
# The library by its soname (what the loader asks for), with the name the linker looks for beside it.
cp "$work/x/runtimes/linux-x64/native/libonnxruntime.so" "$target/lib/libonnxruntime.so.1"
ln -s libonnxruntime.so.1 "$target/lib/libonnxruntime.so"
cp "$work/x/LICENSE" "$work/x/ThirdPartyNotices.txt" "$target/"
echo "==> ONNX Runtime is in $target"
