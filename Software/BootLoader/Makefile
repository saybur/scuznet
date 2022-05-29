# ============================================================================
#  Edit these lines to match the microcontroller type and programmer
#
#  The BOOTSTART value must match the beginning of the bootloader section on
#  your device, as a byte address. This can be found in the device datasheet.
#  For reference:
#
#    atxmega64a3u:  0x10000
#    atxmega128a3u: 0x20000
#    atxmega192a3u: 0x30000
#    atxmega256a3u: 0x40000
# ============================================================================

MCU := atxmega192a3u
BOOTSTART := 0x30000

PROGRAMMER := avrispv2
AVRDUDE_FLAGS := -p $(MCU) -c $(PROGRAMMER) -P usb

# ============================================================================
#  Use caution changing the following values.
# ============================================================================

CC := avr-gcc
F_CPU := 2000000
WARNINGS := -Wall -Wextra -pedantic -Waddr-space-convert
CORE_OPTS := -Os -std=c99 -flto -mrelax
INC := -I. -Ilib/pff -Ilib/avr1316
VPATH := .:lib/pff:lib/avr1316
CFLAGS ?= $(WARNINGS) $(CORE_OPTS) -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(INC)
ASFLAGS ?= -mmcu=$(MCU) -DF_CPU=$(F_CPU)
LDFLAGS += -Wl,-Ttext=$(BOOTSTART)

MAIN := program
ASRCS := sp_driver.S
CSRCS := disk.c pff.c main.c
OBJS := $(ASRCS:.S=.o) $(CSRCS:.c=.o)

.PHONY: all
all: $(MAIN).bin

.PHONY: clean
clean:
	rm -f $(MAIN).elf $(MAIN).hex $(MAIN).bin $(MAIN).lst $(OBJS)

.PHONY: flash
flash: $(MAIN).bin
	avrdude $(AVRDUDE_FLAGS) -e -U boot:w:$<

.PHONY: reset
reset:
	avrdude $(AVRDUDE_FLAGS)

.PHONY: fuses
fuses:
	avrdude $(AVRDUDE_FLAGS) -U fuse2:w:0xBF:m

.PHONY: dump
dump: $(MAIN).elf
	avr-objdump -d -S -m avr $< > $(MAIN).lst

$(MAIN).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ -g $(OBJS)
	avr-size -C --mcu=$(MCU) $@

$(MAIN).hex: $(MAIN).elf
	avr-objcopy -j .text -j .data -O ihex $< $@

$(MAIN).bin: $(MAIN).hex
	avr-objcopy -I ihex -O binary $< $@
