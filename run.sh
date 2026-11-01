#!/usr/bin/env bash
# Starts Mothion, building it first if the sources changed (see scripts/run-linux.sh).
# Options are passed on to the build, e.g.:  ./run.sh --skip-tests
exec "$(dirname "${BASH_SOURCE[0]}")/scripts/run-linux.sh" "$@"
