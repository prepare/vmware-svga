
Copyright (C) 1999-2009 VMware, Inc.
All Rights Reserved


        VMware SVGA Device Interface and Programming Model
        --------------------------------------------------

Revision 3, 2009-04-12


Table of Contents:

  1. Introduction
  2. Examples and Reference Implementation
  3. Virtual Hardware Overview
  4. 2D Graphics Model
  5. 3D Graphics Model
  6. Overview of SVGA3D features
  7. Programming the VMware SVGA Device

XXX - Todo
----------

This document does not yet describe the 3D hardware in great
detail. It is an architectural overview. See the accompanying sample
and reference code for details.

Section (7) is biased toward describing much older features of the
virtual hardware. Many new capability flags and FIFO commands have
been added, and these are sparsely documented in svga_reg.h.


1.  Introduction
----------------

This document describes the virtual graphics adapter interface which
is implemented by VMware products. The VMware SVGA Device is a virtual
PCI video card. It does not directly correspond to any real video
card, but it serves as an interface for exposing accelerated graphics
capabilities to virtual machines in a hardware-independent way.

In its simplest form, the VMware SVGA Device can be used as a basic
memory-mapped framebuffer. In this mode, the main advantage of
VMware's SVGA device over alternatives like VBE is that the virtual
machine can explicitly indicate which ares of the screen have changed
by sending update rectangles through the device's command FIFO. This
allows VMware products to avoid reading areas of the framebuffer which
haven't been modified by the virtualized OS.

The VMware SVGA device also supports several advanced features:

   - Accelerated video overlays
   - 2D acceleration
   - Synchronization primitives
   - DMA transfers
   - Device-independent 3D acceleration, with shaders
   - Multiple monitors
   - Desktop resizing


2.  Examples and Reference Implementation
-----------------------------------------

This document is not yet complete, in that it doesn't describe the
entire SVGA device interface in detail. It is an architectural
overview of the entire device, as well as an introduction to a few
basic areas of programming for the device.

For deeper details, see the attached example code. The "examples"
directory contains individual example applications which show various
features in action. The "lib" directory contains support code for the
example applications.

Some of this support code is designed to act as a reference
implementation. For example, the process of writing to the command
FIFO safely and efficiently is very complicated. The attached
reference implementation is a must-read for anyone attempting to write
their own driver for the SVGA device.

For simplicity and OS-neutrality, all examples compile to floppy disk
images which execute "on the bare metal" in a VMware virtual
machine. There are no run-time dependencies. At compile-time, most of
the examples only require a GNU toolchain (GCC and Binutils). Some of
the examples require Python at compile-time.

Each example will generate a .vmx virtual machine configuration file
which can be used to boot it in VMware Workstation or Fusion.

The included example code focuses on advanced features of the SVGA
device, such as 3D and synchronization primitives. There are also a
couple examples that demonstrate 3D graphics and video overlays.

For more examples of basic 2D usage, the Xorg driver is also a good
reference.

Header files and reference implementation files in 'lib':

 * svga_reg.h

   SVGA register definitions, SVGA capabilities, and FIFO command
   definitions.

 * svga_overlay.h

   Definitions required to use the SVGA device's hardware video overlay
   support.

 * svga_escape.h

   A list of definitions for the SVGA Escape commands, a way to send
   arbitrary data over the SVGA command FIFO. Escapes are used for video
   overlays, for vendor-specific extensions to the SVGA device, and for
   various tools internal to VMware.

 * svga3d_reg.h

   Defines the SVGA3D protocol, the set of FIFO commands used for hardware
   3D acceleration.

 * svga3d_shaderdefs.h

   Defines the bytecode format for SVGA3D shaders. This is used for
   accelerated 3D with programmable vertex and pixel pipelines.

 * svga.c

   Reference implementation of low-level SVGA device functionality.
   This contains sample code for device initialization, safe and
   efficient FIFO writes, and various synchronization primitives.

 * svga3d.c

   Reference implementation for the SVGA3D protocol. This file
   uses the FIFO primitives in svga.c to speak the SVGA3D protocol.
   Includes a lot of in-line documentation.

 * svga3dutil.c

   This is a collection of high-level utilities which provide usage
   examples for svga3d.c and svga.c, and which demonstrate common
   SVGA3D idioms.


3.  Virtual Hardware Overview
-----------------------------

