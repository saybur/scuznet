INIH Notes
==========

This is commit `4f251f0` of INIH. The only modification is commenting out
`ini_parse()` to avoid the `fopen()` usage, which is not available from
avr-libc.
