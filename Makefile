# Nomadys-PDF
PDFIUM  := third_party/pdfium
PREFIX  ?= $(HOME)/.local
CC      ?= cc
CFLAGS  ?= -O2 -g
override CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter \
           -I$(PDFIUM)/include -isystem third_party/stb $(shell pkg-config --cflags sdl3 freetype2 fontconfig)
override LDLIBS += -L$(PDFIUM)/lib -lpdfium $(shell pkg-config --libs sdl3 freetype2 fontconfig) -lz -lm \
           -Wl,-rpath,'$$ORIGIN/$(PDFIUM)/lib:$$ORIGIN/../lib/nomadys-pdf'

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

npdf: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c src/nomadys.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

$(OBJ): | $(PDFIUM)/lib/libpdfium.so

$(PDFIUM)/lib/libpdfium.so:
	tools/fetch_pdfium.sh

# a package stages into DESTDIR; the caches are then the package manager's job
D := $(DESTDIR)$(PREFIX)

install: npdf
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

.PHONY: install uninstall clean
