CC = cc
CFLAGS = -O2 -Wall -Wextra
LIBS = -lm -lunicorn

SRC = src
BUILD = build
OUT = $(BUILD)/lunix

CSRC = $(shell find $(SRC) -type f -name '*.c')
COBJ = $(patsubst %.c,$(BUILD)/%.o,$(CSRC))

examples := $(patsubst examples/%.c,examples/%,$(wildcard examples/*.c))

all: clean compile examples

clean:
	mkdir -p $(BUILD)
	rm -rf $(BUILD)/*
	mkdir -p $(BUILD)/out
	@for dir in $(examples); do \
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

examples:
	@for program in $(examples); do \
		if grep -q "void _start" $$program.c; then \
			aarch64-linux-gnu-gcc -O0 -nostdlib -static -no-pie $$program.c -o $$program; \
		else \
			aarch64-linux-gnu-gcc -O0 -static -no-pie $$program.c -o $$program; \
		fi; \
	done

.PHONY: all clean compile run examples
