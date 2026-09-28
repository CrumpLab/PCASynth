#!/usr/bin/env bash
# Packages the macOS build: a zip of the VST3 bundle and Standalone app, and an
# installer (.pkg) that puts the VST3 in /Library/Audio/Plug-Ins/VST3.
#
#   scripts/package_macos.sh <artefacts dir> <output dir>
#
# e.g. scripts/package_macos.sh build/plugin/PCASynth_artefacts/Release dist
#
# Signing is optional and driven by the environment:
#   MACOS_SIGN_APP        "Developer ID Application: Name (TEAMID)"  -> codesign the bundles
#   MACOS_SIGN_INSTALLER  "Developer ID Installer: Name (TEAMID)"    -> sign the .pkg
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD                       -> notarize and staple
# Without them the bundles are ad-hoc signed and nothing is notarized.
set -euo pipefail

ART="${1:?artefacts dir}"
mkdir -p "${2:?output dir}"
OUT="$(cd "$2" && pwd)"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(sed -n 's/^project(PCASynth VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
NAME="PCASynth"
ID="com.crumplab.pcasynth"
STEM="PCASynth-$VERSION-macOS"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/vst3" "$WORK/zip" "$WORK/resources"
cp -R "$ART/VST3/$NAME.vst3" "$WORK/vst3/"
cp -R "$ART/Standalone/$NAME.app" "$WORK/zip/"

sign_bundle() {
    if [[ -n "${MACOS_SIGN_APP:-}" ]]; then
        codesign --force --deep --options runtime --timestamp --sign "$MACOS_SIGN_APP" "$1"
    else
        codesign --force --deep --sign - "$1"
    fi
    codesign --verify --deep --strict "$1"
}
sign_bundle "$WORK/vst3/$NAME.vst3"
sign_bundle "$WORK/zip/$NAME.app"

notarize() {
    xcrun notarytool submit "$1" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" --wait
}
NOTARIZE=0
if [[ -n "${MACOS_SIGN_APP:-}" && -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
    NOTARIZE=1
    ditto -c -k --keepParent "$WORK/vst3/$NAME.vst3" "$WORK/vst3.zip"
    ditto -c -k --keepParent "$WORK/zip/$NAME.app" "$WORK/app.zip"
    notarize "$WORK/vst3.zip"
    notarize "$WORK/app.zip"
    xcrun stapler staple "$WORK/vst3/$NAME.vst3"
    xcrun stapler staple "$WORK/zip/$NAME.app"
fi

# ---- zip (manual install) ------------------------------------------------------
cp -R "$WORK/vst3/$NAME.vst3" "$WORK/zip/"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$ROOT/CHANGELOG.md" "$WORK/zip/"
( cd "$WORK/zip" && ditto -c -k --sequesterRsrc . "$OUT/$STEM.zip" )

# ---- installer (VST3 only) --------------------------------------------------------
cat > "$WORK/components.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
    <dict>
        <key>BundleHasStrictIdentifier</key>
        <true/>
        <key>BundleIsRelocatable</key>
        <false/>
        <key>BundleIsVersionChecked</key>
        <false/>
        <key>BundleOverwriteAction</key>
        <string>upgrade</string>
        <key>RootRelativeBundlePath</key>
        <string>$NAME.vst3</string>
    </dict>
</array>
</plist>
PLIST
pkgbuild --root "$WORK/vst3" --component-plist "$WORK/components.plist" --install-location "/Library/Audio/Plug-Ins/VST3" \
    --identifier "$ID.vst3" --version "$VERSION" "$WORK/vst3.pkg"

cp "$ROOT/LICENSE" "$WORK/resources/LICENSE.txt"
cat > "$WORK/resources/welcome.txt" <<EOF2
PCASynth $VERSION

A synthesizer that plays points in a PCA space learned from recorded notes.
This installs the VST3 plug-in into /Library/Audio/Plug-Ins/VST3.
In Live: Settings > Plug-Ins, then Rescan. It appears under CrumpLab.
EOF2
sed -e "s/@VERSION@/$VERSION/g" -e "s/@ID@/$ID/g" "$ROOT/scripts/distribution.xml" > "$WORK/distribution.xml"

SIGN_ARGS=()
if [[ -n "${MACOS_SIGN_INSTALLER:-}" ]]; then
    SIGN_ARGS=(--sign "$MACOS_SIGN_INSTALLER" --timestamp)
fi
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" \
    ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$OUT/$STEM.pkg"

if [[ "$NOTARIZE" == 1 && -n "${MACOS_SIGN_INSTALLER:-}" ]]; then
    notarize "$OUT/$STEM.pkg"
    xcrun stapler staple "$OUT/$STEM.pkg"
fi

SIGNED=ad-hoc
[[ -n "${MACOS_SIGN_APP:-}" ]] && SIGNED="Developer ID"
echo "Packaged $OUT/$STEM.zip and $OUT/$STEM.pkg (signing: $SIGNED, notarized: $NOTARIZE)"
