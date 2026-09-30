#!/bin/sh
# Builds the Arch package from packaging/aur/PKGBUILD in an Arch container,
# as an AUR user would, but from this tree instead of the release tarball;
# checks it with namcap and writes packaging/aur/.SRCINFO.
#   tools/aur-build.sh    -> dist/nomadys-pdf-VERSION-1-x86_64.pkg.tar.zst
# tools/distro-test.sh then tries it (kinds pacman, pacman-x11, pacman-wayland).
set -eu
cd "$(dirname "$0")/.."
ENGINE=${ENGINE:-podman}
VERSION=$(sed -n 's/^#define NOMADYS_VERSION "\(.*\)"/\1/p' src/nomadys.h)
grep -q "^pkgver=$VERSION$" packaging/aur/PKGBUILD || { echo "PKGBUILD: pkgver is not $VERSION"; exit 2; }
mkdir -p dist/arch
rm -f dist/arch/* dist/*.pkg.tar.zst
# the tree as GitHub's tag tarball would hold it (tracked files and changes)
git ls-files -co --exclude-standard | grep -v '^packaging/' |
    tar -cz --transform "s,^,Nomadys-PDF-$VERSION/," -T - -f "dist/arch/nomadys-pdf-$VERSION.tar.gz"
cp packaging/aur/PKGBUILD dist/arch/
"$ENGINE" run --rm -v "$PWD/dist/arch:/work:Z" docker.io/library/archlinux:latest sh -c '
set -eu
pacman -Syu --noconfirm --needed base-devel namcap >/dev/null 2>&1
useradd -m builder
cp -r /work /home/builder/pkg && chown -R builder /home/builder/pkg
echo "builder ALL=(ALL) NOPASSWD: ALL" >/etc/sudoers.d/builder
cd /home/builder/pkg
su builder -c "makepkg -s --noconfirm --skipchecksums -f" >/work/makepkg.log 2>&1 ||
    { tail -30 /work/makepkg.log; exit 1; }
su builder -c "makepkg --printsrcinfo" >/work/.SRCINFO
namcap PKGBUILD nomadys-pdf-[0-9]*.pkg.tar.zst >/work/namcap.log 2>&1 || true
cp nomadys-pdf-[0-9]*.pkg.tar.zst /work/
pacman -Qlp nomadys-pdf-[0-9]*.pkg.tar.zst >/work/files.txt
'
mv dist/arch/.SRCINFO packaging/aur/.SRCINFO
mv dist/arch/*.pkg.tar.zst dist/
echo "namcap:"; sed 's/^/  /' dist/arch/namcap.log
ls -l dist/*.pkg.tar.zst
