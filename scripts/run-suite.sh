#!/usr/bin/env bash
# Run the test suite, optionally under Valgrind, and keep the artefacts.
set -euo pipefail

BUILD_DIR="build"
TRANSPORT="emulated"
VALGRIND="no"
FILTER="*"
ARTIFACTS="artifacts"

usage() {
  cat <<'EOF'
Usage: scripts/run-suite.sh [options]

  -d, --dir DIR         build directory        (default: build)
  -t, --transport NAME  emulated|jtag|i2c|spi  (default: emulated)
  -f, --filter GLOB     GoogleTest filter      (default: *)
  -g, --valgrind        run under Valgrind memcheck
  -o, --out DIR         artifact directory     (default: artifacts)
  -h, --help            this message
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--dir)       BUILD_DIR="$2"; shift 2 ;;
    -t|--transport) TRANSPORT="$2"; shift 2 ;;
    -f|--filter)    FILTER="$2"; shift 2 ;;
    -g|--valgrind)  VALGRIND="yes"; shift ;;
    -o|--out)       ARTIFACTS="$2"; shift 2 ;;
    -h|--help)      usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

BINARY="$BUILD_DIR/soc_tests"
if [[ ! -x "$BINARY" ]]; then
  echo "error: $BINARY not found - run scripts/build.sh first" >&2
  exit 1
fi

mkdir -p "$ARTIFACTS"
export SOC_TRANSPORT="$TRANSPORT"

echo ">> transport=$TRANSPORT filter=$FILTER valgrind=$VALGRIND"

if [[ "$VALGRIND" == "yes" ]]; then
  command -v valgrind >/dev/null || { echo "valgrind not installed" >&2; exit 1; }
  valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=definite,indirect \
           --errors-for-leak-kinds=definite,indirect --track-origins=yes \
           "$BINARY" --gtest_filter="$FILTER" \
           --gtest_output="xml:$ARTIFACTS/results.xml"
else
  "$BINARY" --gtest_filter="$FILTER" --gtest_output="xml:$ARTIFACTS/results.xml"
fi

echo ">> results in $ARTIFACTS/results.xml"
