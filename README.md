raplread
========

raplread is a library for accessing and printing power and energy measurements on (modern) Intel machines. raplread uses Intel's RAPL (Running Average Power Limit) interface to collect measurements.

raplread is an extension of http://web.eece.maine.edu/~vweaver/projects/rapl/rapl-read.c

* Website             : http://lpd.epfl.ch/site/ascylib
* Author              : Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
* Related Publications: raplread was developed for:
  Asynchronized Concurrency: The Secret to Scaling Concurrent Search Data Structures,
  Tudor David, Rachid Guerraoui, Vasileios Trigonakis (alphabetical order),
  ASPLOS '15


Installation
------------

Execute `make` in the raplread base folder.
This should compile `libraplread.a`.

If you want to customize the installation of raplread, you can add a custom configuration for a platform in `platform_defs.h` and change the `PLATFORM` in the `Makefile`. This is for example necessary on multi-socket machines.

You can also compile with `make VERSION=DEBUG` to generate a debug build of raplread.

Using raplread
--------------

To use raplread you need to include `rapl_read.h` and link with `-lraplread`.
`rapl_read.h` contains the interface of raplread.

Use the macros in `rapl_read.h` so that you can easily enable/disable raplread by setting the value of the `RAPL_READ_ENABLE` macro in `rapl_read.h`.

In short, raplread has two main modes of operation:
   1. all/some threads do `RR_INIT(core)`, but only one per-socket is set responsible for performing the actual measurements,
   2. only one thread does `RR_INIT_ALL()` that initializes the structures for all sockets (nodes) of the current machine. This mode can be used for example from a main thread.

Depending on the mode, raplread provides functions to:
   * `RR_START...` start measuring power/energy
   * `RR_STOP...` stop measuring power/energy
   * `RR_PRINT...` print measurements
   * `RR_STATS` store measurements in a `rapl_stats_t` structure so they can be accessed by the application

Refer to `raplread.h` for more details and operations. 

Details
-------

raplread reads the measurements using MSRs, thus you need root access to execute applications that are linked with raplread enabled.
