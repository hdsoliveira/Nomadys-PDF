#!/bin/sh
# Release packages, built in an Ubuntu 22.04 container so they run on that
# and anything newer (glibc 2.35): an AppImage for every distribution and a
# .deb for Debian / Ubuntu, both with SDL3 and PDFium inside.
#   tools/package.sh          -> dist/Nomadys-PDF-VERSION-x86_64.AppImage
#                                dist/nomadys-pdf_VERSION_amd64.deb
# Needs podman (or docker: ENGINE=docker tools/package.sh). The downloads
# (Ubuntu packages, SDL3, appimagetool) are cached in dist/cache.
set -eu
cd "$(dirname "$0")/.."
ENGINE=${ENGINE:-podman}
VERSION=$(sed -n 's/^#define NOMADYS_VERSION "\(.*\)"/\1/p' src/nomadys.h)
[ -f third_party/pdfium/lib/libpdfium.so ] || tools/fetch_pdfium.sh
mkdir -p dist/cache
"$ENGINE" run --rm \
    -v "$PWD:/src:ro,Z" -v "$PWD/dist:/out:Z" \
    -e VERSION="$VERSION" \
    docker.io/library/ubuntu:22.04 sh /src/tools/package-inside.sh
ls -l dist/*.AppImage dist/*.deb
