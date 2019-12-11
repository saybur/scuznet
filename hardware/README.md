scuznet hardware
================

Provided here solely for informational purposes is a prototype hardware design
that was built for testing the scuznet firmware. It is not well tested and has
the following known issues, and likely many other unknown ones:

* There is no USB support, though it would be very nice to have. As it stands,
  any programming must be done via the PDI interface.
* The footprints on the 0603 SMD capacitors and the ferrite are a little small,
  and could stand to be increased in size for my shaky hands.
* The power connector footprint is too large, and wiggles around while
  soldering.
* The Ethernet and power connectors are too close to the edge of the DB25
  connector, but have some space to move out without interfering with the case.
* The '574 is unnecessary and a result of a bad design choice on my part. It
  could be replaced with a '245.
* It would be nice to re-arrange the PHY RX lines so no reordering is
  necessary.

# License

These files are available under the terms of the GNU General Public License,
version 3, available in the LICENSE document in the parent directory.
