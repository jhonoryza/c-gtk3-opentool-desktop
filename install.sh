#!/bin/bash
set -euo pipefail

APP_NAME="Config Opener"
BUNDLE_ID="com.personal.config-opener"
APP_DIR="${APP_NAME}.app"
CONTENTS="${APP_DIR}/Contents"
MACOS_DIR="${CONTENTS}/MacOS"
RESOURCES_DIR="${CONTENTS}/Resources"
INSTALL_DEST="/Applications"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "━━━ Config Opener — Installer ━━━"
echo ""

if [ ! -f "${SCRIPT_DIR}/config-opener" ]; then
    echo "Binary not found. Building..."
    make -C "${SCRIPT_DIR}"
fi

echo "Creating app bundle..."
rm -rf "${SCRIPT_DIR}/${APP_DIR}"
mkdir -p "${MACOS_DIR}"
mkdir -p "${RESOURCES_DIR}"

cp "${SCRIPT_DIR}/config-opener" "${MACOS_DIR}/config-opener"
cp "${SCRIPT_DIR}/Info.plist" "${CONTENTS}/Info.plist"

cat > "${MACOS_DIR}/launcher" << 'LAUNCHER_EOF'
#!/bin/bash
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
export XDG_DATA_DIRS="/opt/homebrew/share:/usr/local/share:${XDG_DATA_DIRS:-}"
DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$DIR/config-opener"
LAUNCHER_EOF

chmod +x "${MACOS_DIR}/launcher"

if [ ! -f "${SCRIPT_DIR}/AppIcon.icns" ]; then
    echo "No custom icon found. Using default GTK icon."
    touch "${RESOURCES_DIR}/AppIcon.icns"
else
    cp "${SCRIPT_DIR}/AppIcon.icns" "${RESOURCES_DIR}/AppIcon.icns"
fi

echo "Installing to ${INSTALL_DEST}/${APP_DIR}..."
if [ -d "${INSTALL_DEST}/${APP_DIR}" ]; then
    echo "Existing app found. Removing old version..."
    rm -rf "${INSTALL_DEST}/${APP_DIR}"
fi

cp -R "${SCRIPT_DIR}/${APP_DIR}" "${INSTALL_DEST}/"

echo "Registering with Spotlight..."
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f "${INSTALL_DEST}/${APP_DIR}" 2>/dev/null || true

mdutil -i on / 2>/dev/null || true

echo ""
echo "✅  Config Opener installed to ${INSTALL_DEST}/${APP_DIR}"
echo "    You can now open it from Spotlight (search: Config Opener)"
echo ""
echo "To uninstall, run: ./uninstall.sh"