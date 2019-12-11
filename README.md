scuznet
=======

Open source hardware emulating a Nuvolink SCSI to Ethernet adapter, for use
with vintage Macintosh computers.

# Status

The firmware is at an early alpha stage. There are many things that are known
to not work correctly, including:

* The emulated hard drive does not provide mode information yet and cannot be
  directly formatted by available tools I've tried. Instead, instructions
  [here](http://www.codesrc.com/mediawiki/index.php/HFSFromScratch) have been
  helpful for creating test images.
* Some SD cards have exhibited compatibility issues that have yet to be
  fully identified and corrected.
* MAC address changing at runtime from the driver is not implemented yet.

There are likely a large number of other bugs that have yet to be identified
and/or fixed.

# Compatibility

This implementation has some limitations by design:

1. There must be only one initiator on the bus.
2. The initiator on the bus must be at ID 7.
3. Read parity is not used. Transmission parity is provided but may be disabled
   to slightly improve performance.

These should not be a significant problem for the target computer platform,
which for the most part has similar limitations.

# Device Configuration

See [SETTINGS.html](SETTINGS.html) for a description of firmware configuration
options.

# License

Except where otherwise noted, all files in this repository are available under
the terms of the GNU General Public License, version 3, available in the
COPYING document.
