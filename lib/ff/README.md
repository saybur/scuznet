FatFs Branch
============

Modified version of Elm's superb
[FatFs](http://elm-chan.org/fsw/ff/00index_e.html) for use with
this project, branched off R0.14b.

Performance Notes
-----------------

The ATxmega64A3U only has 4K of SRAM available. To maximize this limited
working memory, the FatFs `FF_FS_TINY` configuration is used. See
[here](http://elm-chan.org/fsw/ff/doc/appnote.html) for details under the
"Performance Effective File Access" section.

SCSI works with sectors directly, so almost all performance-sensitive
read/write operations will occur on sector boundries and should not be affected
by this setting.
