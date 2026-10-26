#!/bin/sh
# Download the pinned PDFium build into third_party/pdfium and verify it.
set -eu
RELEASE=chromium/8086
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)  ASSET=pdfium-linux-x64;  LIB=libpdfium.so
                   SHA256=588577cf52dabc1a444988bac841920df54cc2f141801424de97ab04f4fbb935 ;;
    Darwin-arm64)  ASSET=pdfium-mac-arm64;  LIB=libpdfium.dylib
                   SHA256=e98679e052c07edbb5a627980902abb823d4b3f35744d877bd21668bd9fc13ab ;;
    Darwin-x86_64) ASSET=pdfium-mac-x64;    LIB=libpdfium.dylib
                   SHA256=933a85a138f6027243c56bff8676375c33ceeb767401389415ffc44d689ca85d ;;
    *) echo "no pinned PDFium build for $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac
URL=https://github.com/bblanchon/pdfium-binaries/releases/download/$RELEASE/$ASSET.tgz
DEST=$(dirname "$0")/../third_party/pdfium
[ -f "$DEST/lib/$LIB" ] && { echo "PDFium already present in $DEST"; exit 0; }
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
curl -fsSL -o "$TMP" "$URL"
if command -v sha256sum >/dev/null; then
    echo "$SHA256  $TMP" | sha256sum -c -
else
    echo "$SHA256  $TMP" | shasum -a 256 -c -
fi
mkdir -p "$DEST"
tar -xzf "$TMP" -C "$DEST"
echo "PDFium $RELEASE ($ASSET) installed in $DEST"
