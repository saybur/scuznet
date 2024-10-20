JLCPCB PCB assembly files
=========================

This is a set of files that are useful for JLCPCB PCB Assembly service
They only contain the SMT components, you may have to preorder some components and
will have to do the THT ones by yourself

A few things differs from the main bom:
- USB: this part isn't included as not used
- LEDs: use JLC 8005 basic LEDs (green and yellow)
	- they do need to be run at 1mA otherwise they are too bright
	- R11, R14 and R15 values increased to down to 1mA
- SCSI termination: build for 110Ω packs (R20 = 330Ω, R21 = 422Ω)
- MCU: using ATXMEGA128A3U because stock availability you can use ATXMEGA192A3U
- Power: Uses SS34 instead of SS23, because cheaper and more powerful
