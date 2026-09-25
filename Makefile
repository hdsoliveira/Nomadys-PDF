# Nomadys-PDF
PDFIUM  := third_party/pdfium
PREFIX  ?= $(HOME)/.local
CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter \
           -I$(PDFIUM)/include $(shell pkg-config --cflags sdl3 freetype2 fontconfig)
LDLIBS  += -L$(PDFIUM)/lib -lpdfium $(shell pkg-config --libs sdl3 freetype2 fontconfig) -lm \
           -Wl,-rpath,'$$ORIGIN/$(PDFIUM)/lib:$$ORIGIN/../lib/nomadys-pdf'

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

nomadys-pdf: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c src/nomadys.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

$(OBJ): | $(PDFIUM)/lib/libpdfium.so

$(PDFIUM)/lib/libpdfium.so:
	tools/fetch_pdfium.sh

install: nomadys-pdf
	install -Dm755 nomadys-pdf $(PREFIX)/bin/nomadys-pdf
	install -Dm644 $(PDFIUM)/lib/libpdfium.so $(PREFIX)/lib/nomadys-pdf/libpdfium.so
	install -d $(PREFIX)/share/nomadys-pdf/themes
	install -m644 themes/*.conf $(PREFIX)/share/nomadys-pdf/themes/

uninstall:
	rm -f $(PREFIX)/bin/nomadys-pdf
	rm -rf $(PREFIX)/lib/nomadys-pdf $(PREFIX)/share/nomadys-pdf/themes  # keeps marks/ (user data)

clean:
	rm -rf build nomadys-pdf

.PHONY: install uninstall clean
