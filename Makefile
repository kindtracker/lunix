CC = cc
CFLAGS = -O2 -Wall -Wextra
LIBS = -lm -lunicorn

SRC = src
BUILD = build
OUT = $(BUILD)/lunix

CSRC = $(shell find $(SRC) -type f -name '*.c')
COBJ = $(patsubst %.c,$(BUILD)/%.o,$(CSRC))

PROGRAMS := $(patsubst examples/%.c,examples/%,$(wildcard examples/*.c))

all: clean compile programs

clean:
	mkdir -p $(BUILD)
	rm -rf $(BUILD)/*
	mkdir -p $(BUILD)/out
	@for dir in $(PROGRAMS); do \
		rm -rf $$dir/build; \
	done
	@find examples -type f -exec sh -c 'file "$$1" | grep -q "ELF" && rm -f "$$1"' _ {} \;

compile: $(OUT)

run: compile
	./$(OUT) $(filter-out run,$(MAKECMDGOALS))

%:
	@:

$(OUT): $(COBJ)
	$(CC) $(CFLAGS) -o $@ $(COBJ) $(LIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

programs:
	@for program in $(PROGRAMS); do \
		aarch64-linux-gnu-gcc -O0 -nostdlib -static -no-pie $$program.c -o $$program; \
	done

.PHONY: all clean compile run programs
