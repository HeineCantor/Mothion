#!/usr/bin/env bash
# Installs what Mothion needs on Linux, when missing, and builds it.
#
# Checks for, and installs when missing (through the distribution's package
# manager, with sudo): the C++ compiler, CMake, Ninja, Qt 6 (Core, Gui, Widgets,
# Sql with the SQLite driver, Network, Multimedia with its FFmpeg backend for the
# audio track, Test, Wayland) and Eigen, and ffmpeg (for File > Export video;
# a failure to install it only warns). Supported: apt
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
                    (with its Multimedia module installed)
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
    packages=(build-essential cmake ninja-build qt6-base-dev libqt6sql6-sqlite qt6-multimedia-dev qt6-wayland libeigen3-dev)
    is_installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'ok installed'; }
    install() { sudo apt-get update && sudo apt-get install -y "$@"; }
    optional=(ffmpeg)
elif command -v dnf >/dev/null; then
    packages=(gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtwayland eigen3-devel)
    is_installed() { rpm -q "$1" >/dev/null 2>&1; }
    install() { sudo dnf install -y "$@"; }
    # Fedora's own ffmpeg-free cannot write H.264: for Export video use the ffmpeg of RPM Fusion instead.
    optional=(ffmpeg-free)
elif command -v pacman >/dev/null; then
    # qt6-multimedia is the library, qt6-multimedia-ffmpeg the backend that decodes the audio files.
    packages=(base-devel cmake ninja qt6-base qt6-multimedia qt6-multimedia-ffmpeg qt6-wayland eigen)
    # base-devel is a group on older Arch installs, a package on newer ones.
    is_installed() { pacman -Q "$1" >/dev/null 2>&1 || pacman -Qg "$1" >/dev/null 2>&1; }
    install() { sudo pacman -S --needed --noconfirm "$@"; }
    optional=(ffmpeg)
elif command -v zypper >/dev/null; then
    packages=(gcc-c++ cmake ninja qt6-base-devel qt6-sql-sqlite qt6-multimedia-devel qt6-wayland eigen3-devel)
    is_installed() { rpm -q "$1" >/dev/null 2>&1; }
    install() { sudo zypper --non-interactive install "$@"; }
    # openSUSE's ffmpeg has no H.264 encoder either: Packman's has.
    optional=(ffmpeg)
else
    packages=()
    optional=()
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

# ffmpeg only serves Export video: not finding or installing it does not stop the build.
if ! command -v ffmpeg >/dev/null && [[ ${#optional[@]} -gt 0 ]]; then
    if [[ $no_install -eq 1 ]]; then
        step "ffmpeg is missing (not installed because of --no-install): Export video needs it"
    else
        step "Installing ${optional[*]} (for Export video)"
        install "${optional[@]}" || step 'ffmpeg could not be installed: Export video will not work until it is.'
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
install a recent Qt (e.g. with aqtinstall or the Qt Online Installer, with the Multimedia module) and pass it
with --qt-dir."
fi
step "Building ($config)"
# One job per core, but no more than the free memory carries (a compile peaks near 1 GB, with -g): all the cores of a
# large machine at once could fill the memory and freeze it. MOTHION_JOBS says another number.
jobs="${MOTHION_JOBS:-}"
if [[ -z "$jobs" ]]; then
    jobs="$(nproc 2>/dev/null || echo 4)"
    avail_kb="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo 2>/dev/null || true)"
    if [[ -n "$avail_kb" ]]; then
        by_memory=$(( avail_kb / 2097152 )) # 2 GB for each job, with room for what else is open
        (( by_memory < 1 )) && by_memory=1
        (( by_memory < jobs )) && jobs=$by_memory
    fi
fi
cmake --build "$build_dir" --parallel "$jobs" || fail 'Building failed.'

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
