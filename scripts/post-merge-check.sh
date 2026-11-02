#!/bin/bash
set -uo pipefail

# Runs the checks that are too slow for a PR on a fresh clone from GitHub: a clean
# iOS build, the in-app suite with network tests, the Maestro flows, the rebuild
# check and the package install. See docs/testing.md.
#
#   scripts/post-merge-check.sh [branch or tag]
#
# The default is main. Every check runs even if one fails. The clone is deleted
# if everything passed, and kept with its logs if something failed.
#
# Environment:
#   SIMULATOR   iOS simulator name (default: iPhone 17). Other booted simulators are left alone
#   WORK_DIR    default a new temporary directory
#   MIN_PASSED  default 174, the same as the Full checks workflow

REF="${1:-main}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REMOTE="$(git -C "$SCRIPT_DIR" remote get-url origin)"
export SIMULATOR="${SIMULATOR:-iPhone 17}"
export MIN_PASSED="${MIN_PASSED:-174}"
WORK_DIR="${WORK_DIR:-$(mktemp -d)}"
CLONE_DIR="${WORK_DIR}/repo"
LOG_DIR="${WORK_DIR}/logs"
METRO_PID=""
FAILED=0
RESULTS=()

stop_metro() {
  if [ -n "$METRO_PID" ]; then
    kill "$METRO_PID" 2>/dev/null || true
    wait "$METRO_PID" 2>/dev/null || true
    METRO_PID=""
  fi
}
trap stop_metro EXIT

metro_running() {
  curl -s -m 2 "http://localhost:8081/status" | grep -q "packager-status:running"
}

# Runs a check, keeps its output in a log and records the result.
check() {
  local name="$1"
  shift
  local started="$SECONDS"
  echo "--- ${name} ---"
  if "$@" > "${LOG_DIR}/${name}.log" 2>&1; then
    RESULTS+=("PASS  ${name} ($(( SECONDS - started ))s)")
  else
    RESULTS+=("FAIL  ${name} ($(( SECONDS - started ))s), see ${LOG_DIR}/${name}.log")
    FAILED=1
  fi
}

run_maestro() {
  (cd "${CLONE_DIR}/example" && CI=1 exec npx expo start) > "${LOG_DIR}/metro.log" 2>&1 &
  METRO_PID=$!
  local _
  for _ in $(seq 1 60); do
    metro_running && break
    sleep 1
  done
  maestro --device "$UDID" test --test-output-dir "${LOG_DIR}/maestro" example/.maestro
  local result=$?
  stop_metro
  return "$result"
}

if metro_running; then
  echo "ERROR: Metro is already running on port 8081. Stop it first."
  exit 1
fi

UDID="$(xcrun simctl list devices available | grep -F "    ${SIMULATOR} (" | grep -oE '[0-9A-F-]{36}' | head -1)"
if [ -z "$UDID" ]; then
  echo "ERROR: no simulator named ${SIMULATOR}"
  exit 1
fi
WAS_BOOTED=0
if xcrun simctl list devices booted | grep -q "$UDID"; then WAS_BOOTED=1; fi

mkdir -p "$LOG_DIR"
echo "=== Checking ${REF} from ${REMOTE} in ${WORK_DIR} ==="
git clone --quiet --depth 1 --branch "$REF" "$REMOTE" "$CLONE_DIR" || exit 1
cd "$CLONE_DIR" || exit 1
git log -1 --format='%h %s'
if command -v mise > /dev/null; then mise trust --quiet mise.toml; fi

check install bun install --frozen-lockfile
check ios-build-and-network-tests scripts/ci-local.sh ios network=1
check maestro run_maestro
check rebuild scripts/check-ios-rebuild.sh
check package-install env DEVICE="$UDID" scripts/check-package-install.sh ios

if [ "$WAS_BOOTED" = 0 ]; then xcrun simctl shutdown "$UDID" 2>/dev/null || true; fi

echo
printf '%s\n' "${RESULTS[@]}"
if [ "$FAILED" = 0 ]; then
  rm -rf "$WORK_DIR"
  echo "All checks passed on ${REF}"
else
  echo "Failed. Kept ${WORK_DIR}"
fi
exit "$FAILED"
