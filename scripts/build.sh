#!/usr/bin/env bash
# Configure and build. Same script a human runs and CI runs.
set -euo pipefail

BUILD_DIR="build"
BUILD_TYPE="RelWithDebInfo"
COMPILER=""
SANITIZE=""
PYTHON_BINDINGS="OFF"
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() {
  cat <<'EOF'
Usage: scripts/build.sh [options]

  -d, --dir DIR         build directory            (default: build)
  -t, --type TYPE       CMake build type           (default: RelWithDebInfo)
  -c, --compiler NAME   gcc | clang               (default: system default)
  -s, --sanitize LIST   address | undefined | address,undefined
  -p, --python          also build the pybind11 module
  -j, --jobs N          parallel build jobs        (default: nproc)
  -h, --help            this message

Examples:
  scripts/build.sh --compiler clang --sanitize address,undefined
  scripts/build.sh --python
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--dir)       BUILD_DIR="$2"; shift 2 ;;
    -t|--type)      BUILD_TYPE="$2"; shift 2 ;;
    -c|--compiler)  COMPILER="$2"; shift 2 ;;
    -s|--sanitize)  SANITIZE="$2"; shift 2 ;;
    -p|--python)    PYTHON_BINDINGS="ON"; shift ;;
    -j|--jobs)      JOBS="$2"; shift 2 ;;
    -h|--help)      usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

CMAKE_ARGS=(
  -S . -B "$BUILD_DIR"
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DSOC_BUILD_PYTHON="$PYTHON_BINDINGS"
)
[[ -n "$SANITIZE" ]] && CMAKE_ARGS+=(-DSOC_SANITIZE="$SANITIZE")

case "$COMPILER" in
  gcc)   CMAKE_ARGS+=(-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++) ;;
  clang) CMAKE_ARGS+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++) ;;
  "")    ;;
  *) echo "unknown compiler: $COMPILER (expected gcc or clang)" >&2; exit 2 ;;
esac

echo ">> configuring (${COMPILER:-default}, ${BUILD_TYPE}${SANITIZE:+, sanitize=$SANITIZE})"
cmake "${CMAKE_ARGS[@]}"

echo ">> building with $JOBS jobs"
cmake --build "$BUILD_DIR" -j "$JOBS"

echo ">> build complete: $BUILD_DIR"
