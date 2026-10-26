#!/bin/sh
# macOS release: a self-contained "Nomadys PDF.app" (PDFium, SDL3, FreeType and
# fontconfig inside) and a .dmg to drag it to Applications.
#   tools/package-macos.sh   -> dist/Nomadys PDF.app
#                               dist/Nomadys-PDF-VERSION-macos-ARCH.dmg
# Needs the Xcode command line tools and: brew install sdl3 freetype fontconfig pkgconf
# The app is signed ad hoc. Set CODESIGN_ID="Developer ID Application: ..." to sign it
# for distribution (notarising is then up to you).
set -eu
cd "$(dirname "$0")/.."
[ "$(uname -s)" = Darwin ] || { echo "run this on macOS" >&2; exit 1; }

VERSION=$(sed -n 's/^#define NOMADYS_VERSION "\(.*\)"/\1/p' src/nomadys.h)
ARCH=$(uname -m)
SIGN=${CODESIGN_ID:--}
BREW=$(brew --prefix)

make -j"$(sysctl -n hw.ncpu)" npdf

APP="dist/Nomadys PDF.app"
C="$APP/Contents"
rm -rf "$APP"
mkdir -p "$C/MacOS" "$C/Frameworks" "$C/Resources" "$C/share/nomadys-pdf/themes" "$C/share/doc/nomadys-pdf"

cp npdf "$C/MacOS/npdf"
cp third_party/pdfium/lib/libpdfium.dylib "$C/Frameworks/"
cp themes/*.conf "$C/share/nomadys-pdf/themes/"
cp LICENSE config.example "$C/share/doc/nomadys-pdf/"
cp -R licenses "$C/share/doc/nomadys-pdf/licenses"
mkdir -p "$C/share/doc/nomadys-pdf/licenses/pdfium"
cp third_party/pdfium/LICENSE third_party/pdfium/licenses/* "$C/share/doc/nomadys-pdf/licenses/pdfium/"

# ---- the Homebrew libraries, copied in and pointed at each other via @rpath
is_brew() { case "$1" in "$BREW"/*|/usr/local/*) return 0 ;; *) return 1 ;; esac; }

# the absolute file a dependency of $2 (a file) names
resolve() {
    case "$1" in
        @loader_path/*) echo "$(dirname "$2")/${1#@loader_path/}" ;;
        @rpath/*) n=${1#@rpath/}
                  for d in "$(dirname "$2")" "$BREW/lib"; do [ -e "$d/$n" ] && { echo "$d/$n"; return; }; done
                  echo "" ;;
        *) echo "$1" ;;
    esac
}

deps() { otool -L "$1" | tail -n +2 | awk '{print $1}'; }

# bring in $1 (a Mach-O already in the bundle) and everything it needs
bundle() {
    for dep in $(deps "$1"); do
        case "$dep" in /usr/lib/*|/System/*) continue ;; esac
        name=$(basename "$dep")
        [ "$name" = "$(basename "$1")" ] && continue # its own id
        [ "$name" = libpdfium.dylib ] && continue
        src=$(resolve "$dep" "$ORIG")
        if [ -n "$src" ] && is_brew "$(realpath "$src")" || [ -e "$C/Frameworks/$name" ]; then
            install_name_tool -change "$dep" "@rpath/$name" "$1" 2>/dev/null
            if [ ! -e "$C/Frameworks/$name" ]; then
                cp "$(realpath "$src")" "$C/Frameworks/$name"
                chmod u+w "$C/Frameworks/$name"
                install_name_tool -id "@rpath/$name" "$C/Frameworks/$name" 2>/dev/null
                install_name_tool -add_rpath @loader_path "$C/Frameworks/$name" 2>/dev/null || true
                ORIG_SAVE=$ORIG; ORIG=$(realpath "$src")
                bundle "$C/Frameworks/$name"
                ORIG=$ORIG_SAVE
            fi
        fi
    done
}
ORIG=$PWD/npdf
bundle "$C/MacOS/npdf"
# only the rpaths that point into the bundle stay (pkg-config adds $BREW/lib, which would win)
rpaths() { otool -l "$1" | awk '/cmd LC_RPATH/ {r=1} r && /path / {print $2; r=0}'; }
for f in "$C/MacOS/npdf" "$C"/Frameworks/*.dylib; do
    for r in $(rpaths "$f"); do
        case "$r" in @executable_path/../Frameworks|@loader_path) ;; *) install_name_tool -delete_rpath "$r" "$f" ;; esac
    done
done

# ---- fontconfig settings for Macs without Homebrew (see use_bundled_fontconfig in main.c)
FC="$C/Resources/fontconfig"
mkdir -p "$FC/conf.d"
for f in "$BREW"/etc/fonts/conf.d/*.conf; do cp "$(realpath "$f")" "$FC/conf.d/$(basename "$f")"; done
cat > "$FC/fonts.conf" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>/System/Library/Fonts</dir>
  <dir>/Library/Fonts</dir>
  <dir>~/Library/Fonts</dir>
  <dir>/System/Library/AssetsV2/com_apple_MobileAsset_Font7</dir>
  <dir>/System/Library/Assets/com_apple_MobileAsset_Font3</dir>
  <dir>/System/Library/Assets/com_apple_MobileAsset_Font4</dir>
  <dir prefix="xdg">fonts</dir>
  <include ignore_missing="yes">conf.d</include>
  <cachedir>~/Library/Caches/nomadys-pdf/fontconfig</cachedir>
</fontconfig>
EOF

# ---- icon
ICONSET=$(mktemp -d)/nomadys-pdf.iconset
mkdir -p "$ICONSET"
for n in 16 32 128 256 512; do
    cp "icons/nomadys-pdf-$n.png" "$ICONSET/icon_${n}x${n}.png"
done
for n in 16 128 256; do
    cp "icons/nomadys-pdf-$((n * 2)).png" "$ICONSET/icon_${n}x${n}@2x.png"
done
iconutil -c icns "$ICONSET" -o "$C/Resources/nomadys-pdf.icns"
rm -rf "$(dirname "$ICONSET")"

cat > "$C/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Nomadys PDF</string>
  <key>CFBundleDisplayName</key><string>Nomadys PDF</string>
  <key>CFBundleIdentifier</key><string>io.github.hdsoliveira.nomadys-pdf</string>
  <key>CFBundleExecutable</key><string>npdf</string>
  <key>CFBundleIconFile</key><string>nomadys-pdf</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>$(sw_vers -productVersion | cut -d. -f1).0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.productivity</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeName</key><string>PDF document</string>
      <key>CFBundleTypeRole</key><string>Editor</string>
      <key>LSHandlerRank</key><string>Alternate</string>
      <key>LSItemContentTypes</key><array><string>com.adobe.pdf</string></array>
    </dict>
  </array>
</dict>
</plist>
EOF

# ---- sign (inside out) and check
for f in "$C"/Frameworks/*.dylib; do codesign --force --sign "$SIGN" --timestamp=none "$f"; done
codesign --force --sign "$SIGN" --timestamp=none "$APP"
codesign --verify --strict --verbose=1 "$APP"
if otool -l "$C/MacOS/npdf" "$C"/Frameworks/*.dylib | grep -E "(name|path) ($BREW|/usr/local/)"; then
    echo "error: a library still points outside the app" >&2
    exit 1
fi

# ---- disk image
DMG="dist/Nomadys-PDF-$VERSION-macos-$ARCH.dmg"
STAGE=$(mktemp -d)
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -quiet -volname "Nomadys PDF $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"
ls -l "$DMG"
echo "built $APP and $DMG"