The VMware SVGA Device is a virtual PCI device. It provides the
following low-level hardware features, which are used to implement
various feature-specific protocols for 2D, 3D, and video overlays:

 * I/O space, at PCI Base Address Register 0 (BAR0)

There are only a few I/O ports. Besides the ports used to access
registers, these are generally either legacy features, or they are for
I/O which is performance critical but may have side-effects. (Such as
clearing IRQs after they occur.)

 * Registers, accessed indirectly via INDEX and VALUE I/O ports.

The device's register space is the principal method by which
configuration takes place. In general, registers are for actions which
may have side-effects and which take place synchronously with the CPU.

 * Guest Framebuffer (BAR1)

The SVGA device itself owns a variable amount of "framebuffer" memory,
up to a maximum of 128MB. This memory size is fixed at power-on. The
memory exists outside of the virtual machine's "main memory", and it's
mapped into PCI space via BAR1. The size of this framebuffer may be
determined either by probing BAR1 in typical PCI fashion, or by
reading SVGA_REG_FB_SIZE.

The beginning of framebuffer memory is reserved for the 2D
framebuffer. The rest of framebuffer memory may be used as buffer
space for DMA operations.

 * Command FIFO (BAR2)

The SVGA device can be thought of as a co-processor which executes
commands asynchronously with the virtual machine's CPU. To enqueue
commands for this coprocessor, the SVGA device uses another
device-owned memory region which is mapped into PCI space.

The command FIFO is usually much smaller than the framebuffer.  While
the framebuffer usually ranges from 4MB to 128MB, the FIFO ranges in
size from 256KB to 2MB. Like the framebuffer, the FIFO size is fixed
at power-on. The FIFO is mapped via PCI BAR2.

 * FIFO Registers

The beginning of FIFO memory is reserved for a set of "registers".
Some of these are used to implement the FIFO command queueing
protocol, but many of these are used for other purposes. The main
difference between FIFO registers and non-FIFO registers is that FIFO
registers are backed by normal RAM whereas non-FIFO registers require
I/O operations to access. This means that only non-FIFO registers can
have side-effects, but FIFO registers are much more efficient when
side-effects aren't necessary.

The FIFO register space is variable-sized. The driver is responsible
for partitioning FIFO memory into register space and command space.

 * Synchronization Primitives

Conceptually, the part of the SVGA device which processes FIFO
commands can be thought of as a coprocessor or a separate thread of
execution. The virtual machine may need to:

 - Wake up the FIFO processor when it's sleeping, to ensure that
   new commands are processed with low-latency. (FIFO doorbell)

 - Check whether a previously enqueued FIFO command has been
   processed. (FIFO fence)

 - Wait until the FIFO processor has passed a particular
   command. (Sync to fence)

 - Wait until more space is available in the FIFO. (Wait for
   FIFO progress)

 * Interrupts (Workstation 6.5 virtual machines and later only)

On virtual machines which have been upgrade to Workstation 6.5 virtual
hardware, the SVGA device provides an IRQ which can be used to notify
the virtual machine when a synchronization event occurs. This allows
implementing operations like "Sync to fence" without interfering with
a virtual machine's ability to multitask.

On older virtual hardware versions, the SVGA device only supports a
"legacy sync" mechanism, in which a particular register access has the
side-effect of waiting for host FIFO processing to occur.  This older
mechanism completely halts the virtual machine's CPU while the FIFO is
being processed.

 * Physical VRAM

The VMware SVGA device provides management of physical VRAM resources
via "surface objects", however physical VRAM is never directly visible
to the virtual machine. Physical VRAM can only be accessed via DMA
transfers.

Note that framebuffer memory is simply a convenient place to put DMA
buffers. Even if a virtual machine only has 16MB of framebuffer memory
allocated to it, it could be using gigabytes of physical VRAM if that
memory is available to the physical GPU.

 * DMA engine

The VMware SVGA device can asynchronously transfer surface data
between phyiscal VRAM and guest-visible memory. This guest-visible
memory could be part of framebuffer memory, or it could be part of
guest system memory.

The DMA engine uses a "Guest Pointer" abstraction to refer to any
guest-visible memory. Guest pointer consist of an offset and a Guest
Memory Region (GMR) ID. There is a pre-defined GMR which refers to
framebuffer memory. The virtual machine can create additional GMRs to
refer to regions of system memory which may or may not be physically
contiguous.


