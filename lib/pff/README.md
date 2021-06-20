Petit FatFs Branch
==================

Modified version of Elm's superb
[Petit FatFs](http://elm-chan.org/fsw/ff/00index_p.html) for use with
this project, branched off R0.03a.

Note: these changes are not intended to be general-purpose and are tailored to
fit the needs of this code. If you want to use them elsewhere, do so with
caution.

Changes
-------

The normal Petit FatFs library typically works on single or partial sectors.
This is great for small, memory-constrained microcontrollers but kills
performance when working with large amounts of data.

To address this, two functions have been added to both `diskio.h` and `pff.h`
to help leverage the DMA unit. Instead of returning the data as a single block,
these instead call a provided function repeatedly, either asking or providing a
sector of data at a time. Two internal buffers are used in a ping-pong
configuration to support this. More specifically:

* When reading:
    * The first sector is read in into the first buffer.
    * The second sector read is started via DMA.
    * The first sector is sent to the function for processing.
    * When both the function and the DMA transaction are done, DMA is
      restarted for the next sector, this time pointing to the first buffer.
    * The second sector is provided to the function.
    * The DMA/call steps repeat until all sectors are transferred.
* When writing:
    * The function is called, asking for the first sector's data.
    * Once the memory card is ready, the write is started via DMA.
    * The function is called again, asking for the second sector.
    * Once both the pending write and function return, the second sector
      is started via DMA.
    * The DMA/call steps repeat until all sectors are transferred.

This allows a percentage of the read/write time to be occupied by handling
data, and maximizes use of the multi-sector read and write commands that are
critical for maintaining decent performance with modern memory cards.

The two functions added that support this are:

```
FRESULT pf_mread (UINT (*func)(BYTE*,UINT), UINT str, UINT* sr);
FRESULT pf_mwrite (UINT (*func)(BYTE*,UINT), UINT stw, UINT* sw);
```

They should be given a function that either reads or writes a sector's worth of
data into the provided pointer (length given as UINT), returning the number of
bytes processed. The `btr/btw` parameters are the number of bytes to work on,
and `br/bw` provide the number of bytes actually processed; if these numbers
do not match there was an error of some kind.

These calls error out with `FR_NOT_ON_SECTOR` if the file pointer is not on a
sector boundary when they are invoked.

One additional support function has also been added:

```
FRESULT pf_size (DWORD* sr);
```

This supplies the size of the currently open file, in bytes. If no file is open
this will return an error.
