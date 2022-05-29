
# Scuznet    ![Badge Firmware]

***SCSI Hard Drive*** *and* ***Ethernet Adapter*** <br>
*emulator for use with vintage **Macintosh**.*

<br>

## Status

There are things that are known to not work correctly, <br>
and there are likely a large number of other bugs that <br>
have yet to be identified and / or fixed.

*If you encounter problems, please open a new issue.*

<br>
<br>

## Compatibility

This implementation has some limitations by design:

1. There must be only one initiator on the bus.
2. The initiator on the bus must be at ID 7.
3. Read parity is not used. Transmission parity is provided but may be disabled
   to improve performance.

These should not be a significant problem for the target computer platform,
which for the most part has similar limitations.

<br>

## License

Except where otherwise noted, all files in this repository are available under
the terms of the GNU General Public License, version 3, available in the
LICENSE document. There is NO WARRANTY, not even for MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. For details, refer to the license.


<!----------------------------------------------------------------------------->

[Badge Firmware]: https://img.shields.io/badge/Firmware-Beta-F46D01?style=for-the-badge
