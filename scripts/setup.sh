#!/usr/bin/env bash
# Install everything needed to build and test. Debian/Ubuntu.
set -euo pipefail

WITH_PYTHON="no"
SUDO="sudo"
[[ "$(id -u)" -eq 0 ]] && SUDO=""

usage() {
  cat <<'EOF'
Usage: scripts/setup.sh [options]

  -p, --python   also install pybind11 for the Python bindings
  -h, --help     this message
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--python) WITH_PYTHON="yes"; shift ;;
    -h|--help)   usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

echo ">> installing build dependencies"
$SUDO apt-get update -qq
$SUDO apt-get install -y -qq \
  build-essential cmake clang clang-format clang-tidy \
  valgrind libgtest-dev python3 python3-pip python3-yaml

if [[ "$WITH_PYTHON" == "yes" ]]; then
  echo ">> installing pybind11"
  pip3 install --user pybind11 || pip3 install --break-system-packages pybind11
fi

echo ">> versions"
cmake --version | head -1
g++ --version | head -1
clang++ --version | head -1
valgrind --version
python3 --version

echo ">> setup complete - next: scripts/build.sh"
