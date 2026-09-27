# Nomadys-PDF
PDFIUM  := third_party/pdfium
PREFIX  ?= $(HOME)/.local
CC      ?= cc
CFLAGS  ?= -O2 -g
override CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter \
           -I$(PDFIUM)/include -isystem third_party/stb $(shell pkg-config --cflags sdl3 freetype2 fontconfig)
override LDLIBS += -L$(PDFIUM)/lib -lpdfium $(shell pkg-config --libs sdl3 freetype2 fontconfig) -lm \
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

# the command is npdf; nomadys-pdf stays as a link to it (older setups)
install: npdf
	install -Dm755 npdf $(PREFIX)/bin/npdf
	ln -sf npdf $(PREFIX)/bin/nomadys-pdf
	install -Dm644 $(PDFIUM)/lib/libpdfium.so $(PREFIX)/lib/nomadys-pdf/libpdfium.so
	install -d $(PREFIX)/share/nomadys-pdf/themes
	install -m644 themes/*.conf $(PREFIX)/share/nomadys-pdf/themes/

uninstall:
	rm -f $(PREFIX)/bin/npdf $(PREFIX)/bin/nomadys-pdf
	rm -rf $(PREFIX)/lib/nomadys-pdf $(PREFIX)/share/nomadys-pdf/themes  # keeps marks/ (user data)

clean:
	rm -rf build npdf

.PHONY: install uninstall clean