4.  2D Graphics Model
---------------------

Conceptually, the 2D portion of the VMware SVGA device is a compositor
which displays a user-visible image composed of several planes. From
back to front, those planes are:

  - The 2D framebuffer
  - 3D regions
  - Video overlay regions
  - The virtual hardware mouse cursor ("cursor bypass")
  - The physical hardware mouse cursor ("host cursor")

It is important to note that host-executed 2D graphics commands do not
necessarily modify the 2D framebuffer, they may write directly to the
physical display or display window. Like a physical video card, the
VMware SVGA device's framebuffer is never modified by a mouse cursor
or video overlay. Unlike a physical video card, however, 3D display
regions in the VMware SVGA device may or may not modify the 2D
framebuffer.

The following basic 2D operations are available:

 * Update

Redraw a portion of the screen, using data from the 2D
framebuffer. Any update rectangles are subtracted from the set of
on-screen 3D regions, so 2D updates always overwrite 3D regions. 2D
updates still appear behind video overlays and mouse cursors.

An update command must be sent any time the driver wishes to make
changes to the 2D framebuffer available. The user-visible screen is
not guaranteed to update unless an explicit update command is sent.

Also note that the SVGA device is allowed to read the 2D framebuffer
even if no update command has been sent. For example, if the virtual
machine is running in a partially obscured window, the SVGA device
will read the 2D framebuffer immediately when the window is uncovered
in order to draw the newly visible portion of the VM's window.

This means that the virtual machine must not treat the 2D framebuffer
as a back-buffer. It must contain a completely rendered image at all
times.

There is not yet any way to synchronize updates with the vertical
refresh. Current VMware SVGA devices may suffer from tearing
artifacts.

 * 2D acceleration operations

These include fills, copies, and various kinds of blits. All 2D
acceleration operations happen directly on the user-visible screen,
not in 2D framebuffer memory.

Use of the 2D acceleration operations is encouraged only in very
limited circumstances. For example, when moving or scrolling
windows. Mixing accelerated and unaccelerated 2D operations is
difficult to implement properly, and incurs a significant
synchronization penalty.

 * Present 3D surface

"Present" is an SVGA3D command which copies a finished image from an
SVGA3D surface to the user-visible screen. It may or may not update
the 2D framebuffer in the process.

Present commands effectively create a 3D overlay on top of part of the
2D framebuffer. This overlay can be overwritten by Update commands or
by other Present commands.

Present is the only way in which the 2D and 3D portions of the VMware
SVGA device interact.

 * Video overlay operations

The SVGA device defines a group of virtual "video overlay units", each
of which can color-convert, scale, and display a frame of YUV video
overlayed with the 2D framebuffer. Overlay units each have a set of
virtual registers which are configured using the commands in
svga_overlay.h.

 * Virtual mouse cursor operations

The virtual mouse cursor is an overlay which shows the SVGA device's
current cursor image at a particular location. It may not be
hardware-accelerated by the physical machine, and it does not
necessarily correspond with the position of the user's physical mouse.

There are three "Cursor Bypass" mechanisms by which the virtual
machine can set the position of the virtual mouse cursor. Cursor
bypass 1 did not follow the overlay model described above, and it has
long been obsolete. Cursor bypass 2 and 3 are functionally equivalent,
except that cursor bypass 2 operates via non-FIFO registers and cursor
bypass 3 operates via FIFO registers. If cursor bypass 3 is supported
(SVGA_FIFO_CAP_CURSOR_BYPASS_3), it should be used instead of cursor
bypass 2.

For all forms of cursor bypass, the cursor image is defined by
SVGA_CMD_DEFINE_CURSOR.

 * Physical mouse cursor operations

The virtual machine does not define the location of the physical mouse
cursor, but it can define the cursor image and hide/show it. It does
so using the SVGA_CMD_DEFINE_CURSOR and SVGA_CMD_DISPLAY_CURSOR
commands.


5.  3D Graphics Model
---------------------

The VMware SVGA device supports hardware-independent accelerated 3D
graphics via the "SVGA3D" protocol. This is a set of extended FIFO
commands. SVGA3D utilizes the same underlying FIFO and synchronization
primitives as the 2D portion of the SVGA device, but the 2D and 3D
portions of the device are largely independent.

The SVGA3D protocol is relatively high-level. The device is
responsible for tracking render state among multiple contexts, for
managing physical VRAM, and for implementing both fixed-function and
programmable vertex and pixel processing.

