# ============================================================================
#  Edit the following lines to match the hardware revision, CPU type, and
#  the programmer being used.
# ============================================================================

OPTIONS := -DHW_VXXX
PROGRAMMER := avrispv2
MCU := atxmega192a3u

# ============================================================================
#  Use caution editing the following values.
# ============================================================================

F_CPU := 32000000
WARNINGS := -Wall -Wextra -pedantic -Waddr-space-convert
CORE_OPTS := -Os -fshort-enums
CC := avr-gcc
INC := -I. -Ilib/ff -Ilib/inih
VPATH := .:lib/ff:lib/inih
CFLAGS ?= $(WARNINGS) $(CORE_OPTS) -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(INC) $(OPTIONS)
ASFLAGS ?= -mmcu=$(MCU) -DF_CPU=$(F_CPU)
AVRDUDE_FLAGS := -p $(MCU) -c $(PROGRAMMER) -P usb

MAIN = scuznet
CSRCS = config.c debug.c disk.c enc.c net.c init.c phy.c logic.c mode.c hdd.c \
		cdrom.c link.c test.c ff.c ffunicode.c ini.c main.c
ASRCS = 
OBJS = $(ASRCS:.S=.o) $(CSRCS:.c=.o)

.PHONY: all
all: $(MAIN).bin

.PHONY: clean
clean:
	rm -f $(MAIN).elf $(MAIN).hex $(MAIN).bin $(MAIN).lst $(OBJS)

.PHONY: flash
flash: $(MAIN).hex
	avrdude $(AVRDUDE_FLAGS) -e -U $<

.PHONY: reset
reset:
	avrdude $(AVRDUDE_FLAGS)

.PHONY: dump
dump: $(MAIN).elf
	avr-objdump -d -S -m avr $(MAIN).elf > $(MAIN).lst

$(MAIN).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ -g $(OBJS)
	avr-size -C --mcu=$(MCU) $@

$(MAIN).hex: $(MAIN).elf
	avr-objcopy -j .text -j .data -O ihex $< $@

$(MAIN).bin: $(MAIN).hex
	avr-objcopy -I ihex -O binary $< $@
