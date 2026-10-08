include $(DEVKITPPC)/wii_rules
OUT ?= build/wii/arm-integer-conformance
DEST ?= 0
SUBTRACT ?= 0
CFLAGS := -O2 -Wall -Wextra $(MACHDEP) -I$(LIBOGC_INC) -DDEST=$(DEST) -DSUBTRACT=$(SUBTRACT)
all:
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c wii/diagnostics/arm_integer/main.c -o $(OUT)/main.o
	$(CC) $(CFLAGS) -c wii/diagnostics/arm_integer/probe.S -o $(OUT)/probe.o
	$(CC) $(MACHDEP) $(OUT)/main.o $(OUT)/probe.o -L$(LIBOGC_LIB) -logc -lm -Wl,-Map,$(OUT)/probe.map -o $(OUT)/probe.elf
	elf2dol $(OUT)/probe.elf $(OUT)/probe.dol
	$(DEVKITPPC)/bin/powerpc-eabi-objdump -d $(OUT)/probe.elf > $(OUT)/disassembly.txt
