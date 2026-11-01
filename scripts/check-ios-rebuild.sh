#!/bin/bash
set -euo pipefail

# Rebuilds the iOS library over its previous output and fails if the result
# differs, which is how the library doubling bug showed up.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PACKAGE_DIR="${REPO_DIR}/package"
LIB="${PACKAGE_DIR}/duckdb/build-ios-iphonesimulator-arm64/libduckdb_combined.a"

measure() {
  echo "$(wc -c < "$LIB" | tr -d ' ') bytes, $(ar -t "$LIB" | wc -l | tr -d ' ') members"
}

BEFORE="$(measure)"
rm -f "${PACKAGE_DIR}/DuckDB.xcframework.key"
# The podspec sets this during pod install. Without it the build cannot find the
# app's extension list, and would rebuild with a different one.
RNDUCKDB_APP_IOS_DIR="${REPO_DIR}/example/ios" bash "${PACKAGE_DIR}/scripts/build-duckdb-ios.sh" "$@"
AFTER="$(measure)"

echo "before: ${BEFORE}"
echo "after:  ${AFTER}"
if [ "$BEFORE" != "$AFTER" ]; then
  echo "ERROR: rebuilding changed the library"
  exit 1
fi
