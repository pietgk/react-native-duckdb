#!/bin/bash
set -euo pipefail

# Generates the example's native project and builds it. CI and ci-local.sh both
# use this, so a local build matches CI.
#
#   scripts/build-example.sh ios|android [prebuild|build]
#
# Without a step it runs both.
#
# Environment:
#   SIMULATOR     iOS simulator name (default: iPhone 17)
#   ANDROID_ARCH  default: matches this machine, so x86_64 in CI and arm64-v8a on Apple silicon

PLATFORM="${1:?usage: build-example.sh ios|android [prebuild|build]}"
STEP="${2:-all}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXAMPLE_DIR="$(cd "$SCRIPT_DIR/../example" && pwd)"
SIMULATOR="${SIMULATOR:-iPhone 17}"

case "$(uname -m)" in
  arm64|aarch64) DEFAULT_ARCH="arm64-v8a" ;;
  *) DEFAULT_ARCH="x86_64" ;;
esac
ANDROID_ARCH="${ANDROID_ARCH:-$DEFAULT_ARCH}"

prebuild() {
  case "$PLATFORM" in
    ios) (cd "$EXAMPLE_DIR" && npx expo prebuild --platform ios) ;;
    android) (cd "$EXAMPLE_DIR" && npx expo prebuild --platform android --no-install) ;;
    *)
      echo "ERROR: unknown platform ${PLATFORM}"
      exit 2
      ;;
  esac
}

build() {
  case "$PLATFORM" in
    ios)
      (cd "$EXAMPLE_DIR/ios" && xcodebuild -workspace DuckDBReference.xcworkspace -scheme DuckDBReference \
        -configuration Debug -destination "platform=iOS Simulator,name=${SIMULATOR}" \
        -derivedDataPath build build)
      ;;
    android)
      (cd "$EXAMPLE_DIR/android" && ./gradlew :app:assembleDebug \
        -PreactNativeArchitectures="${ANDROID_ARCH}" --console=plain)
      ;;
    *)
      echo "ERROR: unknown platform ${PLATFORM}"
      exit 2
      ;;
  esac
}

case "$STEP" in
  prebuild) prebuild ;;
  build) build ;;
  all)
    prebuild
    build
    ;;
  *)
    echo "ERROR: unknown step ${STEP}"
    exit 2
    ;;
esac
