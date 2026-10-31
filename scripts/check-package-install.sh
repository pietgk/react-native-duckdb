#!/bin/bash
set -euo pipefail

# Proves the packed package works for a consumer: installs the tarball in a new
# app outside this repo, builds it and runs a smoke test on a booted device.
#
#   scripts/check-package-install.sh ios|android
#
# Environment:
#   DEVICE       simulator UDID or adb serial (default: booted simulator, or adb default)
#   METRO_PORT   default 8081
#   WORK_DIR     default a new temporary directory, kept for inspection
#   PACKAGE_SOURCE  install this tarball path or URL instead of packing the repo,
#                   for example a GitHub Release asset

PLATFORM="${1:?usage: check-package-install.sh ios|android}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PACKAGE_DIR="${REPO_DIR}/package"
METRO_PORT="${METRO_PORT:-8081}"
WORK_DIR="${WORK_DIR:-$(mktemp -d)}"
APP_DIR="${WORK_DIR}/SmokeApp"
APP_ID="com.smokeapp"
PACKAGE_NAME="$(node -p "require('${PACKAGE_DIR}/package.json').name")"
RN_VERSION="$(node -p "require('${PACKAGE_DIR}/package.json').devDependencies['react-native']")"
NITRO_VERSION="$(node -p "require('${PACKAGE_DIR}/package.json').devDependencies['react-native-nitro-modules']")"
DUCKDB_VERSION="$(node -p "require('${PACKAGE_DIR}/duckdb.json').version")"
LOG_FILE="${WORK_DIR}/device.log"
METRO_PID=""
LOG_PID=""

cleanup() {
  if [ -n "$LOG_PID" ]; then kill "$LOG_PID" 2>/dev/null || true; wait "$LOG_PID" 2>/dev/null || true; fi
  if [ -n "$METRO_PID" ]; then kill "$METRO_PID" 2>/dev/null || true; fi
}
trap cleanup EXIT

echo "=== Working in ${WORK_DIR} ==="

if [ ! -d "$APP_DIR" ]; then
  if [ -n "${PACKAGE_SOURCE:-}" ]; then
    TARBALL="$PACKAGE_SOURCE"
  else
    (cd "$PACKAGE_DIR" && bun run build && npm pack --pack-destination "$WORK_DIR")
    TARBALL="$(ls "$WORK_DIR"/*.tgz)"
  fi

  (cd "$WORK_DIR" && npx --yes @react-native-community/cli@latest init SmokeApp \
    --version "$RN_VERSION" --package-name "$APP_ID" --skip-install --skip-git-init)

  (cd "$APP_DIR" && npm install --legacy-peer-deps "$TARBALL" "react-native-nitro-modules@${NITRO_VERSION}")

  sed "s/EXPECTED_DUCKDB_VERSION/${DUCKDB_VERSION}/" "${SCRIPT_DIR}/fixture/App.tsx" > "${APP_DIR}/App.tsx"

  # A list without core_functions, and without the repo's vss, proves the app's
  # own config is what the build reads.
  node - "$APP_DIR" "$PACKAGE_NAME" <<'NODE'
const fs = require('fs');
const [appDir, name] = process.argv.slice(2);
const file = `${appDir}/package.json`;
const pkg = JSON.parse(fs.readFileSync(file, 'utf8'));
pkg[name] = { build: { extensions: ['parquet', 'json'] } };
fs.writeFileSync(file, JSON.stringify(pkg, null, 2));
NODE
fi

case "$PLATFORM" in
  ios)
    TARGET="${DEVICE:-booted}"
    SIMULATOR_ID="${DEVICE:-$(xcrun simctl list devices booted | grep -oE '[0-9A-F-]{36}' | head -1)}"
    (cd "${APP_DIR}/ios" && pod install)
    (cd "${APP_DIR}/ios" && xcodebuild -workspace SmokeApp.xcworkspace -scheme SmokeApp \
      -configuration Debug -destination "platform=iOS Simulator,id=${SIMULATOR_ID}" \
      -derivedDataPath build build > "${WORK_DIR}/xcodebuild.log" 2>&1) ||
      { tail -40 "${WORK_DIR}/xcodebuild.log"; exit 1; }
    xcrun simctl install "$TARGET" "${APP_DIR}/ios/build/Build/Products/Debug-iphonesimulator/SmokeApp.app"
    if [ "$METRO_PORT" != "8081" ]; then
      xcrun simctl spawn "$TARGET" defaults write "$APP_ID" RCT_jsLocation "localhost:${METRO_PORT}"
    fi
    ;;
  android)
    (cd "${APP_DIR}/android" && ./gradlew :app:assembleDebug --console=plain \
      -PreactNativeArchitectures="${ANDROID_ABI:-arm64-v8a}")
    ADB=(adb)
    if [ -n "${DEVICE:-}" ]; then ADB=(adb -s "$DEVICE"); fi
    "${ADB[@]}" install -r "${APP_DIR}/android/app/build/outputs/apk/debug/app-debug.apk"
    ;;
  *)
    echo "ERROR: unknown platform ${PLATFORM}"
    exit 2
    ;;
esac

(cd "$APP_DIR" && exec npx react-native start --port "$METRO_PORT") > "${WORK_DIR}/metro.log" 2>&1 &
METRO_PID=$!
for _ in $(seq 1 60); do
  curl -s -m 2 "http://localhost:${METRO_PORT}/status" | grep -q "packager-status:running" && break
  sleep 1
done

case "$PLATFORM" in
  ios)
    xcrun simctl spawn "$TARGET" log stream --level debug \
      --predicate "process == \"SmokeApp\"" > "$LOG_FILE" 2>&1 &
    LOG_PID=$!
    sleep 2
    xcrun simctl launch "$TARGET" "$APP_ID"
    ;;
  android)
    if [ "$METRO_PORT" != "8081" ]; then
      PREFS="<map><string name=\\\"debug_http_host\\\">10.0.2.2:${METRO_PORT}</string></map>"
      "${ADB[@]}" shell "run-as $APP_ID sh -c 'mkdir -p shared_prefs && echo \"$PREFS\" > shared_prefs/${APP_ID}_preferences.xml'"
    fi
    "${ADB[@]}" logcat -c
    "${ADB[@]}" logcat -s ReactNativeJS:V > "$LOG_FILE" 2>&1 &
    LOG_PID=$!
    "${ADB[@]}" shell am start -W -n "${APP_ID}/.MainActivity" > /dev/null
    ;;
esac

for _ in $(seq 1 120); do
  if grep -q '\[SMOKE PASS\]' "$LOG_FILE"; then
    echo "${PLATFORM}: smoke test passed"
    exit 0
  fi
  if grep -q '\[SMOKE FAIL\]' "$LOG_FILE"; then
    grep -o '\[SMOKE FAIL\].*' "$LOG_FILE"
    exit 1
  fi
  sleep 1
done

echo "ERROR: no smoke result within 120s"
tail -30 "$LOG_FILE"
exit 1
