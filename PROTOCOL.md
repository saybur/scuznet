Device Protocol Description
===========================

This describes the protocol used between the Nuvolink SC and the device driver.

## Hardware

The real hardware consists of the following main components:

* 80C188 microcontroller
* 5380 SCSI controller
* 8390 Ethernet controller
* 32K SRAM
* 32K ROM

## Known Supported Commands

These are the SCSI commands supported by this device, organized by opcode. Each
command name is suffixed with the diagnostic flag that the device driver uses
for reporting how many times each command has been received by the device.

Note: there is one command, "cmmd_dummy" listed in the driver utility that I
have never observed.  When seen it can be added below.

Unless otherwise noted below, after receiving the command bytes the device will
transition to the STATUS phase, send GOOD, transition to MESSAGE IN, send
COMMAND COMPLETE, and then release the bus.

### 0x00: TEST UNIT READY (cmmd_tstrdy)

No special notes.

### 0x02: "Reset Statistics" Vendor Specific Command (cmmd_ethrst)

This six byte command is sent when the driver has the "reset statistics" button
clicked.  All bytes but the opcode are zero.

### 0x03: REQUEST SENSE (cmmd_rqsens)

This has never been observed, but the driver tool has a field for "cmd_rqsens"
that likely refers to this command, which is mandatory in the spec.

### 0x05: "Send Packet" Vendor Specific Command (cmd_ethwrt)

This is covered in detail in Standard Packet Transmission later in this
document.

### 0x06: "Change MAC Address" Vendor Specific Command (cmd_addr)

This six byte command is sent during startup to change the MAC address from the
default.  This is only sent if the MAC needs to be changed.  All bytes are 0x00
except byte 4, which is the allocation length, set to 6.

After receiving this command, the target moves to DATA OUT and accepts 6 bytes,
which is the whole MAC address to apply to the device.

Subsequent INQUIRY commands will show this new MAC address in the configured
MAC address bytes.

### 0x08: GET MESSAGE(6) (probably cmmd_getmsg)

See initialization section for details.

### 0x09: "Set Multicast Registers" (cmmd_mcast)

Six-byte command, as follows:

* Byte 4: Length of following data (only value seen is 0x08).
* All other bytes are zero.

After reception, the target moves to DATA OUT and accepts 8 bytes.

The 8 data byte patterns seen are as follows:

* 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 (usually before the 0x0C 
  command)
* 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80 (usually after the 0x0C
  command)

These may be for the 8390’s multicast address registers (MAR0-MAR7). From what
I can find, this doesn't make a ton of sense, as it would only allow a very
particular set of multicast addresses to be allowed into the device, and would
not include the 09:00:07:FF:FF:FF address used by AppleTalk.

### 0x0A: SEND MESSAGE(6) (probably cmmd_sndmsg)

Presence inferred from cmmd_sndmsg and spec requirements.  Never observed.

### 0x0C: Unknown Vendor Specific Command (probably cmmd_mdsens)

Six-byte command, as follows:

* Byte 2: 0x02 or 0x04
* All other bytes are zero.

This only seems to occur during startup and shutdown.

### 0x12: INQUIRY (cmmd_inq)

This always seems to be directed toward page code 0x02.  DATA IN follows
reception of this command.  When allocated 96 bytes (standard length), which is
the usual behavior, this is what it responds with:

* 0: 0x09 (communications device)
* 1: 0x00
* 2: 0x02 (SCSI-2 device)
* 3: 0x02 (SCSI-2 response type)
* 4: 0x00 (no additional length)
* 5: 0x00
* 6: 0x00
* 7: 0x00
* 8-15: 0x4E, 0x75, 0x76, 0x6F, 0x74, 0x65, 0x63, 0x68 ("Nuvotech")
* 16-21: 0x4E, 0x75, 0x76, 0x6F, 0x53, 0x43, ("NuvoSC")
* 22-31: 10 bytes of 0x00 (rest of name, which is not used)
* 32-35: 0x31, 0x2E, 0x31, 0x72 ("1.1r")
* 36-41: six byte MAC address currently configured
* 42-55: 14 bytes of 0x00
* 56-61: six byte MAC built-in hardware address
* 62-95: 34 bytes of 0x00

It will also sometimes allocate 292 bytes, using the the reserved byte 3 of
INQUIRY as the MSB of a 16 bit value.  The first 96 bytes are identical to the
above items.  The remaining bytes include a bunch of statistics available in
the diagnostic utility.

* 96-97: 0x04D2 (decimal 1234): header for SCSI statistics section
* 98-183: SCSI statistics bytes
* 184-185: 0x0929 (decimal 2345): header for SCSI errors section
* 186-243: SCSI error counting bytes
* 244-245: 0x0D80 (decimal 3456): header for network statistics
* 246-249: unsigned 32 bit value for received packet count
* 250-253: unsigned 32 bit value for transmitted packet count
* 254-257: unsigned 32 bit value for transmit request count
* 258-259: unsigned 16 bit value for reset count
* 260-261: 0x11D7 (decimal 4567): header for network errors
* 262-291: network error counting bytes

### 0x1C: RECEIVE DIAGNOSTIC RESULTS (cmmd_rdiag)

