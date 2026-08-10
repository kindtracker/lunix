CC = cc
CFLAGS = -O2 -Wall -Wextra -static
LIBS = -lm

ASSET = asset
SRC = lunix
BUILD = build
OUT = $(BUILD)/lunix.bin

CSRC = $(shell find $(SRC) -type f -name '*.c')
COBJ = $(patsubst %.c,$(BUILD)/%.o,$(CSRC))

PROGRAMS := $(shell find programs -mindepth 2 -maxdepth 2 -name Makefile -printf '%h\n')

all: clean compile programs

clean:
	mkdir -p $(BUILD)
	rm -rf $(BUILD)/*
	@for dir in $(PROGRAMS); do \
		rm -rf $$dir/build; \
	done

compile: $(OUT)
	cp -R $(ASSET) $(BUILD)/.

run:
	./$(OUT)

$(OUT): $(COBJ)
	$(CC) $(CFLAGS) -o $@ $(COBJ) $(LIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

programs:
	@for dir in $(PROGRAMS); do \
		$(MAKE) -C $$dir; \
	done

.PHONY: all clean compile run programs