The SVGA3D protocol is designed to be vendor- and API-neutral, but for
convenience it has been designed to be compatible with Direct3D in
most places. The shader bytecode is fully binary-compatible with
Direct3D bytecode, and most render states are identical to those
defined by Direct3D.

Note that the VMware SVGA device still supports 3D acceleration on all
operating systems that VMware products run on. Internally, hardware
accelerated 3D is implemented on top of the OpenGL graphics API.

To summarize the SVGA3D device's design:

 * SVGA3D is an extension to the VMware SVGA device's command FIFO
   protocol.

 * In some ways it looks like a graphics API:

      o SVGA3D device manages all physical VRAM allocation.
      o High-level render states, relatively high-level shader bytecode.

 * In some ways it looks like hardware:

      o All commands are executed asynchronously.
      o Driver must track memory ownership, schedule DMA transfers.
      o All physical VRAM represented by generic "Surface" objects

 * Supports both fixed-function and programmable vertex and fragment
   pipelines.


6. Overview of SVGA3D features
------------------------------

 * Capabilities

      o Extensible key/value pair list describes the SVGA3D device's
        capabilities.

      o Number of texture image units, max texture size, number of
        lights, texture formats, etc.

 * Surfaces

      o Formats: 8-bit RGB/RGBA, 16-bit RGB/RGBA, depth, packed
        depth/stencil, luminance/alpha, DXT compressed, signed, floating
        point, etc.

      o Supports 3D (volume) textures, cube maps,.

      o Surfaces are also used as vertex and index buffers.

      o Generic DMA blits between surfaces and system memory or
        offscreen "virtual VRAM".

      o Generic surface-to-surface blits, with and without scaling.

 * Contexts

      o Surfaces are global, other objects are per-context, render
        states are per-context.

      o Commands to create/delete contexts.

 * Render State (Mostly Direct3D-style)

      o Matrices

      o Texture stage states: Filtering, combiners, LOD, gamma
        correction, etc.

      o Stencil, depth, culling, blending, lighting, materials, etc.

 * Render Targets

      o Few restrictions on which surfaces can be used as render
        targets (More lenient than OpenGL FBOs)

      o Supports depth, stencil, color buffer(s)

 * Present

      o The "present" operation is a blit from an SVGA3D surface back
        to the user-visible screen.

      o May or may not update the guest-visible 2D framebuffer.

 * Occlusion queries

      o Submitted via FIFO commands

      o Results returned asynchronously: a results structure is filled
        in via DMA.

 * Shaders

      o We define an "SVGA3D bytecode", which is binary-compatible
        with Direct3D's shader bytecode.

      o SVGA3D may define extensions to the bytecode format in the future.

 * Drawing

      o A single generic "draw primitives" command performs a list of
        rendering operations from a list of vertex buffers.

      o Index buffer is optional.

      o Similar to drawing with OpenGL vertex arrays and VBOs.


7. Programming the VMware SVGA Device
-------------------------------------

1. Reading/writing a register:

    The SVGA registers are addressed by an index/value pair of 32 bit
    registers in the IO address space.

    The 0710 VMware SVGA chipset (PCI device ID PCI_DEVICE_ID_VMWARE_SVGA) has
    its index and value ports hardcoded at:

        index: SVGA_LEGACY_BASE_PORT + 4 * SVGA_INDEX_PORT
        value: SVGA_LEGACY_BASE_PORT + 4 * SVGA_VALUE_PORT

    The 0405 VMware SVGA chipset (PCI device ID PCI_DEVICE_ID_VMWARE_SVGA2)
    determines its index and value ports as a function of the first base
    address register in its PCI configuration space as:

        index: <Base Address Register 0> + SVGA_INDEX_PORT
        value: <Base Address Register 0> + SVGA_VALUE_PORT

    To read a register:
        Set the index port to the index of the register, using a dword OUT
        Do a dword IN from the value port

    To write a register:
        Set the index port to the index of the register, using a dword OUT
        Do a dword OUT to the value port

    Example, setting the width to 1024:

        mov     eax, SVGA_REG_WIDTH
        mov     edx, <SVGA Address Port>
        out     dx, eax
        mov     eax, 1024
        mov     edx, <SVGA Value Port>
        out     dx, eax

