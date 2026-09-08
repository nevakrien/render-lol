#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
ANDROID_DIR="$ROOT_DIR/android"
SHADER_DIR="$ROOT_DIR/shaders"
PACKAGE="com.renderlol"
DEVICE_SHADER_DIR="files/shaders"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()  { echo -e "${GREEN}[deploy]${NC} $*"; }
warn() { echo -e "${YELLOW}[deploy]${NC} $*"; }
err()  { echo -e "${RED}[deploy]${NC} $*" >&2; }

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Build, install, and run render-lol on Android with hot-reload.

Options:
  --build-only    Only build the APK, don't install or run
  --no-watch      Don't watch for shader changes
  --skip-build    Skip the Gradle build (use existing APK)
  --release       Build release instead of debug
  -h, --help      Show this help

The script will:
  1. Build the APK via Gradle
  2. Install and launch it on the connected device
  3. Push shaders/ to the device
  4. Watch shaders/ for changes and push updates (hot-reload)
EOF
    exit 0
}

BUILD_ONLY=false
NO_WATCH=false
SKIP_BUILD=false
BUILD_TYPE="Debug"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-only)  BUILD_ONLY=true; shift ;;
        --no-watch)    NO_WATCH=true; shift ;;
        --skip-build)  SKIP_BUILD=true; shift ;;
        --release)     BUILD_TYPE="Release"; shift ;;
        -h|--help)     usage ;;
        *) err "Unknown option: $1"; usage ;;
    esac
done

# Preflight checks
if ! command -v adb &>/dev/null; then
    err "adb not found. Install Android SDK platform-tools."
    exit 1
fi

if [[ ! -d "$ANDROID_DIR" ]]; then
    err "android/ directory not found at $ANDROID_DIR"
    exit 1
fi

# Build
if [[ "$SKIP_BUILD" == false ]]; then
    log "Building $BUILD_TYPE APK..."
    ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}" \
        "$ANDROID_DIR/gradlew" -p "$ANDROID_DIR" assembleDebug
    APK="$ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"
    if [[ ! -f "$APK" ]]; then
        err "APK not found at $APK"
        exit 1
    fi
    log "APK built: $APK"
fi

if [[ "$BUILD_ONLY" == true ]]; then
    log "Build-only mode. APK at: $APK"
    exit 0
fi

# Check device
DEVICES=$(adb devices | grep -v "^List" | grep "device$" | wc -l)
if [[ "$DEVICES" -eq 0 ]]; then
    err "No Android device connected. Connect a device with USB debugging enabled."
    exit 1
fi
if [[ "$DEVICES" -gt 1 ]]; then
    warn "Multiple devices connected. Using the first one."
fi

DEVICE_SERIAL=$(adb devices | grep -v "^List" | grep "device$" | head -1 | awk '{print $1}')
ADB="adb -s $DEVICE_SERIAL"
log "Device: $DEVICE_SERIAL"

# Install and launch
if [[ "$SKIP_BUILD" == false ]]; then
    log "Installing APK..."
    $ADB install -r "$APK"
fi

log "Launching $PACKAGE..."
$ADB shell am start -n "$PACKAGE/org.libsdl.app.SDLActivity" 2>/dev/null || \
    $ADB shell monkey -p "$PACKAGE" -c android.intent.category.LAUNCHER 1

# Push shaders
push_shaders() {
    if [[ -d "$SHADER_DIR" ]]; then
        $ADB shell "run-as $PACKAGE mkdir -p $DEVICE_SHADER_DIR"
        for f in "$SHADER_DIR"/*.spv; do
            [[ -f "$f" ]] || continue
            NAME=$(basename "$f")
            $ADB push "$f" /data/local/tmp/_sh 2>/dev/null
            $ADB shell "run-as $PACKAGE cp /data/local/tmp/_sh $DEVICE_SHADER_DIR/$NAME"
        done
        log "Shaders pushed to device"
    fi
}

push_shaders

if [[ "$NO_WATCH" == true ]]; then
    log "Done (not watching for changes)"
    exit 0
fi

# Watch for shader changes and push
log "Watching $SHADER_DIR for changes... (Ctrl+C to stop)"

if command -v inotifywait &>/dev/null; then
    while true; do
        inotifywait -q -e modify,create,delete "$SHADER_DIR"/*.spv 2>/dev/null || true
        sleep 0.2
        push_shaders
    done
elif command -v fswatch &>/dev/null; then
    fswatch -0 "$SHADER_DIR" | while read -d "" event; do
        if [[ "$event" == *.spv ]]; then
            push_shaders
        fi
    done
else
    warn "No file watcher found (install inotify-tools or fswatch)."
    warn "Falling back to polling (slower)."
    PREV_HASH=""
    while true; do
        HASH=$(find "$SHADER_DIR" -name "*.spv" -exec md5sum {} + 2>/dev/null | sort | md5sum | cut -d' ' -f1)
        if [[ "$HASH" != "$PREV_HASH" && -n "$PREV_HASH" ]]; then
            push_shaders
        fi
        PREV_HASH="$HASH"
        sleep 1
    done
fi
