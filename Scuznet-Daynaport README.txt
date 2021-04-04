Daynaport Firmware for Scuznet ("Scuznet-Daynaport")

* Thanks to Saybur @ 68kMLA for designing Scuznet and writing the firmware - The hardware has been rock solid and the firmware programming was EXTREMELY easy to follow (very organized and well-commented).  I am sure that others would easily be able to extend the capabilities of this to support CD-ROM support given the awesome library of SCSI commands Saybur has included.  I would also think that it would be fairly straightforward to replace the current network hardware with a Wiznet WiFi alternative providing a wireless ethernet/SCSI2SD alternative for PowerBook users.

* Comments on the Scuznet-Daynaport firmware to superjer2000 on 68kMLA

BACKGROUND
~~~~~~~~~~
* Scuznet is an incredible device - solid state storage and SCSI-based Ethernet for about US$38.00
* The main project is designed to emulate the Nuvolink/Etherlan SC device.  Although this emulation mode offers strong performance, the Nuvolink and it's drivers are relatively complicated in their operation and at least two Scuznet users experienced various issues with the Nuvolink Ethernet emulation (initially freezes which were largely resolved but bus errors at shutdown/restart or when turning off Appletalk remained)
* This version of Scuznet firmware swaps out the Nuvolink device emulation for an emulation of a simpler Ethernet device called the Daynaport ("Scuznet-Daynaport")
* Initial tests of this firmware seem to indicate better stability at the cost of lower performance (On an SE/30 AppleShare file transfers run at ~30-35kB/s vs ~50-55kB/s with Nuvolink and Fetch file transfers of ~55kB/s versus ~70kB/s)

DAYNAPORT EMULATION
~~~~~~~~~~~~~~~~~~~
* Protocol data sourced from http://anodynesoftware.com/ethernet/main.html [Anodyne]
* Inquiry Data from RaSCSI DaynaPort project

EMULATED COMMANDS
~~~~~~~~~~~~~~~~~

0x03: REQUEST SENSE
-------------------
Not yet observed but documented by Anodyne.  System responds with 0x70 and then eight bytes of 0x00 which seems to be appropriate based on what I read of the SCSI spec at https://www.staff.uni-mainz.de/tacke/scsi/SCSI2-06.html

0x0A: SEND PACKET
-----------------
Per Anodyne, two potential commands options with the format of:
0A 00 00 LL LL XX where XX = 00 or 80.  I have only observed XX=80.

Per Anodyne LL LL is big-endian packet length, although Anodyne indicates that if XX=80 LL LL is "the packet length + 8, and the data to be sent is PP PP 00 00 XX XX XX ... 00 00 00 00" where PP PP is the actual length of the packet and XX is the packet.  My reading of the Anodyne spec differs from what I experienced which is:

For XX=80 LL LL is big-endian length of the packet to be sent.  An extra 8 bytes needs to be read from the Macintosh for the PP PP 00 00 and the trailing 00 00 00 00.  All cases I noted during porting of Daynaport firmware where XX = 80 (again XX=00 hasn't been observed) has been that LL LL == PP PP.

The if XX = 80 the Scuznet Daynaport firmware reads the extra eight bytes and then sends the related packet to the network.

0x08: READ PACKET
-----------------
Per the Anodyne spec the command format is 08 00 00 LL LL XX where LL LL is the data length and XX is either C0 or 80 (only C0 has been observed).  LL LL seems to just reflect the max buffer size the driver can receive which I have always observed be 1,524 (05F4).  As most packets are smaller than this, I had tried sending multiple packets to the driver at once to improve performance without success.

Per Anodyne, the driver expects a response of LL LL NN NN NN NN followed by the packet with CRC.

LL LL is the length of the packet (including CRC).

NN NN NN NN is set to either 00 00 00 00 or 00 00 00 10:

Scuznet-Daynaport checks if there is another packet that has been read by the network controller and if so the last NN is set to 0x10 instead of 0x00.  I am assuming that this tells the driver to request another packet before it's regular polling interval.  Not following this (i.e. always returning all 0x00s) does negatively impact performance.

When the driver polls, if there isn't any packet to send, Scuznet-Daynaport returns 00 00 00 00 00 00.

I don't report skipped packets to the driver.  Anodyne seems to indicate there are specific flags sent in this case but I haven't had any issues not monitoring for this or emulating the related response.

Important:  The Macintosh driver seems to require a delay after the first six bytes are sent.  Scuznet-Daynaport has a delay of 100us after the first six pre-amble bytes (i.e. LL LL NN NN NN NN) are sent before the actual packet is sent.  Without this delay, the Mac doesn't recognize the read packet properly.  A delay of 30us worked on an SE/30 but an SE required a longer delay.  100us worked fine with the SE and SE/30 and didn't have any negative impact on the SE/30's network performance (it may have actually improved it slightly).

When the Mac driver first starts up it sends a READ with a length of 1.  Scuznet-Daynaport responds with just a STATUS GOOD and COMMAND COMPLETE.

0x09 RETRIEVE STATS
-------------------
Per Anodyne spec, the driver expects the MAC address followed by three 4 byte counters.  Scuznet-Daynaport pulls the MAC address read from the config (or default) and simply returns 00 00 00 00 for each of those counters.

0x12 INQUIRY
------------
Gets called twice when the driver loads.  Expects Daynaport identification response.  Credit to the RaSCSI project for the appropriate response.

0x0D ACTIVATE APPLETALK
-----------------------
Not documented by Anodyne but gets called when AppleTalk is turned on.  If the device doesn't respond by reading from the Mac whatever data is sent (length is specified in bytes 4 and 5 of the command block) AppleTalk initialization fails on the Mac.  My guess, based on Scuznet-Nuvolink is that this configures the device's filter to enable and disable AppleTalk.  I always have multicast and broadcast receiption turned on so these read bytes just get consigned to oblivion.

FILTERING
~~~~~~~~~			
Scuznet-Daynaport network filter is always configured to only allow packets that: Have correct CRC AND are directed to our MAC address OR are broadcast OR are multicast