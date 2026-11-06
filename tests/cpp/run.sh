#!/usr/bin/env bash
# Builds and runs the C++ unit tests, then reports the coverage of the component.
# Needs ESPHome and gcovr (see .github/requirements-ci.txt), a C++ compiler with AddressSanitizer and ninja.
# Arguments are passed to gcovr, e.g. --fail-under-line 100 or --html-details coverage.html
set -euo pipefail

cd "$(dirname "$0")"
BUILD=.esphome/build/kyo-cpp-tests

# `includes` copies the test files into the build, but does not remove the ones deleted since the last build
rm -rf "${BUILD}/src/kyo_alarm"
# Coverage of the last run only
if [[ -d "${BUILD}" ]]; then
  find "${BUILD}" -name '*.gcda' -delete
fi

esphome compile cpp-tests.yaml
"${BUILD}/.pioenvs/kyo-cpp-tests/program"

# --merge-lines: a line of a template is covered if any of its instances runs. The instances in the code generated from
# cpp-tests.yaml never run (see main.cpp)
gcovr --root "${BUILD}/src" --filter "${BUILD}/src/esphome/components/kyo_alarm/" --merge-lines \
  --exclude-unreachable-branches --exclude-throw-branches "${BUILD}" "$@"