2. Initialization

    Check the version number
     loop:
      Write into SVGA_REG_ID the maximum SVGA_ID_* the driver supports.
      Read from SVGA_REG_ID.
       Check if it is the value you wrote.
        If yes, VMware SVGA device supports it
        If no, decrement SVGA_ID_* and goto loop
     This algorithm converges.

    Map the frame buffer and the command FIFO
        Read SVGA_REG_FB_START, SVGA_REG_FB_SIZE, SVGA_REG_MEM_START,
        SVGA_REG_MEM_SIZE.
        Map the frame buffer (FB) and the FIFO memory (MEM).

        This step must occur after the version negotiation above, since by
        default the device is in a legacy-compatibility mode in which there
        is no command FIFO.

    Get the device capabilities and frame buffer dimensions
        Read SVGA_REG_CAPABILITIES, SVGA_REG_MAX_WIDTH, SVGA_REG_MAX_HEIGHT,
        and SVGA_REG_HOST_BITS_PER_PIXEL / SVGA_REG_BITS_PER_PIXEL.

        Note: The capabilities can and do change without the PCI device ID
        changing or the SVGA_REG_ID changing.  A driver should always check
        the capabilities register when loading before expecting any
        capabilities-determined feature to be available.  See below for a list
        of capabilities as of this writing.

        Note: If SVGA_CAP_8BIT_EMULATION is not set, then it is possible that
        SVGA_REG_HOST_BITS_PER_PIXEL does not exist and
        SVGA_REG_BITS_PER_PIXEL should be read instead.

    Optional: Report the Guest Operating System
        Write SVGA_REG_GUEST_ID with the appropriate value from <guest_os.h>.
        While not required in any way, this is useful information for the
        virtual machine to have available for reporting and sanity checking
        purposes.

    SetMode
        Set SVGA_REG_WIDTH, SVGA_REG_HEIGHT, SVGA_REG_BITS_PER_PIXEL
        Read SVGA_REG_FB_OFFSET
        (SVGA_REG_FB_OFFSET is the offset from SVGA_REG_FB_START of the
         visible portion of the frame buffer)
        Read SVGA_REG_BYTES_PER_LINE, SVGA_REG_DEPTH, SVGA_REG_PSEUDOCOLOR,
        SVGA_REG_RED_MASK, SVGA_REG_GREEN_MASK, SVGA_REG_BLUE_MASK

        Note: SVGA_REG_BITS_PER_PIXEL is readonly if
        SVGA_CAP_8BIT_EMULATION is not set in the capabilities register.  Even
        if it is set, values other than 8 and SVGA_REG_HOST_BITS_PER_PIXEL
        will be ignored.

    Enable SVGA
        Set SVGA_REG_ENABLE to 1
        (to disable SVGA, set SVGA_REG_ENABLE to 0.  Setting SVGA_REG_ENABLE
        to 0 also enables VGA.)

    Initialize the command FIFO
        The FIFO is exclusively dword (32-bit) aligned.  The first four
        dwords define the portion of the MEM area that is used for the
        command FIFO.  These are values are all in byte offsets from the
        start of the MEM area.

        A minimum sized FIFO would have these values:
            mem[SVGA_FIFO_MIN] = 16;
            mem[SVGA_FIFO_MAX] = 16 + (10 * 1024);
            mem[SVGA_FIFO_NEXT_CMD] = 16;
            mem[SVGA_FIFO_STOP] = 16;

        Various addresses near the beginning of the FIFO are defined as
        "FIFO registers" with special meaning. If the driver wishes to
        take advantage of the special meaning of these addresses rather
        than using them as part of the command FIFO, the driver must
        reserve space for these registers when setting up the FIFO.
        Typically the driver will set MIN to SVGA_FIFO_NUM_REGS*4.

    Report the guest 3D version
        If your driver supports 3D, write the latest supported 3D
        version (SVGA3D_HWVERSION_CURRENT) to the
        SVGA_FIFO_GUEST_3D_HWVERSION register.

    Enable the command FIFO
        Set SVGA_REG_CONFIG_DONE to 1 after these values have been set.

        Note: Setting SVGA_REG_CONFIG_DONE to 0 will stop the device from
        reading the FIFO until it is reinitialized and SVGA_REG_CONFIG_DONE is
        set to 1 again.

