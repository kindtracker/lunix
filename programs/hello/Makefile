CC = cc

CFLAGS = -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pie -I../../lunix/
LDFLAGS = -nostdlib -e _start -static -no-pie

SRC = src
BUILD = build
OUT = $(BUILD)/hello

CSRC = $(shell find $(SRC) -type f -name '*.c')
COBJ = $(patsubst %.c,$(BUILD)/%.o,$(CSRC))

all: $(OUT)

$(OUT): $(COBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: all clean
