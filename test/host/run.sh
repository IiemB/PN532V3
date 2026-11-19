#!/usr/bin/env bash
# Host-side tests for the pure NDEF encode/parse logic (no hardware needed).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p .pio/host-test
"${CXX:-c++}" -std=c++17 -Wall -Wextra -I test/host -I src ${CXXFLAGS:-} \
  test/host/ndef_test.cpp src/nfc/Ndef.cpp -o .pio/host-test/ndef_test
.pio/host-test/ndef_test
