#!/bin/sh
# Runs inside the Ubuntu 22.04 container started by tools/package.sh: builds
# SDL3 and npdf, then the AppImage and the .deb in /out. /src is the source
# tree (read-only), VERSION comes from package.sh. With rootless podman,
# root in here is you outside: the files in /out are yours.
set -eu
SDL_VERSION=3.4.16
SDL_SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
APPIMAGETOOL=1.9.1
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
CACHE=/out/cache
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential pkg-config cmake ninja-build ca-certificates curl file \
    libfreetype-dev libfontconfig-dev zlib1g-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev libwayland-bin wayland-protocols libdecor-0-dev \
    libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev libdbus-1-dev libibus-1.0-dev libudev-dev >/dev/null

# SDL3: the video and events npdf uses (X11 and Wayland, loaded at run time)
[ -f "$CACHE/SDL3-$SDL_VERSION.tar.gz" ] ||
    curl -fsSL -o "$CACHE/SDL3-$SDL_VERSION.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
echo "$SDL_SHA256  $CACHE/SDL3-$SDL_VERSION.tar.gz" | sha256sum -c - # what was downloaded is what was published
tar -xzf "$CACHE/SDL3-$SDL_VERSION.tar.gz" -C /tmp
cmake -S /tmp/SDL3-$SDL_VERSION -B /tmp/sdl-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/sdl3 -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_STATIC=OFF \
    -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF >/dev/null
cmake --build /tmp/sdl-build >/dev/null
cmake --install /tmp/sdl-build >/dev/null
export PKG_CONFIG_PATH=/opt/sdl3/lib/pkgconfig

# npdf, from a copy of the tree (/src is read-only, and its build/ is the host's)
mkdir /build
(cd /src && tar -cf - --exclude=./build --exclude=./dist --exclude=./npdf --exclude=./npdf-asan .) | tar -xf - -C /build
cd /build
make -j"$(nproc)" >/dev/null

# one staging tree, as installed under /usr, with SDL3 next to PDFium
stage() {
    make install PREFIX="$1/usr" >/dev/null
    cp -L /opt/sdl3/lib/libSDL3.so.0 "$1/usr/lib/nomadys-pdf/"
    install -m644 /tmp/SDL3-$SDL_VERSION/LICENSE.txt "$1/usr/share/doc/nomadys-pdf/licenses/SDL3.txt"
    strip --strip-unneeded "$1/usr/bin/npdf" "$1/usr/lib/nomadys-pdf/"*.so*
}

# the AppImage: npdf finds its libraries and themes next to itself
APPDIR=/tmp/Nomadys-PDF.AppDir
stage "$APPDIR"
cp npdf.desktop "$APPDIR/"
cp icons/nomadys-pdf-256.png "$APPDIR/nomadys-pdf.png"
ln -s nomadys-pdf.png "$APPDIR/.DirIcon"
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
exec "$here/usr/bin/npdf" "$@"
EOF
chmod 755 "$APPDIR/AppRun"
[ -f "$CACHE/appimagetool-$APPIMAGETOOL" ] || {
    curl -fsSL -o "$CACHE/appimagetool-$APPIMAGETOOL" \
        "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL/appimagetool-x86_64.AppImage"
    chmod 755 "$CACHE/appimagetool-$APPIMAGETOOL"
}
echo "$APPIMAGETOOL_SHA256  $CACHE/appimagetool-$APPIMAGETOOL" | sha256sum -c -
APPIMAGE=/out/Nomadys-PDF-$VERSION-x86_64.AppImage
# written under another name and then renamed over the old one: an AppImage that is
# running ("Text file busy") cannot be written to, but its name can be taken over
ARCH=x86_64 "$CACHE/appimagetool-$APPIMAGETOOL" --appimage-extract-and-run --no-appstream \
    "$APPDIR" "$APPIMAGE.new" >/dev/null 2>&1
mv -f "$APPIMAGE.new" "$APPIMAGE"

# the .deb: the same files under /usr; Debian versions sort "0.5.0~beta" before "0.5.0"
DEB=/tmp/deb
DEBVERSION=$(echo "$VERSION" | tr '-' '~')
stage "$DEB"
mkdir -p "$DEB/DEBIAN"
cat > "$DEB/DEBIAN/control" <<EOF
Package: nomadys-pdf
Version: $DEBVERSION
Architecture: amd64
Maintainer: Hugo de Souza Oliveira <hs.oliveira@mailbox.org>
Installed-Size: $(du -sk "$DEB/usr" | cut -f1)
Depends: libc6 (>= 2.35), libfontconfig1, libfreetype6, zlib1g
Recommends: xdg-utils
Suggests: texlive-latex-base, curl, cups-client
Section: text
Priority: optional
Homepage: https://github.com/hdsoliveira/Nomadys-PDF
Description: keyboard-driven PDF reader for research
 A minimal, fast PDF reader driven by vim keys: highlights and comments with
 LaTeX equations saved in the PDF, tasks, replies to reviewers, search across
 all your PDFs, SyncTeX with Vim, a dark mode that keeps photographs, themes.
EOF
dpkg-deb --root-owner-group -Zxz --build "$DEB" "/out/nomadys-pdf_${DEBVERSION}_amd64.deb" >/dev/null