3. SVGA command FIFO protocol

    The FIFO is empty when SVGA_FIFO_NEXT_CMD == SVGA_FIFO_STOP.  The
    driver writes commands to the FIFO starting at the offset specified
    by SVGA_FIFO_NEXT_CMD, and then increments SVGA_FIFO_NEXT_CMD.

    The FIFO is full when SVGA_FIFO_NEXT_CMD is one word before SVGA_FIFO_STOP.

    When the FIFO becomes full, the driver must wait for space to become
    available. It can do this via various methods (busy-wait, legacy sync)
    but the preferred method is to use the FIFO_PROGRESS interrupt.

    The SVGA device does not guarantee that all of FIFO memory is valid
    at all times. The device is free to discard the contents of any memory
    which is not part of the active portion of the FIFO. The active portion
    of the FIFO is defined as the region with valid commands (starting
    at SVGA_FIFO_STOP and ending at SVGA_FIFO_NEXT_CMD) plus the reserved
    portion of the FIFO.

    By default, only one word of memory is 'reserved'. If the FIFO supports
    the SVGA_FIFO_CAP_RESERVE capability, the device supports reserving
    driver-defined amounts of memory. If both the device and driver support
    this operation, it's possible to write multiple words of data between
    updates to the FIFO control registers.

    The simplest way to use the FIFO is to write one word at a time, but the
    highest-performance way to use the FIFO is to reserve enough space for
    an entire command or group of commands, write the commands directly to
    FIFO memory, then "commit" the command(s) by updating the FIFO control
    registers.

    A reference implementation of this reserve/commit algorithm is provided
    in svga.c, in SVGA_FIFOReserve() and SVGA_FIFOCommit(). In the common
    case, this algorithm lets drivers assemble commands directly in FIFO
    memory without any additional copies or memory allocation.

4. Synchronization

    The primary synchronization primitive defined by the SVGA device is
    "Sync to fence". A "fence" is a numbered marker inserted into the FIFO
    command stream. The driver can insert fences at any time, and efficiently
    determine the value of the last fence processed by the device.

    "Sync to fence" is the process of waiting for a particular fence to be
    processed. This may be important for several reasons:

       - Flow control. For interactivity, it is important to put an upper
         limit on the amount by which the device may lag the application.

       - Waiting for DMA completion. If the driver needs to recycle a DMA
         buffer or complete a DMA operation synchronously, it must sync
         to a fence which occurred after the DMA operation in the command
         stream.

       - Waiting for accelerated 2D operations. If a 2D driver needs to
         write to a portion of the framebuffer which is affected by
         an accelerated blit, it should sync to a fence which occurred
         after the blit.

    There are multiple possible implementations of Sync to Fence, depending
    on the capabilities of the SVGA device you're driving. Very old versions
    of the VMware SVGA device did not support fences at all. For these
    devices, you must always perform a "legacy sync". New virtual machines
    with Workstation 6.5 virtual hardware or later support an IRQ-driven
    sync operation. For all other versions of the SVGA device, the best
    approach is a hybrid in which you synchronously use the SYNC/BUSY
    registers to process the FIFO until the sync has passed.

    FIFO synchronization is a very complex topic, and it isn't covered fully
    by this document. Please see the synchronization-related comments in
    svga_reg.h, and the reference implementation of these primitives in
    svga.c.

5. Cursor

    When SVGA_CAP_CURSOR is set, hardware cursor support is available.  In
    practice, SVGA_CAP_CURSOR will only be set when SVGA_CAP_CURSOR_BYPASS is
    also set and drivers supporting a hardware cursor should only worry about
    SVGA_CAP_CURSOR_BYPASS and only use the FIFO to define the cursor.  See
    below for more information.

6. Pseudocolor

    When the read-only register SVGA_REG_PSEUDOCOLOR is 1, the device is in a
    colormapped mode whose index width and color width are both SVGA_REG_DEPTH.
    Thus far, 8 is the only depth at which pseudocolor is ever used.

    In pseudocolor, the colormap is programmed by writing to the SVGA palette
    registers.  These start at SVGA_PALETTE_BASE and are interpreted as
    follows:

        SVGA_PALETTE_BASE + 3*n         - The nth red component
        SVGA_PALETTE_BASE + 3*n + 1     - The nth green component
        SVGA_PALETTE_BASE + 3*n + 2     - The nth blue component

    And n ranges from 0 to ((1<<SVGA_REG_DEPTH) - 1).

