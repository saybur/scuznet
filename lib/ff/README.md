FatFs Branch
============

Modified version of Elm's superb
[FatFs](http://elm-chan.org/fsw/ff/00index_e.html) for use with
this project, branched off R0.14b.

Note: these changes are not intended to be general-purpose and are tailored to
fit the needs of this code. If you want to use them elsewhere, do so with
caution.

Changes
-------

The normal FatFs library typically fills a large contiguous buffer before
returning the data all in one go to the caller. This is great if you have a
RTOS and/or lots of memory, neither of which this project has.

To address this, two functions have been added to both `diskio.h` and `ff.h`
to help leverage the Xmega DMA unit. Instead of returning the data as a large
block, these instead call a provided function repeatedly, either asking or
providing a sector of data at a time. Two internal buffers are used in a
ping-pong configuration. More specifically:

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

The two functions added are:

```
FRESULT f_mread (FIL* fp, BYTE (*func)(BYTE*), UINT str, UINT* sr);
FRESULT f_mwrite (FIL* fp, BYTE (*func)(BYTE*), UINT stw, UINT* sw);
```

They should be given a function that either reads or writes a sector's worth of
data into the provided pointer, returning nonzero on success. The `str/stw`
parameters are the number of sectors to work on, and `sr/sw` provide the number
of sectors actually processed; if these numbers do not match there was an error
of some kind.

These calls error out with `FR_MISALIGNED` if the file pointer is not on a
sector boundary when they are invoked. They will also error with
`FR_NOT_SYNCED` if there is a partial sector stored within the FatFs; if that
is possible invoke `f_sync()` prior to use.

This code relies on sector sizes being equal to 512 bytes. This is enforced via
a check in the `disk.c` implementation.

FatFs Performance Notes
-----------------------

The ATxmega64A3U only has 4K of SRAM available. To maximize this limited
working memory, the FatFs `FF_FS_TINY` configuration is used. See
[here](http://elm-chan.org/fsw/ff/doc/appnote.html) for details under the
"Performance Effective File Access" section.

SCSI works with sectors directly, so almost all performance-sensitive
read/write operations will occur on sector boundaries and should not be
affected by this setting.

