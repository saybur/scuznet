scuznet
=======

This is a modified version of the Scuznet board designed to fit in a
PowerBook. Also included is a small PCB for a PowerBook 100's modem 
bay. Otherwise this is fully compatible with a Scuznet V2.


Open source hardware emulating SCSI hard drives and an Ethernet adapter, for
use with vintage Macintosh computers.

# Status

The firmware is at a beta stage. There are things that are known to not work
correctly, and there are likely a large number of other bugs that have yet to
be identified and/or fixed.

If you encounter problems, please open a new issue.

# Compatibility

This implementation has some limitations by design:

1. There must be only one initiator on the bus.
2. The initiator on the bus must be at ID 7.
3. Read parity is not used. Transmission parity is provided but may be disabled
   to improve performance.

These should not be a significant problem for the target computer platform,
which for the most part has similar limitations.

# License

Except where otherwise noted, all files in this repository are available under
the terms of the GNU General Public License, version 3, available in the
LICENSE document. There is NO WARRANTY, not even for MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. For details, refer to the license.