This appears to follow the specification, setting the parameter length to 0x20
(32) and leaving everything else set to 0.  After reception, the target moves
to DATA IN and sends 32 bytes.

The following bytes are sent every time this has been tested.  The driver
reports this result as "Passed":

0x43, 0x21, 0x53, 0x02, 0x40, 0x00, 0x00, 0x00,
0x08, 0x89, 0x12, 0x04, 0x43, 0x02, 0x40, 0x00,
0x00, 0x00, 0x08, 0x89, 0x12, 0x04, 0x43, 0x02,
0x40, 0x00, 0x00, 0x00, 0x08, 0x89, 0x12, 0x04

### 0x1D: SEND DIAGNOSTIC (cmmd_sdiag)

This appears to follow the specification, setting the parameter length to 0x3C
(60) and leaving everything else set to 0.  After reception, the target moves
to DATA OUT and accepts 60 bytes.  The contents of these bytes are not
understood.

## Initialization

As the first thing it does, the initiator sends a READ(6) command with a length
of 1.  The device responds by moving to STATUS, sending GOOD, then going to
MESSAGE IN, and sending COMMAND COMPLETE.  This may be part of the Mac startup
routine, as this happens very early during startup – I think this may just be
the toolkit looking for a boot volume.

The first actual command is INQUIRY, with page code 0x02, allocating 96 bytes
for the response.  The response data is as described in the vendor-specific
section.

After the INQUIRY command, the initiator sends the following commands,
more-or-less in order (TEST UNIT READY command may appear in here or after
these as well):

* 0x0C, 0x00, 0x04, 0x00, 0x00, 0x00
* 0x09: "Set Multicast Registers", with 0x00 as last data byte.
* 0x0C, 0x00, 0x02, 0x00, 0x00, 0x00
* 0x09: "Set Multicast Registers", with 0x80 as last data byte.

The initiator will usually send the IDENTIFY message during a MESSAGE OUT phase
set to 0xC0 (b11000000) during the TEST UNIT READY commands. This appears to be
used to let the device know that packet reception is now allowed.

## Standard Packet Reception:

When it receives a packet, the device will attempt to reselect the computer.
Upon successful reselection, it moves to MESSAGE OUT.  It usually receives NO
OPERATION (note: I’m not sure if this is due to a pending /ATN or not).  After
that, the device moves to DATA IN and sends the packet as described below.
After transmitting the data, the device changes to MESSAGE OUT, again usually
getting NO OPERATION.  If there are more packets, it moves back to DATA IN,
otherwise it moves to MESSAGE IN, sends DISCONNECT, and then goes to bus free.

There are instances when the device will reselect the computer, move to MESSAGE
OUT, and get a DISCONNECT message.  I assume this is being caused by the
computer’s driver having a pending packet to send, since that usually occurs
immediately after the bare minimum time in BUS FREE.  The device obeys
DISCONNECT in this situation, responds to the message by transitioning to
MESSAGE IN, sends a DISCONNECT of its own, then goes to bus free.

MESSAGE OUT may respond with an extended message (0x01) with a length set to 3,
the bytes of which are [0xFF, 0x00, 0x2B].  The exact purpose of this is not
known. Ignoring it seems to be acceptable. At this point, I have not been
successful at observing this behavior on the real hardware.

The packet format itself is straightforward.  First, a preamble is sent,
consisting of the following:

* 1 flag byte
* 1 ID byte, incremented by one for each packet received, presumably to help
  the driver keep track of packet order
* 2 length bytes in little-endian format, indicating the remaining bytes to
  be sent, not including these four header bytes.

The packet payload is then sent, starting with the destination MAC. This
includes padding bytes and the FCS.

The flag byte appears to be a copy of the 8390's 0x0C (RSR) register.  The only
bits of interest are:

* Bit 0: set when packet received intact, thus always set.
* Bit 5: set when the packet was a multicast/broadcast packet.

Note that the target has a habit of driving /REQ after the last byte of
interest has already been sent.  It waits ~10 or 11us then releases /REQ. I'm
not sure if this is intentional or is just a bug. Experimentation has shown
that not emulating this behavior is acceptable.

Important note: /ATN may become asserted during DATA IN, even while /REQ is
asserted. If /ATN becomes asserted, immediately halt DATA IN and move to
MESSAGE OUT, or the driver may malfunction.

## Standard Packet Transmission:

Once the target is selected, it moves to the COMMAND phase.  The initator
normally sends a six byte command with opcode 0x05, which appears to be for a
vendor-specific "send a packet" command. Byte 3 and 4 are the transfer length
in bytes, in big-endian order, and all other bytes are zero.

After receiving this command, the device switches to DATA OUT to accept the
packet.  The packet format for sending is straightforward:

* Destination Address (6 bytes)
* Source Address (6 bytes)
* EtherType (2 bytes, big-endian order)
* Packet Data

The device takes care of appending the packet header, any needed padding, and
the FCS.

Note: trailing padding is apparently whatever was in the buffer from the last
time data was sent, though that likely is irrelevant for emulating the device.

## Attribution

Copyright 2019 saybur

This document is licensed under 
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
