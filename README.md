
# Scuznet    [![Badge License]][License]   ![Badge Firmware]

***SCSI Hard Drive*** *and* ***Ethernet Adapter*** <br>
*emulator for use with vintage **Macintosh**.*

<br>

<div align = center>

---

[![Button Tools]][Tools]   
[![Button Software]][Software]   
[![Button Hardware]][Hardware] 

---

</div>

<br>

## Status

There are things that are known to not work correctly, <br>
and there are likely a large number of other bugs that <br>
have yet to be identified and / or fixed.

*If you encounter problems, please open a **[New Issue]**.*

<br>
<br>

## Limitations

- There must be only one initiator on the bus

- The initiator on the bus must be at `ID 7`

- Read parity is not used.

  *Transmission parity is provided but may* <br>
  *be disabled to improve performance.*

<br>

These should not be a significant problem for the <br>
target computer platform, which for the most part <br>
has similar limitations.

<br>


<!----------------------------------------------------------------------------->

[New Issue]: https://github.com/saybur/scuznet/issues

[Hardware]: Hardware
[Software]: Software
[License]: LICENSE
[Tools]: Tools


<!---------------------------------{ Badges }---------------------------------->

[Badge Firmware]: https://img.shields.io/badge/Firmware-Beta-F46D01?style=for-the-badge
[Badge License]: https://img.shields.io/badge/License-GPL_3-blue.svg?style=for-the-badge


<!--------------------------------{ Buttons }---------------------------------->

[Button Tools]: https://img.shields.io/badge/Tools-EF4678?style=for-the-badge&logoColor=white&logo=AzureArtifacts
[Button Software]: https://img.shields.io/badge/Software-1997B5?style=for-the-badge&logoColor=white&logo=CodeFactor
[Button Hardware]: https://img.shields.io/badge/Hardware-535D6C?style=for-the-badge&logoColor=white&logo=CloudFoundry