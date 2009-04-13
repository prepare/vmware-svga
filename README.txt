--------------------------------
VMware SVGA Device Developer Kit
--------------------------------

The "VMware SVGA II" device is the virtual graphics card implemented
by all VMware virtualization products. It is a virtual PCI device,
which implements a basic 2D framebuffer, as well as 3D acceleration,
video overlay acceleration, and hardware cursor support.

This is a package of documentation and example code for the VMware
SVGA device's programming model. Currently it consists of some very
basic documentation, and a collection of examples which illustrate the
more advanced features of the device. These examples are written to
run on the "virtual bare metal", without an operating system.

This package is intended for educational purposes, or for people who
are developing 3D drivers. This code won't help you if you're writing
normal user-level apps that you'd like to run inside a virtual
machine. It's for driver authors, and it assumes a reasonable amount
of prior knowledge about graphics hardware.

Requirements
------------

To compile the example code, you'll need a few basic open source tools:

  - A recent version of GCC. I use 4.2. Older versions may
    require tweaking the Makefile.rules file slightly.)
  - binutils
  - GNU Make
  - Python

To run the examples, you'll need a recent version of VMware
Workstation, Player, or Fusion. Some of the examples will work on
older versions, but Workstation 6.5.x or Fusion 2.0.x is strongly
recommended.

Contents
--------

* bin/

  Precompiled binaries and .vmx files for all examples. These can be
  loaded directly into VMware Workstation, Player, or Fusion.

* doc/

  Basic SVGA hardware documentation. This includes a text file with
  information about the programming model, plus it includes a copy of
  a WIOV paper which describes our 3D acceleration architecture.

* lib/metalkit/

  Metalkit is a very simple open source OS, which bootstraps the
  examples and provides basic hardware support.

* lib/refdriver/

  The SVGA "Reference Driver". This is a sample implementation of a
  driver for our device, which is used by the examples. It provides
  device initialiation, an implementation of the low-level FIFO
  protocol, and wrappers around common FIFO commands.

  If you're writing a driver for the VMware SVGA device, "svga.c"
  from this directory is required reading. The FIFO protocol has
  many subtle gotchas, and this source file is the only place
  where they're publicly documented.

* lib/vmware/

  Header files which define VMware's protocols and virtual hardware.
  The svga_reg.h and svga3d_reg.h files are (in places, at least)
  commented with more information on the programming model.

  If you can't find specific documentation or an example on a feature,
  this is the next place to look. This is also where to get a complete
  list of the supported registers and commands.

* lib/util/

  Higher-level utilities built on top of the refdriver layer. This
  directory won't contain any novel information about the virtual
  hardware, but it does contain some higher-level abstractions used
  by the examples, and these abstractions demonstrate some useful
  idioms for programming the SVGA device.

* examples/

  Each example has a separate subdirectory. You can run "make" in the
  top-level directory to compile all examples, or you can build them
  individually.

  Many of the examples are self-explanatory, but some of them are
  not. See the comments at the beginning of the 'main.c' file in each
  example.

Development
-----------

This project isn't intended to be a one-time "code drop" from VMware.
Our intent is for the examples in this package to be maintained out in
the open. If we have a bugfix, or a new example that works on released
VMware products, we'll check it in directly to the public repository.

For examples of not-yet-released features, we will be developing on an
internal branch. This branch will be merged to the public repository
shortly after the first release which has working versions of these
features.

License
-------

Except where noted in individual source files, the whole package is
Copyright (C) 1998-2009 VMware, Inc.  It is released under the MIT
license:

  Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software and associated documentation files
  (the "Software"), to deal in the Software without restriction,
  including without limitation the rights to use, copy, modify, merge,
  publish, distribute, sublicense, and/or sell copies of the Software,
  and to permit persons to whom the Software is furnished to do so,
  subject to the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

Contact
-------

This project is provided as-is, with no official support from
VMware. However, I will try to answer questions as time permits.
If you have questions or you'd like to submit a patch, feel free
to email me at: micah at vmware.com

--
