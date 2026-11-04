#!/usr/bin/env bash
# Installs what Mothion needs on Linux, when missing, and builds it.
#
# Checks for, and installs when missing (through the distribution's package
# manager, with sudo): the C++ compiler, CMake, Ninja, Qt 6 (Core, Gui, Widgets,
# Sql with the SQLite driver, Network, Test, Wayland) and Eigen. Supported: apt
# (Debian, Ubuntu), dnf (Fedora), pacman (Arch) and zypper (openSUSE).
# Then configures and builds the project and runs the tests.
#
# Examples:
#   scripts/build-linux.sh
#   scripts/build-linux.sh --run --skip-tests
#   scripts/build-linux.sh --qt-dir ~/Qt/6.10.2/gcc_64   # a Qt not from the distribution
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: build-linux.sh [options]
  --config <name>   RelWithDebInfo (default), Release or Debug
  --qt-dir <dir>    use the Qt installed in <dir> (e.g. ~/Qt/6.10.2/gcc_64) instead of the system one
  --skip-tests      build only
  --dev-tools       also build mothion_brushsheet
  --no-install      do not install missing packages
  --run             start Mothion at the end
  -h, --help        show this help
EOF
}

config=RelWithDebInfo
qt_dir=
skip_tests=0
dev_tools=0
no_install=0
run=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)     config="${2:?--config needs a value}"; shift ;;
        --qt-dir)     qt_dir="${2:?--qt-dir needs a value}"; shift ;;
        --skip-tests) skip_tests=1 ;;
        --dev-tools)  dev_tools=1 ;;
        --no-install) no_install=1 ;;
        --run)        run=1 ;;
        -h|--help)    usage; exit 0 ;;
        *)            echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done
case "$config" in
    RelWithDebInfo|Release|Debug) ;;
    *) echo "--config must be RelWithDebInfo, Release or Debug" >&2; exit 2 ;;
esac

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo/build"
exe="$build_dir/src/mothion"

step() { printf '\033[36m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

# --- Packages ---
# One list per package manager, and how to ask whether a package is installed.
if command -v apt-get >/dev/null; then
    packages=(build-essential cmake ninja-build qt6-base-dev libqt6sql6-sqlite qt6-wayland libeigen3-dev)
    is_installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'ok installed'; }
    install() { sudo apt-get update && sudo apt-get install -y "$@"; }
elif command -v dnf >/dev/null; then
    packages=(gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtwayland eigen3-devel)
    is_installed() { rpm -q "$1" >/dev/null 2>&1; }
    install() { sudo dnf install -y "$@"; }
elif command -v pacman >/dev/null; then
    packages=(base-devel cmake ninja qt6-base qt6-wayland eigen)
    # base-devel is a group on older Arch installs, a package on newer ones.
    is_installed() { pacman -Q "$1" >/dev/null 2>&1 || pacman -Qg "$1" >/dev/null 2>&1; }
    install() { sudo pacman -S --needed --noconfirm "$@"; }
elif command -v zypper >/dev/null; then
    packages=(gcc-c++ cmake ninja qt6-base-devel qt6-sql-sqlite qt6-wayland eigen3-devel)
    is_installed() { rpm -q "$1" >/dev/null 2>&1; }
    install() { sudo zypper --non-interactive install "$@"; }
else
    packages=()
fi

if [[ ${#packages[@]} -eq 0 ]]; then
    step 'Unknown package manager: install a C++20 compiler, CMake, Qt >= 6.5 and Eigen by hand'
else
    missing=()
    for p in "${packages[@]}"; do
        # With --qt-dir the system Qt is not needed.
        [[ -n "$qt_dir" && "$p" == *qt6* ]] && continue
        is_installed "$p" || missing+=("$p")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        if [[ $no_install -eq 1 ]]; then
            step "Missing packages (not installed because of --no-install): ${missing[*]}"
        else
            step "Installing ${missing[*]}"
            install "${missing[@]}" || fail 'Installing the packages failed.'
        fi
    fi
fi

# --- Configure and build ---
cmake_args=(-S "$repo" -B "$build_dir" "-DCMAKE_BUILD_TYPE=$config"
            "-DMOTHION_BUILD_DEVTOOLS=$([[ $dev_tools -eq 1 ]] && echo ON || echo OFF)")
[[ -n "$qt_dir" ]] && cmake_args+=("-DCMAKE_PREFIX_PATH=$qt_dir")
if [[ ! -f "$build_dir/CMakeCache.txt" ]] && command -v ninja >/dev/null; then
    # First configure: Ninja when there is one, otherwise CMake's default (Makefiles).
    cmake_args+=(-G Ninja)
fi
step 'Configuring'
if ! cmake "${cmake_args[@]}"; then
    fail "Configuring failed. If CMake did not find Qt >= 6.5 (older distributions ship an older one),
install a recent Qt (e.g. with aqtinstall or the Qt Online Installer) and pass it with --qt-dir."
fi
step "Building ($config)"
cmake --build "$build_dir" --parallel || fail 'Building failed.'

if [[ $skip_tests -eq 0 ]]; then
    step 'Running the tests'
    # The tests open widgets: without a display, Qt's offscreen platform stands in.
    if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then export QT_QPA_PLATFORM=offscreen; fi
    ctest --test-dir "$build_dir" --output-on-failure || fail 'Tests failed.'
fi

echo
printf '\033[32mMothion is ready: %s\033[0m\n' "$exe"
if [[ $run -eq 1 ]]; then
    "$exe" &
    disown
fi