7. Pseudocolor

    After initialization, the driver can write directly to the frame
    buffer.  The updated frame buffer is not displayed immediately, but
    only when an update command is sent.  The update command
    (SVGA_CMD_UPDATE) defines the rectangle in the frame buffer that has
    been modified by the driver, and causes that rectangle to be updated
    on the screen.

    A complete driver can be developed this way.  For increased
    performance, additional commands are available to accelerate common
    operations.  The two most useful are SVGA_CMD_RECT_FILL and
    SVGA_CMD_RECT_COPY.

    After issuing an accelerated command, the FIFO should be sync'd, as
    described above, before writing to the frame buffer.


SVGA_REG_FB_OFFSET and SVGA_REG_BYTES_PER_LINE may change after SVGA_REG_WIDTH
or SVGA_REG_HEIGHT is set.  Also the VGA registers must be written to after
setting SVGA_REG_ENABLE to 0 to change the display to a VGA mode.

 8. Mode changes

    The video mode may be changed by writing to the WIDTH, HEIGHT,
    and/or DEPTH registers again, after initialization. All of the
    registers listed in the 'SetMode' initialization section above
    should be reread afterwards. Additionally, when changing modes, it
    can be convenient to set SVGA_REG_ENABLE to 0, change
    SVGA_REG_WIDTH, SVGA_REG_HEIGHT, and SVGA_REG_BITS_PER_PIXEL (if
    available), and then set SVGA_REG_ENABLE to 1 again. This is
    optional, but it will avoid intermediate states in which only one
    component of the new mode has been set.


 9. Capabilities

The capabilities register (SVGA_REG_CAPABILITIES) is an array of bits that
indicates the capabilities of the SVGA emulation.  A driver should check
SVGA_REG_CAPABILITIES every time it loads before relying on any feature that
is only optionally available.

XXX: There is also a capabilities register in the FIFO register space.
     It is not documented in this file, but all of the available bits
     are listed in svga_reg.h.

Some of the capabilities determine which FIFO commands are available.  This
table shows which capability indicates support for which command.

    FIFO Command                        Capability
    ------------                        ----------

    SVGA_CMD_RECT_FILL                  SVGA_CAP_RECT_FILL
    SVGA_CMD_RECT_COPY                  SVGA_CAP_RECT_COPY
    SVGA_CMD_DEFINE_BITMAP              SVGA_CAP_OFFSCREEN
    SVGA_CMD_DEFINE_BITMAP_SCANLINE     SVGA_CAP_OFFSCREEN
    SVGA_CMD_DEFINE_PIXMAP              SVGA_CAP_OFFSCREEN
    SVGA_CMD_DEFINE_PIXMAP_SCANLINE     SVGA_CAP_OFFSCREEN
    SVGA_CMD_RECT_BITMAP_FILL           SVGA_CAP_RECT_PAT_FILL
    SVGA_CMD_RECT_PIXMAP_FILL           SVGA_CAP_RECT_PAT_FILL
    SVGA_CMD_RECT_BITMAP_COPY           SVGA_CAP_RECT_PAT_FILL
    SVGA_CMD_RECT_PIXMAP_COPY           SVGA_CAP_RECT_PAT_FILL
    SVGA_CMD_FREE_OBJECT                SVGA_CAP_OFFSCREEN
    SVGA_CMD_RECT_ROP_FILL              SVGA_CAP_RECT_FILL +
                                            SVGA_CAP_RASTER_OP
    SVGA_CMD_RECT_ROP_COPY              SVGA_CAP_RECT_COPY +
                                            SVGA_CAP_RASTER_OP
    SVGA_CMD_RECT_ROP_BITMAP_FILL       SVGA_CAP_RECT_PAT_FILL +
                                            SVGA_CAP_RASTER_OP
    SVGA_CMD_RECT_ROP_PIXMAP_FILL       SVGA_CAP_RECT_PAT_FILL +
                                            SVGA_CAP_RASTER_OP
    SVGA_CMD_RECT_ROP_BITMAP_COPY       SVGA_CAP_RECT_PAT_FILL +
                                            SVGA_CAP_RASTER_OP
    SVGA_CMD_RECT_ROP_PIXMAP_COPY       SVGA_CAP_RECT_PAT_FILL +
                                            SVGA_CAP_RASTER_OP
    SVGA_CMD_DEFINE_CURSOR              SVGA_CAP_CURSOR
    SVGA_CMD_DISPLAY_CURSOR             SVGA_CAP_CURSOR
    SVGA_CMD_MOVE_CURSOR                SVGA_CAP_CURSOR
    SVGA_CMD_DEFINE_ALPHA_CURSOR        SVGA_CAP_ALPHA_CURSOR
    SVGA_CMD_DRAW_GLYPH                 SVGA_CAP_GLYPH
    SVGA_CMD_DRAW_GLYPH_CLIPPED         SVGA_CAP_GLYPH_CLIPPING
    SVGA_CMD_ESCAPE                     SVGA_FIFO_CAP_ESCAPE

    (NOTE: Many of the commands here are deprecated, and listed
           in the table only for reference. All comments for glyph,
           bitmap, and pixmap drawing are not implemented in the
           latest releases of VMware products.)

