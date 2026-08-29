#!/usr/bin/env bash
#
# Build a self-contained, double-clickable Typewriter.app on macOS — and
# optionally a drag-to-Applications .dmg. The macOS counterpart of
# build_installer.ps1.
#
# It assembles dist/macos-universal/Typewriter.app with:
#   - the static universal (arm64 + x86_64) typewriter binary from
#     `make STATIC=1` (SDL2, SDL2_ttf, FreeType, sounds and font all inside,
#     so no dylibs to bundle)
#   - an Info.plist (NSHighResolutionCapable = true for crisp Retina)
#   - a Typewriter.icns generated from resources/icon_*.png
#   - an ad-hoc code signature so Gatekeeper lets it launch locally
#
# Usage: ./package_macos.sh [--dmg] [--no-build] [--sign "Developer ID App: …"]

set -euo pipefail

usage() {
    cat <<'EOF'
Build a self-contained Typewriter.app (and optionally a .dmg) on macOS.

Usage: ./package_macos.sh [--dmg] [--no-build] [--sign <identity>]

  --dmg              Also produce dist/Typewriter-<version>.dmg (drag-to-Applications).
  --no-build         Skip the build; reuse the existing ./typewriter binary.
  --sign <identity>  Code-sign with a Developer ID instead of ad-hoc ("-").
                     For distribution you still need to notarize separately.
  -h, --help         Show this help.
EOF
}

do_dmg=0
do_build=1
sign_id="-"          # ad-hoc by default

while [ $# -gt 0 ]; do
    case "$1" in
        --dmg)      do_dmg=1; shift ;;
        --no-build) do_build=0; shift ;;
        --sign)     sign_id="${2:?--sign needs an identity}"; shift 2 ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "package_macos.sh: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ "$(uname -s)" != "Darwin" ]; then
    echo "package_macos.sh: this must run on macOS." >&2
    exit 1
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root"

# --- version (single source of truth: main.c) ----------------------------
version="$(sed -n 's/.*#define TYPEWRITER_VERSION "\([0-9.]*\)".*/\1/p' main.c | head -n1)"
[ -n "$version" ] || { echo "could not read TYPEWRITER_VERSION from main.c" >&2; exit 1; }
echo "Typewriter version : $version"

# --- build ---------------------------------------------------------------
if [ "$do_build" -eq 1 ]; then
    if [ ! -x deps/prefix/bin/sdl2-config ]; then
        echo "Building static SDL2 + SDL2_ttf (make deps) ..."
        make deps
    fi
    echo "Building (make STATIC=1) ..."
    make STATIC=1
fi
[ -x typewriter ] || { echo "./typewriter missing — run without --no-build." >&2; exit 1; }

# --- assemble the .app skeleton ------------------------------------------
# dist/ mirrors the Windows installer layout.
app="dist/macos-universal/Typewriter.app"
mkdir -p dist/macos-universal
echo "Assembling $app ..."
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp typewriter "$app/Contents/MacOS/typewriter"
chmod +x "$app/Contents/MacOS/typewriter"
cp LICENSE.txt "$app/Contents/Resources/LICENSE.txt"

# --- Info.plist ----------------------------------------------------------
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>                <string>Typewriter</string>
    <key>CFBundleDisplayName</key>         <string>Typewriter</string>
    <key>CFBundleExecutable</key>          <string>typewriter</string>
    <key>CFBundleIdentifier</key>          <string>com.fezcode.typewriter</string>
    <key>CFBundleVersion</key>             <string>${version}</string>
    <key>CFBundleShortVersionString</key>  <string>${version}</string>
    <key>CFBundlePackageType</key>         <string>APPL</string>
    <key>CFBundleIconFile</key>            <string>Typewriter</string>
    <key>CFBundleInfoDictionaryVersion</key> <string>6.0</string>
    <key>NSHighResolutionCapable</key>     <true/>
    <key>LSMinimumSystemVersion</key>      <string>11.0</string>
    <key>NSHumanReadableCopyright</key>    <string>Fezcode</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>        <string>Text document</string>
            <key>CFBundleTypeRole</key>        <string>Editor</string>
            <key>LSHandlerRank</key>           <string>Alternate</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>public.plain-text</string>
                <string>net.daringfireball.markdown</string>
            </array>
        </dict>
    </array>
</dict>
</plist>
PLIST

# --- icon: resources/icon_*.png -> Typewriter.icns -----------------------
if command -v iconutil >/dev/null 2>&1; then
    echo "Building Typewriter.icns ..."
    iconset="$(mktemp -d)/Typewriter.iconset"
    mkdir -p "$iconset"
    cp resources/icon_16.png   "$iconset/icon_16x16.png"
    cp resources/icon_32.png   "$iconset/icon_16x16@2x.png"
    cp resources/icon_32.png   "$iconset/icon_32x32.png"
    cp resources/icon_64.png   "$iconset/icon_32x32@2x.png"
    cp resources/icon_128.png  "$iconset/icon_128x128.png"
    cp resources/icon_256.png  "$iconset/icon_128x128@2x.png"
    cp resources/icon_256.png  "$iconset/icon_256x256.png"
    cp resources/icon_512.png  "$iconset/icon_256x256@2x.png"
    cp resources/icon_512.png  "$iconset/icon_512x512.png"
    cp resources/icon_1024.png "$iconset/icon_512x512@2x.png"
    iconutil -c icns "$iconset" -o "$app/Contents/Resources/Typewriter.icns"
else
    echo "warning: iconutil not found — app will use the default icon." >&2
fi

# --- code sign (ad-hoc by default so it launches locally) ----------------
echo "Code-signing ($([ "$sign_id" = "-" ] && echo ad-hoc || echo "$sign_id")) ..."
codesign --force --deep --sign "$sign_id" "$app"

echo "Built: $app"

# --- optional .dmg -------------------------------------------------------
if [ "$do_dmg" -eq 1 ]; then
    command -v hdiutil >/dev/null 2>&1 || { echo "hdiutil missing" >&2; exit 1; }
    dmg="dist/Typewriter-${version}.dmg"
    echo "Building $dmg ..."
    stage="$(mktemp -d)"
    cp -R "$app" "$stage/"
    ln -s /Applications "$stage/Applications"
    rm -f "$dmg"
    hdiutil create -volname "Typewriter ${version}" -srcfolder "$stage" \
        -ov -format UDZO "$dmg" >/dev/null
    echo "Built: $dmg"
fi

echo "Done. Double-click $app, or drag it to /Applications."
