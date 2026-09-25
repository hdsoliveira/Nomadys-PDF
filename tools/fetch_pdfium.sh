#!/bin/sh
# Download the pinned PDFium build into third_party/pdfium and verify it.
set -eu
RELEASE=chromium/8066
SHA256=0b43f405477cf2cfc4dbff06905093c3309756c6bca1fb9da99234a2ca97fed2
URL=https://github.com/bblanchon/pdfium-binaries/releases/download/$RELEASE/pdfium-linux-x64.tgz
DEST=$(dirname "$0")/../third_party/pdfium
[ -f "$DEST/lib/libpdfium.so" ] && { echo "PDFium already present in $DEST"; exit 0; }
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
curl -fsSL -o "$TMP" "$URL"
echo "$SHA256  $TMP" | sha256sum -c -
mkdir -p "$DEST"
tar -xzf "$TMP" -C "$DEST"
echo "PDFium $RELEASE installed in $DEST"
