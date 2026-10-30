#!/bin/bash
set -euo pipefail

# Rebuilds the iOS library over its previous output and fails if the result
# differs, which is how the library doubling bug showed up.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LIB="${REPO_DIR}/duckdb/build-ios-iphonesimulator-arm64/libduckdb_combined.a"

measure() {
  echo "$(wc -c < "$LIB" | tr -d ' ') bytes, $(ar -t "$LIB" | wc -l | tr -d ' ') members"
}

BEFORE="$(measure)"
rm -f "${REPO_DIR}/package/DuckDB.xcframework.key"
bash "${SCRIPT_DIR}/build-duckdb-ios.sh" "$@"
AFTER="$(measure)"

echo "before: ${BEFORE}"
echo "after:  ${AFTER}"
if [ "$BEFORE" != "$AFTER" ]; then
  echo "ERROR: rebuilding changed the library"
  exit 1
fi
