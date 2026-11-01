#!/bin/bash
set -euo pipefail

# Runs a CI device job on this machine: builds the example, installs it and runs
# the in-app suite. Run it before pushing to find failures without waiting for CI.
#
#   scripts/ci-local.sh ios|android [autorun query]
#
# Environment:
#   SIMULATOR     iOS simulator name (default: iPhone 17). Other booted simulators are left alone
#   DEVICE        adb serial to use. Without it the script boots its own emulator and shuts it
#                 down afterwards, so devices used by other projects are left alone
#   AVD           Android emulator to boot (default: the first one listed)
#   ANDROID_HOME  default: the Android Studio or Homebrew location
#   MIN_PASSED    default 160, the same as CI

PLATFORM="${1:?usage: ci-local.sh ios|android [autorun query]}"
QUERY="${2:-network=0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SIMULATOR="${SIMULATOR:-iPhone 17}"
EMULATOR_PORT=5580
EMULATOR_PID=""
export MIN_PASSED="${MIN_PASSED:-160}"

cleanup() {
  if [ -n "$EMULATOR_PID" ]; then
    adb -s "emulator-${EMULATOR_PORT}" emu kill > /dev/null 2>&1 || kill "$EMULATOR_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

# run-device-tests.sh reuses a running Metro, which could be serving another
# checkout. The tests would then pass or fail against the wrong code.
if curl -s -m 2 "http://localhost:8081/status" | grep -q "packager-status:running"; then
  echo "ERROR: Metro is already running on port 8081. Stop it first."
  exit 1
fi

run_ios() {
  local udid
  udid="$(xcrun simctl list devices available --json | node -e "
    const { devices } = JSON.parse(require('fs').readFileSync(0, 'utf8'));
    const device = Object.values(devices).flat().find((d) => d.name === process.argv[1]);
    if (device) console.log(device.udid);
  " "$SIMULATOR")"
  if [ -z "$udid" ]; then
    echo "ERROR: no simulator named ${SIMULATOR}"
    exit 1
  fi

  xcrun simctl boot "$udid" 2>/dev/null || true
  xcrun simctl bootstatus "$udid" -b
  SIMULATOR="$SIMULATOR" "${SCRIPT_DIR}/build-example.sh" ios

  # A simulator that has just booted can refuse the first install.
  local app="${REPO_DIR}/example/ios/build/Build/Products/Debug-iphonesimulator/DuckDBReference.app"
  local attempt
  for attempt in 1 2 3; do
    xcrun simctl install "$udid" "$app" && break
    if [ "$attempt" = 3 ]; then exit 1; fi
    sleep 5
  done

  DEVICE="$udid" "${SCRIPT_DIR}/run-device-tests.sh" ios "$QUERY"
}

find_android_home() {
  local dir
  for dir in "${ANDROID_HOME:-}" "$HOME/Library/Android/sdk" "/opt/homebrew/share/android-commandlinetools"; do
    if [ -n "$dir" ] && [ -d "$dir/platform-tools" ]; then
      echo "$dir"
      return
    fi
  done
  echo "ERROR: no Android SDK found. Set ANDROID_HOME." >&2
  exit 1
}

boot_emulator() {
  local avd="${AVD:-$(emulator -list-avds | head -1)}"
  if [ -z "$avd" ]; then
    echo "ERROR: no Android emulator found. Create one, or set DEVICE."
    exit 1
  fi
  DEVICE="emulator-${EMULATOR_PORT}"
  emulator -avd "$avd" -port "$EMULATOR_PORT" -no-window -no-audio -no-boot-anim -no-snapshot-save > /dev/null 2>&1 &
  EMULATOR_PID=$!

  local _
  for _ in $(seq 1 180); do
    if ! kill -0 "$EMULATOR_PID" 2>/dev/null; then
      echo "ERROR: emulator ${avd} exited. Is it already running? Set AVD or DEVICE to use another."
      exit 1
    fi
    if [ "$(adb -s "$DEVICE" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; then
      return
    fi
    sleep 1
  done
  echo "ERROR: emulator ${avd} did not boot"
  exit 1
}

run_android() {
  ANDROID_HOME="$(find_android_home)"
  export ANDROID_HOME
  export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"

  "${SCRIPT_DIR}/build-example.sh" android

  if [ -z "${DEVICE:-}" ]; then boot_emulator; fi
  adb -s "$DEVICE" install -r "${REPO_DIR}/example/android/app/build/outputs/apk/debug/app-debug.apk"

  DEVICE="$DEVICE" "${SCRIPT_DIR}/run-device-tests.sh" android "$QUERY"
}

case "$PLATFORM" in
  ios) run_ios ;;
  android) run_android ;;
  *)
    echo "ERROR: unknown platform ${PLATFORM}"
    exit 2
    ;;
esac
