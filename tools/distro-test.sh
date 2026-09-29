#!/bin/sh
# Tries the release packages (tools/package.sh) on several distributions in
# containers, side by side. In each, only what an ordinary system has is
# installed, then npdf is driven by its test mode:
#   appimage, deb  without a window: dark mode, a highlight with a comment,
#                  saving, :response, a theme; it must end cleanly, draw a
#                  page that is not blank, save the highlight and the letter
#   x11, wayland   in a real window, on Xvfb or a headless weston: copying
#                  text (read back from the clipboard: X11 only, a headless
#                  weston has no seat) and following a link (a stand-in
#                  xdg-open records the address)
#   nofonts        without a single font installed: it must not crash
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

# what a desktop has: fontconfig, a font, FreeType, zlib; with gui, a display
# server, the X11 / Wayland / Mesa libraries SDL loads, and clipboard tools
setup() { # distro gui(0/1)
    case $1 in
    ubuntu* | debian*)
        p="fontconfig fonts-dejavu-core libfreetype6 zlib1g"
        [ "$2" = 1 ] && p="$p xvfb weston xclip wl-clipboard libegl1 libgl1 libgl1-mesa-dri libxkbcommon0 \
            libwayland-client0 libwayland-cursor0 libwayland-egl1 libdecor-0-0 libx11-6 libxext6 libxrandr2 \
            libxcursor1 libxi6 libxss1 libxfixes3"
        echo "apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $p >/dev/null" ;;
    fedora*)
        p="fontconfig dejavu-sans-mono-fonts freetype zlib"
        [ "$2" = 1 ] && p="$p xorg-x11-server-Xvfb weston xclip wl-clipboard mesa-dri-drivers mesa-libEGL \
            mesa-libGL libxkbcommon libdecor libXcursor libXi libXrandr libXScrnSaver"
        echo "dnf install -y -q $p >/dev/null" ;;
    archlinux*)
        p="fontconfig ttf-dejavu freetype2 zlib"
        [ "$2" = 1 ] && p="$p xorg-server-xvfb weston xclip wl-clipboard mesa libxkbcommon libdecor libxcursor \
            libxi libxrandr libxss"
        echo "pacman -Sy --noconfirm --needed $p >/dev/null" ;;
    opensuse*)
        p="fontconfig dejavu-fonts libfreetype6 libz1"
        [ "$2" = 1 ] && p="$p xvfb-run xorg-x11-server-Xvfb weston xclip wl-clipboard Mesa-dri Mesa-libEGL1 \
            Mesa-libGL1 libxkbcommon0 libdecor-0-0 libXcursor1 libXi6 libXrandr2 libXss1"
        echo "zypper -q -n in $p >/dev/null" ;;
    esac
}

# without a window, run inside the container as sh -c
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

# in a real window ($BACKEND: x11 or wayland): the clipboard is read while
# npdf still runs (it owns the copied text), the link opens a stand-in
GUI_SESSION='
set -u
mkdir -p /tmp/bin /tmp/xdg && chmod 700 /tmp/xdg
printf "#!/bin/sh\necho \"\$@\" > /out/opened.txt\n" > /tmp/bin/xdg-open && chmod 755 /tmp/bin/xdg-open
export PATH=/tmp/bin:$PATH XDG_RUNTIME_DIR=/tmp/xdg
cp /tests/link.pdf /tmp/l.pdf
if [ "$BACKEND" = x11 ]; then
    Xvfb :99 -screen 0 1280x1024x24 >/out/server.log 2>&1 &
    export DISPLAY=:99
    read_clipboard() { xclip -o -selection clipboard 2>>/out/server.log; }
else
    for opts in "--backend=headless --renderer=pixman" "--backend=headless-backend.so --use-pixman"; do
        weston $opts --socket=wayland-1 --idle-time=0 >/out/server.log 2>&1 &
        sleep 2
        [ -S /tmp/xdg/wayland-1 ] && break
    done
    export WAYLAND_DISPLAY=wayland-1
    read_clipboard() { wl-paste -n 2>>/out/server.log; }
fi
sleep 2
cd /tmp
SDL_VIDEO_DRIVER=$BACKEND APPIMAGE_EXTRACT_AND_RUN=1 XDG_CONFIG_HOME=/tmp/cfg XDG_DATA_HOME=/tmp/data \
    timeout 120 $NPDF --test-out /out --test-script "<resize:900,1100>vvwwwy<wait:6000><Esc>fs<wait:500>i<shot:page>" /tmp/l.pdf >/out/run.log 2>&1 &
app=$!
sleep 4
read_clipboard >/out/clipboard.txt
wait $app
code=$?
echo "exit $code" >>/out/run.log
[ $code -eq 0 ] || { echo "FAIL: exit $code (see run.log, server.log)"; exit 1; }
[ -s /out/page.bmp ] || { echo "FAIL: no screenshot"; exit 1; }
colours=$(od -An -v -tx4 -j 54 /out/page.bmp | tr -s " " "\n" | sort -u | wc -l)
[ "$colours" -gt 3 ] || { echo "FAIL: blank window ($colours colours)"; exit 1; }
grep -q "example.org/nomadys-test" /out/opened.txt 2>/dev/null || { echo "FAIL: link not opened"; exit 1; }
if [ "$BACKEND" = x11 ]; then
    grep -q "Nomadys" /out/clipboard.txt || { echo "FAIL: clipboard empty"; exit 1; }
    echo OK
else # a headless weston has no seat, without which Wayland gives no clipboard
    echo "OK (clipboard not testable here)"
fi
'

run() { # distro kind  -> one line
    distro=$1 kind=$2
    dir=dist/test/$(echo "$distro" | tr ':/' '--')-$kind
    rm -rf "$dir" && mkdir -p "$dir"
    session=$SESSION backend=
    case $kind in
    appimage) prep=$(setup "$distro" 0); npdf=/pkg/$(basename "$APPIMAGE") ;;
    deb) prep="$(setup "$distro" 0) && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq /pkg/$(basename "$DEB") >/dev/null"; npdf=npdf ;;
    x11 | wayland) prep=$(setup "$distro" 1); npdf=/pkg/$(basename "$APPIMAGE"); session=$GUI_SESSION backend=$kind ;;
    nofonts) # the libraries, but not one font
        prep="apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends libfontconfig1 libfreetype6 zlib1g >/dev/null"
        npdf=/pkg/$(basename "$APPIMAGE") ;;
    esac
    case $distro in */*) image=docker.io/$distro ;; *) image=docker.io/library/$distro ;; esac
    result=$("$ENGINE" run --rm -v "$PWD/dist:/pkg:ro,Z" -v "$PWD/tests:/tests:ro,Z" -v "$PWD/$dir:/out:Z" \
        -e NPDF="$npdf" -e BACKEND="$backend" "$image" sh -c "$prep && sh -c '$session'" 2>"$dir/container.log" |
        tail -1)
    printf '%-28s %-9s %s\n' "$distro" "$kind" "${result:-FAIL: see $dir/container.log}"
}

for d in $DISTROS; do
    run "$d" appimage &
    run "$d" x11 &
    run "$d" wayland &
    case $d in ubuntu* | debian*) run "$d" deb & ;; esac
done
run debian:13 nofonts &
wait
