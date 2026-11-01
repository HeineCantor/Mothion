#!/usr/bin/env bash
# Starts Mothion, building it first if the sources changed since the last build.
#
# Rebuilds with build-linux.sh (same options) when the mothion executable is
# missing, was built with another --config, or is older than any file it is
# built from (src, resources, cmake, the CMakeLists), then starts it.
#
# Examples:
#   scripts/run-linux.sh
#   scripts/run-linux.sh --skip-tests
set -euo pipefail

scripts="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$scripts")"
build_dir="$repo/build"
exe="$build_dir/src/mothion"

# The options of build-linux.sh, passed on when a build is needed.
config=RelWithDebInfo
args=("$@")
while [[ $# -gt 0 ]]; do
    case "$1" in
        --config) config="${2:-}"; shift ;;
        --run)    echo '--run is implied by run-linux.sh' >&2; exit 2 ;;
        -h|--help) "$scripts/build-linux.sh" --help; exit 0 ;;
    esac
    shift
done

up_to_date() {
    [[ -x "$exe" ]] || return 1
    grep -qx "CMAKE_BUILD_TYPE:STRING=$config" "$build_dir/CMakeCache.txt" 2>/dev/null || return 1
    local dirs=()
    for d in src resources cmake; do [[ -d "$repo/$d" ]] && dirs+=("$repo/$d"); done
    [[ -z "$(find "${dirs[@]}" "$repo/CMakeLists.txt" -type f -newer "$exe" -print -quit)" ]]
}

if up_to_date; then
    printf '\033[36m==> Mothion is up to date\033[0m\n'
else
    printf '\033[36m==> Sources changed since the last build: building\033[0m\n'
    "$scripts/build-linux.sh" "${args[@]}"
    # A change the build had nothing to do for leaves the executable's date behind: mark it as current.
    touch "$exe"
fi
exec "$exe"
