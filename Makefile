# Flash C build
CC      ?= gcc
CFLAGS  ?= -O2 -std=gnu11 -D_GNU_SOURCE -Wall -Wno-unused-function -Wno-unused-variable -Wno-format-truncation -Wno-stringop-truncation -Wno-misleading-indentation -Wno-parentheses
BUILD   := build
PREFIX  ?= /usr/local

# Version comes from the latest git tag (v1.2.3 -> 1.2.3; commits past the tag add -N-gHASH, a dirty tree adds -dirty).
# Override on the command line with `make VERSION=x.y.z`, e.g. for source tarballs without .git.
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null | sed 's/^v//')
ifeq ($(strip $(VERSION)),)
VERSION := 0.0.0-unknown
endif
CFLAGS  += -DFC_VERSION='"$(VERSION)"'

COMP_SRC := $(wildcard src/compiler/*.c)
IDE_SRC  := $(wildcard src/ide/*.c)
HEADERS  := $(wildcard runtime/include/*.h) $(wildcard runtime/include/sys/*.h)
LIB_START := runtime/lib/c0.c
LIB_C     := $(sort $(wildcard runtime/lib/c/*.c))
LIB_M     := $(sort $(wildcard runtime/lib/m/*.c))
LIB_OBJ   := runtime/lib/wildargs.c
LIB_INT   := $(wildcard runtime/lib/*.h)
LIB_SRC   := $(LIB_START) $(LIB_C) $(LIB_M) $(LIB_OBJ) $(LIB_INT)

COMP_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(COMP_SRC)) $(BUILD)/compiler/embedded.o
IDE_OBJ  := $(patsubst src/%.c,$(BUILD)/%.o,$(IDE_SRC))

all: $(BUILD)/fcc $(BUILD)/fc

# The stamp changes only when the version does, so a new tag or commit rebuilds just what embeds it.
$(BUILD)/version.stamp: FORCE
	@mkdir -p $(BUILD)
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' > $@

FORCE:

version:
	@echo $(VERSION)

$(BUILD)/embed: tools/embed.c
	@mkdir -p $(BUILD)
	$(CC) -O1 -o $@ $<

$(BUILD)/compiler/embedded.c: $(BUILD)/embed $(HEADERS) $(LIB_SRC)
	@mkdir -p $(dir $@)
	$(BUILD)/embed $@ $(addprefix 0:,$(LIB_START)) $(addprefix 1:,$(LIB_C)) $(addprefix 2:,$(LIB_M)) $(addprefix 3:,$(LIB_OBJ)) \
	    $(addprefix h:,$(HEADERS)) $(addprefix i:,$(LIB_INT))

$(BUILD)/compiler/embedded.o: $(BUILD)/compiler/embedded.c
	$(CC) $(CFLAGS) -Isrc/compiler -c -o $@ $<

$(BUILD)/%.o: src/%.c $(wildcard src/compiler/*.h) $(wildcard src/ide/*.h) $(BUILD)/version.stamp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc/compiler -Isrc/ide -c -o $@ $<

$(BUILD)/fcc: $(COMP_OBJ) $(BUILD)/cli/fcc.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/fc: $(COMP_OBJ) $(IDE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ -lutil

# Reformat the C sources with clang-format (rules in .clang-format).
format:
	clang-format -i src/compiler/*.[ch] src/cli/*.c src/ide/*.[ch] tools/*.c runtime/lib/*.[ch] runtime/lib/c/*.c runtime/lib/m/*.c runtime/include/*.h runtime/include/sys/*.h

test: $(BUILD)/fcc $(BUILD)/fc
	sh tests/run.sh
	sh tests/diag.sh
	sh tests/options.sh
	sh tests/opt.sh
	python3 tests/optfuzz.py 60 7
	python3 tests/helpexamples.py
	sh tests/libtest.sh
	python3 tests/ide_smoke.py
	python3 tests/project_smoke.py
	python3 tests/debug_smoke.py

install: all
	install -d $(PREFIX)/bin
	install $(BUILD)/fc $(BUILD)/fcc $(PREFIX)/bin
	install -d $(PREFIX)/share/flashc/help
	install -m 644 help/*.fch $(PREFIX)/share/flashc/help

# Release tarball (build/dist/flash-c-VERSION-linux-x86_64.tar.gz): binaries, help files, docs and install.sh.
DIST_NAME := flash-c-$(VERSION)-linux-$(shell uname -m)
dist: all
	rm -rf $(BUILD)/dist/$(DIST_NAME)
	mkdir -p $(BUILD)/dist/$(DIST_NAME)/bin $(BUILD)/dist/$(DIST_NAME)/help
	install -s $(BUILD)/fc $(BUILD)/fcc $(BUILD)/dist/$(DIST_NAME)/bin
	install -m 644 help/*.fch $(BUILD)/dist/$(DIST_NAME)/help
	cp -R docs $(BUILD)/dist/$(DIST_NAME)/docs
	install -m 644 README.md LICENSE $(BUILD)/dist/$(DIST_NAME)
	install -m 755 tools/dist/install.sh $(BUILD)/dist/$(DIST_NAME)
	echo '$(VERSION)' > $(BUILD)/dist/$(DIST_NAME)/VERSION
	tar -C $(BUILD)/dist -czf $(BUILD)/dist/$(DIST_NAME).tar.gz $(DIST_NAME)

clean:
	rm -rf $(BUILD)
	find . -name __pycache__ -type d -prune -exec rm -rf {} +

help:
	python3 tools/helpgen/gen.py help

.PHONY: FORCE version dist format all test install clean help
