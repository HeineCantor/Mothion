#!/usr/bin/env bash
# Installs what is missing and builds Mothion (see scripts/build-linux.sh).
# Options are passed on, e.g.:  ./build.sh --run   ./build.sh --skip-tests --dev-tools
exec "$(dirname "${BASH_SOURCE[0]}")/scripts/build-linux.sh" "$@"
