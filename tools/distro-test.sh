#!/bin/sh
# Tries the release packages (tools/package.sh) on several distributions in
# containers, side by side: the AppImage everywhere, the .deb on Debian and
# Ubuntu. Each run installs only what an ordinary system has, then drives
# npdf without a window (dark mode, a highlight with a comment, saving,
# :response, a theme) and checks that it ends cleanly, that the page it drew
# is not blank and that the saved PDF holds the highlight. A last run has no
# fonts at all, and must still not crash.
#   tools/distro-test.sh [DISTRO...]     (default: all below)
# Logs and screenshots in dist/test/DISTRO-KIND/.
set -eu
cd "$(dirname "$0")/.."
ENGINE=${ENGINE:-podman}
APPIMAGE=$(ls -t dist/*.AppImage 2>/dev/null | head -1)
DEB=$(ls -t dist/*.deb 2>/dev/null | head -1)
[ -n "$APPIMAGE" ] && [ -n "$DEB" ] || { echo "no packages in dist/: run tools/package.sh first"; exit 2; }
ALL="ubuntu:22.04 ubuntu:24.04 debian:12 debian:13 fedora:latest archlinux:latest opensuse/tumbleweed:latest"
DISTROS=${*:-$ALL}

# what a desktop has: fontconfig, a font, FreeType, zlib
setup() {
    case $1 in
    ubuntu* | debian*) echo "apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq fontconfig fonts-dejavu-core libfreetype6 zlib1g >/dev/null" ;;
    fedora*) echo "dnf install -y -q fontconfig dejavu-sans-mono-fonts freetype zlib >/dev/null" ;;
    archlinux*) echo "pacman -Sy --noconfirm --needed fontconfig ttf-dejavu freetype2 zlib >/dev/null" ;;
    opensuse*) echo "zypper -q -n in fontconfig dejavu-fonts libfreetype6 libz1 >/dev/null" ;;
    esac
}

# the session and its checks, run inside the container as sh -c
SESSION='
set -u
cp /tests/sample.pdf /tmp/s.pdf
cd /tmp
SDL_VIDEO_DRIVER=offscreen APPIMAGE_EXTRACT_AND_RUN=1 XDG_CONFIG_HOME=/tmp/cfg XDG_DATA_HOME=/tmp/data \
    timeout 120 $NPDF --test-out /out --test-script "<resize:900,1100>ivvwwwCcchecked<Enter>:w<Enter>:response<Enter>:theme nord<Enter><shot:page>" /tmp/s.pdf >/out/run.log 2>&1
code=$?
echo "exit $code" >>/out/run.log
[ $code -eq 0 ] || { echo "FAIL: exit $code"; exit 1; }
[ -s /out/page.bmp ] || { echo "FAIL: no screenshot"; exit 1; }
colours=$(od -An -v -tx4 -j 54 /out/page.bmp | tr -s " " "\n" | sort -u | wc -l)
[ "$colours" -gt 20 ] || { echo "FAIL: blank page ($colours colours)"; exit 1; }
grep -q "/Highlight" /tmp/s.pdf || { echo "FAIL: highlight not saved"; exit 1; }
[ -s /tmp/s-response.md ] || { echo "FAIL: no :response letter"; exit 1; }
echo OK
'

run() { # distro kind  -> one line
    distro=$1 kind=$2
    dir=dist/test/$(echo "$distro" | tr ':/' '--')-$kind
    rm -rf "$dir" && mkdir -p "$dir"
    case $kind in
    appimage) prep=$(setup "$distro"); npdf=/pkg/$(basename "$APPIMAGE") ;;
    deb) prep="$(setup "$distro") && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq /pkg/$(basename "$DEB") >/dev/null"; npdf=npdf ;;
    nofonts) # the libraries, but not one font
        prep="apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends libfontconfig1 libfreetype6 zlib1g >/dev/null"
        npdf=/pkg/$(basename "$APPIMAGE") ;;
    esac
    case $distro in */*) image=docker.io/$distro ;; *) image=docker.io/library/$distro ;; esac
    result=$("$ENGINE" run --rm -v "$PWD/dist:/pkg:ro,Z" -v "$PWD/tests:/tests:ro,Z" -v "$PWD/$dir:/out:Z" \
        -e NPDF="$npdf" "$image" sh -c "$prep && sh -c '$SESSION'" 2>"$dir/container.log" |
        tail -1)
    printf '%-28s %-9s %s\n' "$distro" "$kind" "${result:-FAIL: see $dir/container.log}"
}

for d in $DISTROS; do
    run "$d" appimage &
    case $d in ubuntu* | debian*) run "$d" deb & ;; esac
done
run debian:13 nofonts &
wait
