#!/usr/bin/env bash
# Bundle everything needed to debug a failed run into one tarball.
set -euo pipefail

BUILD_DIR="build"
ARTIFACTS="artifacts"
OUTPUT=""

usage() {
  cat <<'EOF'
Usage: scripts/collect-artifacts.sh [options]

  -d, --dir DIR     build directory     (default: build)
  -a, --art DIR     artifact directory  (default: artifacts)
  -o, --out FILE    output tarball      (default: soc-artifacts-<utc>.tar.gz)
  -h, --help        this message
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--dir) BUILD_DIR="$2"; shift 2 ;;
    -a|--art) ARTIFACTS="$2"; shift 2 ;;
    -o|--out) OUTPUT="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUTPUT="${OUTPUT:-soc-artifacts-${STAMP}.tar.gz}"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/bundle"

# Environment: which compiler and kernel produced this run matters when a
# failure reproduces on one machine and not another.
{
  echo "collected_utc=$STAMP"
  echo "host=$(uname -a)"
  echo "cmake=$(cmake --version 2>/dev/null | head -1)"
  echo "compiler=$(${CXX:-c++} --version 2>/dev/null | head -1)"
  echo "git_commit=$(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "git_dirty=$(git status --porcelain 2>/dev/null | wc -l) file(s)"
} > "$STAGE/bundle/environment.txt"

for item in "$ARTIFACTS" benchmarks/RESULTS.md regmaps; do
  [[ -e "$item" ]] && cp -r "$item" "$STAGE/bundle/" || true
done

# The generated header is the contract the tests were built against.
[[ -f "$BUILD_DIR/generated/soc/soc_registers.hpp" ]] && \
  cp "$BUILD_DIR/generated/soc/soc_registers.hpp" "$STAGE/bundle/" || true

find . -maxdepth 2 -name "*.csv" -newermt "-1 day" \
  -exec cp {} "$STAGE/bundle/" \; 2>/dev/null || true

tar -czf "$OUTPUT" -C "$STAGE" bundle
echo ">> wrote $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
tar -tzf "$OUTPUT" | sed 's/^/   /'
