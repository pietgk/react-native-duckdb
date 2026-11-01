#!/bin/bash
set -euo pipefail

# Runs the example's in-app suite on a booted device and fails if a test fails.
#
#   scripts/run-device-tests.sh ios|android [autorun query]
#
# The query defaults to network=0. The app must already be installed.
#
# Environment:
#   DEVICE          simulator UDID or adb serial (default: booted simulator, or adb default)
#   METRO_PORT      default 8081. Metro is started here if nothing is listening
#   MIN_PASSED      fail if fewer tests pass, so lost tests are noticed
#   TIMEOUT_SECONDS default 600
#   RESULT_FILE     default test-results.json

PLATFORM="${1:?usage: run-device-tests.sh ios|android [autorun query]}"
QUERY="${2:-network=0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
METRO_PORT="${METRO_PORT:-8081}"
MIN_PASSED="${MIN_PASSED:-1}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-600}"
RESULT_FILE="${RESULT_FILE:-test-results.json}"
APP_ID="dev.pietgk.duckdbreference"
URL="rnduckdb://autorun?${QUERY}"
LOG_FILE="$(mktemp)"
METRO_PID=""
LOG_PID=""

cleanup() {
  if [ -n "$LOG_PID" ]; then kill "$LOG_PID" 2>/dev/null || true; wait "$LOG_PID" 2>/dev/null || true; fi
  if [ -n "$METRO_PID" ]; then kill "$METRO_PID" 2>/dev/null || true; fi
  rm -f "$LOG_FILE"
}
trap cleanup EXIT

if ! curl -s -m 2 "http://localhost:${METRO_PORT}/status" | grep -q "packager-status:running"; then
  echo "--- Starting Metro on port ${METRO_PORT} ---"
  (cd "${REPO_DIR}/example" && CI=1 exec npx expo start --port "${METRO_PORT}") > "${LOG_FILE}.metro" 2>&1 &
  METRO_PID=$!
  for _ in $(seq 1 60); do
    curl -s -m 2 "http://localhost:${METRO_PORT}/status" | grep -q "packager-status:running" && break
    sleep 1
  done
fi

case "$PLATFORM" in
  ios)
    TARGET="${DEVICE:-booted}"
    # The app keeps this setting, so a run on the default port has to clear it.
    if [ "$METRO_PORT" != "8081" ]; then
      xcrun simctl spawn "$TARGET" defaults write "$APP_ID" RCT_jsLocation "localhost:${METRO_PORT}"
    else
      xcrun simctl spawn "$TARGET" defaults delete "$APP_ID" RCT_jsLocation 2>/dev/null || true
    fi
    xcrun simctl terminate "$TARGET" "$APP_ID" 2>/dev/null || true
    xcrun simctl spawn "$TARGET" log stream --level debug \
      --predicate "process == \"DuckDBReference\"" > "$LOG_FILE" 2>&1 &
    LOG_PID=$!
    sleep 2
    xcrun simctl launch "$TARGET" "$APP_ID" -rnduckdbAutorun "$URL"
    ;;
  android)
    ADB=(adb)
    if [ -n "${DEVICE:-}" ]; then ADB=(adb -s "$DEVICE"); fi
    "${ADB[@]}" shell am force-stop "$APP_ID"
    # The app keeps this setting, so a run on the default port has to clear it.
    if [ "$METRO_PORT" != "8081" ]; then
      # React Native on an emulator reaches the host directly, so adb reverse has no effect.
      PREFS="<map><string name=\\\"debug_http_host\\\">10.0.2.2:${METRO_PORT}</string></map>"
      "${ADB[@]}" shell "run-as $APP_ID sh -c 'mkdir -p shared_prefs && echo \"$PREFS\" > shared_prefs/${APP_ID}_preferences.xml'"
    else
      "${ADB[@]}" shell "run-as $APP_ID rm -f shared_prefs/${APP_ID}_preferences.xml" 2>/dev/null || true
    fi
    "${ADB[@]}" logcat -c
    "${ADB[@]}" logcat -s ReactNativeJS:V > "$LOG_FILE" 2>&1 &
    LOG_PID=$!
    "${ADB[@]}" shell am start -W -a android.intent.action.VIEW -d "'$URL'" "$APP_ID" > /dev/null
    ;;
  *)
    echo "ERROR: unknown platform ${PLATFORM}"
    exit 2
    ;;
esac

echo "--- Waiting for ${URL} to finish (timeout ${TIMEOUT_SECONDS}s) ---"
SUMMARY=""
for _ in $(seq 1 "$TIMEOUT_SECONDS"); do
  SUMMARY="$(grep -o '\[TEST SUMMARY\] {.*}' "$LOG_FILE" | head -1 | sed 's/^\[TEST SUMMARY\] //' || true)"
  if [ -n "$SUMMARY" ]; then break; fi
  sleep 1
done

if [ -z "$SUMMARY" ]; then
  echo "ERROR: no test summary within ${TIMEOUT_SECONDS}s"
  tail -30 "$LOG_FILE"
  exit 1
fi

grep -o '\[TEST FAIL\].*' "$LOG_FILE" || true

node - "$SUMMARY" "$LOG_FILE" "$RESULT_FILE" "$PLATFORM" "$MIN_PASSED" <<'NODE'
const fs = require('fs');
const [summary, logFile, resultFile, platform, minPassed] = process.argv.slice(2);
const result = JSON.parse(summary);
result.platform = platform;
result.failures = fs
  .readFileSync(logFile, 'utf8')
  .split('\n')
  .flatMap((line) => line.match(/\[TEST FAIL\] (.*)/)?.[1] ?? []);
fs.writeFileSync(resultFile, `${JSON.stringify(result, null, 2)}\n`);
console.log(`${platform}: ${result.passed} passed, ${result.failed} failed in ${result.durationMs}ms`);
if (result.failed > 0) process.exit(1);
if (result.passed < Number(minPassed)) {
  console.error(`ERROR: expected at least ${minPassed} passing tests`);
  process.exit(1);
}
NODE
