#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
ANDROID_DIR="$ROOT_DIR/android"
BUILT_APK="$ANDROID_DIR/app/build/outputs/apk/release/app-release.apk"
OUTPUT_APK="$ROOT_DIR/render-lol.apk"

ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}" \
    "$ANDROID_DIR/gradlew" -p "$ANDROID_DIR" assembleRelease

cp "$BUILT_APK" "$OUTPUT_APK"
printf 'Release APK ready: %s\n' "$OUTPUT_APK"
