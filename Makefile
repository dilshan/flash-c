# Flash C build
CC      ?= gcc
CFLAGS  ?= -O2 -std=gnu11 -D_GNU_SOURCE -Wall -Wno-unused-function -Wno-unused-variable -Wno-format-truncation -Wno-stringop-truncation -Wno-misleading-indentation -Wno-parentheses
BUILD   := build
PREFIX  ?= /usr/local

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

$(BUILD)/embed: tools/embed.c
	@mkdir -p $(BUILD)
	$(CC) -O1 -o $@ $<

$(BUILD)/compiler/embedded.c: $(BUILD)/embed $(HEADERS) $(LIB_SRC)
	@mkdir -p $(dir $@)
	$(BUILD)/embed $@ $(addprefix 0:,$(LIB_START)) $(addprefix 1:,$(LIB_C)) $(addprefix 2:,$(LIB_M)) $(addprefix 3:,$(LIB_OBJ)) \
	    $(addprefix h:,$(HEADERS)) $(addprefix i:,$(LIB_INT))

$(BUILD)/compiler/embedded.o: $(BUILD)/compiler/embedded.c
	$(CC) $(CFLAGS) -Isrc/compiler -c -o $@ $<

$(BUILD)/%.o: src/%.c $(wildcard src/compiler/*.h) $(wildcard src/ide/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc/compiler -Isrc/ide -c -o $@ $<

$(BUILD)/fcc: $(COMP_OBJ) $(BUILD)/cli/fcc.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/fc: $(COMP_OBJ) $(IDE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ -lutil

test: $(BUILD)/fcc $(BUILD)/fc
	sh tests/run.sh
	sh tests/diag.sh
	sh tests/options.sh
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

clean:
	rm -rf $(BUILD)
	find . -name __pycache__ -type d -prune -exec rm -rf {} +

help:
	python3 tools/helpgen/gen.py help

.PHONY: all test install clean help
