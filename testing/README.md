Board Testing
=============

This is a special routine that can be flashed to a scuznet board, to help
diagnose problems with shorted pins and the like.

**Very important note**: before flashing this firmware, ensure the board is
disconnected from the bus!

This implementation is under development and has not been extensively tested.
Suggestions are welcome.

# Error Reports

If all tests pass, the board will pulse the status LED.

In all other cases, the LED will flash a number of long pulses, then a number
of short pulses, then an optional set of long pulses. Look up the respective
pulses in the list below to see what the error means.

* 1-x-0: TDB(x-1) is registering '1' when not being driven (external pull-down 
  not working).
* 2-x-y: TDB(x-1) is causing TDB(y-1) to change unexpectedly.
* 3-x-y: TDB(x-1) is causing RDB(y-1) to change unexpectedly.

# License

Except where otherwise noted, all files in this repository are available under
the terms of the GNU General Public License, version 3, available in the
LICENSE document. There is NO WARRANTY, not even for MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. For details, refer to the license.
