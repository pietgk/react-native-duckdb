#!/bin/bash
set -euo pipefail

# Downloads the DuckDB source pinned in duckdb.json into duckdb/. Fetching by
# commit means git verifies the content. Builds call this, so a package installed
# from npm needs no submodule.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PACKAGE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DEST="${PACKAGE_DIR}/duckdb"
MARKER="${DEST}/.source-commit"
LOCK="${PACKAGE_DIR}/.duckdb-fetch-lock"

COMMIT="$(node -p "require('${PACKAGE_DIR}/duckdb.json').commit")"

is_current() {
  [ "$(cat "$MARKER" 2>/dev/null)" = "$COMMIT" ]
}

if is_current; then exit 0; fi

# Android configures each ABI separately, possibly in parallel.
until mkdir "$LOCK" 2>/dev/null; do sleep 1; done
TMP=""
cleanup() {
  if [ -n "$TMP" ]; then rm -rf "$TMP"; fi
  rmdir "$LOCK"
}
trap cleanup EXIT

if is_current; then exit 0; fi

echo "=== Fetching DuckDB ${COMMIT} ==="
TMP="$(mktemp -d "${PACKAGE_DIR}/.duckdb-fetch.XXXXXX")"
git -C "$TMP" init --quiet
git -C "$TMP" fetch --quiet --depth 1 https://github.com/duckdb/duckdb.git "$COMMIT"
git -C "$TMP" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
rm -rf "${TMP}/.git"
echo "$COMMIT" > "${TMP}/.source-commit"

rm -rf "$DEST"
mv "$TMP" "$DEST"
TMP=""
