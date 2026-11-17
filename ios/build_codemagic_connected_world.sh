#!/usr/bin/env bash
# Compatibility entry point for the optional Codemagic configuration.
set -euo pipefail
: "${CM_BUILD_DIR:?Codemagic checkout directory is required}"
export CI_OUTPUT_DIR="${CI_OUTPUT_DIR:-$CM_BUILD_DIR/build/codemagic-connected-world}"
bash "$CM_BUILD_DIR/ios/build_ios_metal_ci.sh" validate
bash "$CM_BUILD_DIR/ios/build_ios_metal_ci.sh" full
