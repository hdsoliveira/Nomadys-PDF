# Nomadys-PDF
PDFIUM  := third_party/pdfium
PREFIX  ?= $(HOME)/.local
CC      ?= cc
CFLAGS  ?= -O2 -g
override CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter \
           -I$(PDFIUM)/include -isystem third_party/stb $(shell pkg-config --cflags sdl3 freetype2 fontconfig)
override LDLIBS += -L$(PDFIUM)/lib -lpdfium $(shell pkg-config --libs sdl3 freetype2 fontconfig) -lz -lm

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
PDFIUM_LIB := $(PDFIUM)/lib/libpdfium.dylib
override CFLAGS += -include src/compat_macos.h
override LDLIBS += -framework CoreServices
# the app bundle keeps the dylib in Contents/Frameworks (see tools/package-macos.sh)
override LDLIBS += -Wl,-rpath,@executable_path/$(PDFIUM)/lib -Wl,-rpath,@executable_path/../Frameworks
else
PDFIUM_LIB := $(PDFIUM)/lib/libpdfium.so
override LDLIBS += -Wl,-rpath,'$$ORIGIN/$(PDFIUM)/lib:$$ORIGIN/../lib/nomadys-pdf'
endif

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

npdf: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c src/nomadys.h src/compat_macos.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

$(OBJ): | $(PDFIUM_LIB)

$(PDFIUM_LIB):
	tools/fetch_pdfium.sh
ifeq ($(UNAME_S),Darwin)
	install_name_tool -id @rpath/libpdfium.dylib $@
endif

# macOS: build dist/Nomadys PDF.app and a .dmg
app: npdf
	tools/package-macos.sh

# a package stages into DESTDIR; the caches are then the package manager's job
D := $(DESTDIR)$(PREFIX)

install: npdf
ifeq ($(UNAME_S),Darwin)
	@echo "make install is for Linux; on macOS run 'make app' and drag dist/Nomadys PDF.app to /Applications" >&2; exit 1
endif
	install -Dm755 npdf $(D)/bin/npdf
	rm -f $(D)/bin/nomadys-pdf  # the old name of the command
	install -Dm644 $(PDFIUM)/lib/libpdfium.so $(D)/lib/nomadys-pdf/libpdfium.so
	install -d $(D)/share/nomadys-pdf/themes
	install -m644 themes/*.conf $(D)/share/nomadys-pdf/themes/
	for n in 16 24 32 48 64 128 256 512; do \
		install -Dm644 icons/nomadys-pdf-$$n.png $(D)/share/icons/hicolor/$${n}x$${n}/apps/nomadys-pdf.png; \
	done
	install -Dm644 docs/icon.svg $(D)/share/icons/hicolor/scalable/apps/nomadys-pdf.svg
	install -Dm644 npdf.desktop $(D)/share/applications/npdf.desktop
	install -d $(D)/share/doc/nomadys-pdf/licenses/themes $(D)/share/doc/nomadys-pdf/licenses/pdfium
	install -m644 LICENSE $(D)/share/doc/nomadys-pdf/
	install -m644 licenses/*.txt licenses/README.md $(D)/share/doc/nomadys-pdf/licenses/
	install -m644 licenses/themes/*.txt $(D)/share/doc/nomadys-pdf/licenses/themes/
	install -m644 $(PDFIUM)/LICENSE $(PDFIUM)/licenses/* $(D)/share/doc/nomadys-pdf/licenses/pdfium/
	@[ -n "$(DESTDIR)" ] || gtk-update-icon-cache -q $(D)/share/icons/hicolor 2>/dev/null || true
	@[ -n "$(DESTDIR)" ] || update-desktop-database -q $(D)/share/applications 2>/dev/null || true

uninstall:
	rm -f $(PREFIX)/bin/npdf $(PREFIX)/bin/nomadys-pdf
	rm -rf $(PREFIX)/lib/nomadys-pdf $(PREFIX)/share/nomadys-pdf/themes  # keeps marks/ (user data)
	rm -f $(PREFIX)/share/icons/hicolor/*/apps/nomadys-pdf.png $(PREFIX)/share/icons/hicolor/scalable/apps/nomadys-pdf.svg
	rm -f $(PREFIX)/share/applications/npdf.desktop
	rm -rf $(PREFIX)/share/doc/nomadys-pdf

clean:
	rm -rf build npdf

.PHONY: install uninstall clean app
