#!/usr/bin/env bash
# Builds PureDevotion.pkg (VST3 + AU + Standalone + factory content).
# Signs and notarises when the environment provides the identities:
#   DEV_ID_APP          "Developer ID Application: Your Name (TEAMID)"
#   DEV_ID_INSTALLER    "Developer ID Installer: Your Name (TEAMID)"
#   CERT_P12_BASE64 / CERT_PASSWORD   (CI only: imported into a temporary keychain)
#   NOTARY_APPLE_ID / NOTARY_TEAM_ID / NOTARY_PASSWORD (app-specific password)
# Usage: build_pkg.sh <artefact dir> <output dir>
set -euo pipefail

ART="$1"
OUT="$2"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="1.0.0"
ID="com.raygun.puredevotion"
WORK="$(mktemp -d)"
mkdir -p "$OUT"

# --- optional: import signing certificate into a temporary keychain (CI)
if [[ -n "${CERT_P12_BASE64:-}" ]]; then
  KC="$WORK/build.keychain"
  echo "$CERT_P12_BASE64" | base64 --decode > "$WORK/cert.p12"
  security create-keychain -p ci "$KC"
  security set-keychain-settings -lut 21600 "$KC"
  security unlock-keychain -p ci "$KC"
  security import "$WORK/cert.p12" -k "$KC" -P "$CERT_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productsign
  security set-key-partition-list -S apple-tool:,apple: -s -k ci "$KC"
  security list-keychains -d user -s "$KC" $(security list-keychains -d user | tr -d '"')
fi

sign() {
  if [[ -n "${DEV_ID_APP:-}" ]]; then
    codesign --force --deep --timestamp --options runtime --sign "$DEV_ID_APP" "$1"
  else
    echo "  (unsigned: DEV_ID_APP not set) $1"
  fi
}

# --- component payloads
stage() { mkdir -p "$WORK/$1"; cp -R "$2" "$WORK/$1/"; sign "$WORK/$1/$(basename "$2")"; }
stage vst3 "$ART/VST3/Pure Devotion.vst3"
stage au   "$ART/AU/Pure Devotion.component"
stage app  "$ART/Standalone/Pure Devotion.app"
mkdir -p "$WORK/content"
cp -R "$ROOT/content/Factory Presets" "$WORK/content/"
[[ -d "$ROOT/content/Wavetables" ]] && cp -R "$ROOT/content/Wavetables" "$WORK/content/"
[[ -d "$ROOT/content/Samples" ]] && cp -R "$ROOT/content/Samples" "$WORK/content/"

pkgbuild --root "$WORK/vst3" --identifier "$ID.vst3" --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/VST3" "$WORK/vst3.pkg"
pkgbuild --root "$WORK/au"   --identifier "$ID.au"   --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/Components" "$WORK/au.pkg"
pkgbuild --root "$WORK/app"  --identifier "$ID.app"  --version "$VERSION" --install-location "/Applications" "$WORK/app.pkg"
pkgbuild --root "$WORK/content" --identifier "$ID.content" --version "$VERSION" \
         --install-location "/Library/Application Support/Raygun/Pure Devotion" "$WORK/content.pkg"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Pure Devotion ${VERSION}</title>
  <organization>com.raygun</organization>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline>
    <line choice="vst3"/><line choice="au"/><line choice="app"/><line choice="content"/>
  </choices-outline>
  <choice id="vst3" title="VST3 plug-in"><pkg-ref id="${ID}.vst3"/></choice>
  <choice id="au" title="Audio Unit"><pkg-ref id="${ID}.au"/></choice>
  <choice id="app" title="Standalone app"><pkg-ref id="${ID}.app"/></choice>
  <choice id="content" title="Factory presets + content"><pkg-ref id="${ID}.content"/></choice>
  <pkg-ref id="${ID}.vst3">vst3.pkg</pkg-ref>
  <pkg-ref id="${ID}.au">au.pkg</pkg-ref>
  <pkg-ref id="${ID}.app">app.pkg</pkg-ref>
  <pkg-ref id="${ID}.content">content.pkg</pkg-ref>
</installer-gui-script>
EOF

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$WORK/PureDevotion-unsigned.pkg"

if [[ -n "${DEV_ID_INSTALLER:-}" ]]; then
  productsign --sign "$DEV_ID_INSTALLER" --timestamp "$WORK/PureDevotion-unsigned.pkg" "$OUT/PureDevotion.pkg"
else
  echo "  (unsigned installer: DEV_ID_INSTALLER not set)"
  cp "$WORK/PureDevotion-unsigned.pkg" "$OUT/PureDevotion.pkg"
fi

if [[ -n "${NOTARY_APPLE_ID:-}" && -n "${DEV_ID_INSTALLER:-}" ]]; then
  xcrun notarytool submit "$OUT/PureDevotion.pkg" --apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" \
        --password "$NOTARY_PASSWORD" --wait
  xcrun stapler staple "$OUT/PureDevotion.pkg"
else
  echo "  (not notarised: NOTARY_* not set)"
fi
echo "Built $OUT/PureDevotion.pkg"
