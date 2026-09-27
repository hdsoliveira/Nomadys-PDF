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
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c src/nomadys.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

$(OBJ): | $(PDFIUM)/lib/libpdfium.so

$(PDFIUM)/lib/libpdfium.so:
	tools/fetch_pdfium.sh

install: npdf
	install -Dm755 npdf $(PREFIX)/bin/npdf
	rm -f $(PREFIX)/bin/nomadys-pdf  # the old name of the command
	install -Dm644 $(PDFIUM)/lib/libpdfium.so $(PREFIX)/lib/nomadys-pdf/libpdfium.so
	install -d $(PREFIX)/share/nomadys-pdf/themes
	install -m644 themes/*.conf $(PREFIX)/share/nomadys-pdf/themes/
	for n in 16 24 32 48 64 128 256 512; do \
		install -Dm644 icons/nomadys-pdf-$$n.png $(PREFIX)/share/icons/hicolor/$${n}x$${n}/apps/nomadys-pdf.png; \
	done
	install -Dm644 docs/icon.svg $(PREFIX)/share/icons/hicolor/scalable/apps/nomadys-pdf.svg
	install -Dm644 npdf.desktop $(PREFIX)/share/applications/npdf.desktop
	@gtk-update-icon-cache -q $(PREFIX)/share/icons/hicolor 2>/dev/null || true
	@update-desktop-database -q $(PREFIX)/share/applications 2>/dev/null || true

uninstall:
	rm -f $(PREFIX)/bin/npdf $(PREFIX)/bin/nomadys-pdf
	rm -rf $(PREFIX)/lib/nomadys-pdf $(PREFIX)/share/nomadys-pdf/themes  # keeps marks/ (user data)
	rm -f $(PREFIX)/share/icons/hicolor/*/apps/nomadys-pdf.png $(PREFIX)/share/icons/hicolor/scalable/apps/nomadys-pdf.svg
	rm -f $(PREFIX)/share/applications/npdf.desktop

clean:
	rm -rf build npdf

.PHONY: install uninstall clean