Other capabilities indicate other functionality as described below:

    SVGA_CAP_CURSOR_BYPASS
        The hardware cursor can be drawn via SVGA Registers (without requiring
        the FIFO be synchronized and will be drawn potentially before any
        outstanding unprocessed FIFO commands).

        Note:  Without SVGA_CAP_CURSOR_BYPASS_2, cursors drawn this way still
        appear in the guest's framebuffer and need to be turned off before any
        save under / overlapping drawing and turned back on after.  This can
        cause very noticeable cursor flicker.

    SVGA_CAP_CURSOR_BYPASS_2
        Instead of turning the cursor off and back on around any overlapping
        drawing, the driver can write SVGA_CURSOR_ON_REMOVE_FROM_FB and
        SVGA_CURSOR_ON_RESTORE_TO_FB to SVGA_REG_CURSOR_ON.  In almost all
        cases these are NOPs and the cursor will be remain visible without
        appearing in the guest framebuffer.  In 'direct graphics' modes like
        Linux host fullscreen local displays, however, the cursor will still
        be drawn in the framebuffer, still flicker, and be drawn incorrectly
        if a driver does not use SVGA_CURSOR_ON_REMOVE_FROM_FB / RESTORE_TO_FB.

    SVGA_CAP_8BIT_EMULATION
        SVGA_REG_BITS_PER_PIXEL is writable and can be set to either 8 or
        SVGA_REG_HOST_BITS_PER_PIXEL.  Otherwise the only SVGA modes available
        inside a virtual machine must match the host's bits per pixel.

        Note: Some versions which lack SVGA_CAP_8BIT_EMULATION also lack the
        SVGA_REG_HOST_BITS_PER_PIXEL and a driver should assume
        SVGA_REG_BITS_PER_PIXEL is both read-only and initialized to the only
        available value if SVGA_CAP_8BIT_EMULATION is not set.

    SVGA_CAP_OFFSCREEN_1
        SVGA_CMD_RECT_FILL, SVGA_CMD_RECT_COPY, SVGA_CMD_RECT_ROP_FILL,
        SVGA_CMD_RECT_ROP_COPY can operate with a source or destination (or
        both) in offscreen memory.

        Usable offscreen memory is a rectangle located below the last scanline
        of the visible memory:
        x1 = 0
        y1 = (SVGA_REG_FB_SIZE + SVGA_REG_BYTES_PER_LINE - 1) /
             SVGA_REG_BYTES_PER_LINE
        x2 = SVGA_REG_BYTES_PER_LINE / SVGA_REG_DEPTH
        y2 = SVGA_REG_VRAM_SIZE / SVGA_REG_BYTES_PER_LINE


Cursor Handling
---------------

Several cursor drawing mechanisms are supported for legacy
compatibility. The current mechanism, and the only one that new
drivers need support, is "Cursor Bypass 3".

In Cursor Bypass 3 mode, the cursor image is defined via FIFO
commands, but the cursor position and visibility is reported
asynchronously by writing to FIFO registers.

A driver defines an AND/XOR hardware cursor using
SVGA_CMD_DEFINE_CURSOR to assign an ID and establish the AND and XOR
masks with the hardware.  A driver uses SVGA_CMD_DEFINE_ALPHA_CURSOR
to define a 32 bit mask whose top 8 bits are used to blend the cursor
image with the pixels it covers.  Alpha cursor support is only
available when SVGA_CAP_ALPHA_CURSOR is set. Note that alpha cursors
use pre-multiplied alpha.

---
