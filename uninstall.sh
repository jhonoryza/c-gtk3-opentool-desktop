#!/bin/bash
set -euo pipefail

APP_NAME="Config Opener"
APP_DIR="${APP_NAME}.app"
INSTALL_DEST="/Applications"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "━━━ Config Opener — Uninstaller ━━━"
echo ""

if [ ! -d "${INSTALL_DEST}/${APP_DIR}" ]; then
    echo "App not found at ${INSTALL_DEST}/${APP_DIR}"
    echo "Already uninstalled or never installed."
    exit 0
fi

echo "Removing ${INSTALL_DEST}/${APP_DIR}..."
rm -rf "${INSTALL_DEST}/${APP_DIR}"

echo "Unregistering from Spotlight..."
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -u "${INSTALL_DEST}/${APP_DIR}" 2>/dev/null || true

echo ""
echo "✅  Config Opener uninstalled"
echo "    Spotlight entry will be removed after index update"