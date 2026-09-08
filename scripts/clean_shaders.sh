#!/usr/bin/env sh
# Remove generated SPIR-V and the embedded-shader header so the next build
# regenerates them from the GLSL sources. Keeps the source shaders/ dir tidy.
set -e
cd "$(dirname "$0")/.."

rm -f shaders/*.spv
rm -rf build/generated

echo "Removed generated shader artifacts (shaders/*.spv, build/generated/)."
