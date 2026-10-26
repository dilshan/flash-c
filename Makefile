# Flash C build
CC      ?= gcc
CFLAGS  ?= -O2 -std=gnu11 -D_GNU_SOURCE -Wall -Wno-unused-function -Wno-unused-variable -Wno-format-truncation -Wno-stringop-truncation -Wno-misleading-indentation -Wno-parentheses
BUILD   := build
PREFIX  ?= /usr/local

COMP_SRC := $(wildcard src/compiler/*.c)
IDE_SRC  := $(wildcard src/ide/*.c)
HEADERS  := $(wildcard runtime/include/*.h)

COMP_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(COMP_SRC)) $(BUILD)/compiler/embedded.o
IDE_OBJ  := $(patsubst src/%.c,$(BUILD)/%.o,$(IDE_SRC))

all: $(BUILD)/fcc $(BUILD)/fc

$(BUILD)/embed: tools/embed.c
	@mkdir -p $(BUILD)
	$(CC) -O1 -o $@ $<

$(BUILD)/compiler/embedded.c: $(BUILD)/embed runtime/rt.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(BUILD)/embed $@ runtime/rt.c $(HEADERS)

$(BUILD)/compiler/embedded.o: $(BUILD)/compiler/embedded.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/%.c $(wildcard src/compiler/*.h) $(wildcard src/ide/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc/compiler -Isrc/ide -c -o $@ $<

$(BUILD)/fcc: $(COMP_OBJ) $(BUILD)/cli/fcc.o
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/fc: $(COMP_OBJ) $(IDE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test: $(BUILD)/fcc $(BUILD)/fc
	sh tests/run.sh
	sh tests/diag.sh
	python3 tests/ide_smoke.py

install: all
	install -d $(PREFIX)/bin
	install $(BUILD)/fc $(BUILD)/fcc $(PREFIX)/bin

clean:
	rm -rf $(BUILD)

.PHONY: all test install clean
