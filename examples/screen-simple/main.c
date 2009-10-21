/*
 * Simple example for Screen Objects in the SVGA device.
 *
 * This is an optional feature of the VMware SVGA device which allows
 * multiple screens to be managed dynamically. It replaces legacy
 * multi-monitor mode. It also allows the video driver to manage its
 * own framebuffer memory. The host will never DMA to or from this
 * memory unless the guest requests a DMA operation, and the guest is
 * free to allocate or reallocate framebuffer memory at any time.
 *
 * This is a bare-bones example which uses the Screen Object extension
 * to create a single screen and draw a test pattern to it.
 *
 * This example does not use the legacy Guest Framebuffer (GFB) memory
 * at BAR1 at all. A system memory GMR is used as a data source for
 * blits to the screen.
 */

#include "svga.h"
#include "gmr.h"
#include "screen.h"
#include "intr.h"
#include "datafile.h"

DECLARE_DATAFILE(testPatternData, testpattern_z);


/*
 * main --
 *
 *    Main loop and initialization.
 */

int
main(void)
{
   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);
   SVGA_Init();
   GMR_Init();
   Heap_Reset();

   /*
    * When you use Screen Objects, you no longer need the legacy SVGA
    * framebuffer or display mode. You still need to call SetMode to
    * enable the device and the command FIFO, but it doesn't have to
    * have a valid video mode. Setting a 0x0 video mode explicitly
    * tells the device that we don't need the legacy framebuffer.
    */
   SVGA_SetMode(0, 0, 32);

   /*
    * Screen_Init requires the SVGA FIFO, which is set up in SVGA_SetMode.
    */
   Screen_Init();

   /*
    * Define a screen.
    *
    * This screen has ID zero, which is both the first valid ID and
    * the default ID used by the legacy SVGA device interfaces. This
    * will replace any legacy screen with our new screen.
    *
    * We mark this screen as 'primary', and root it in the virtual
    * coordinate space. We'll just pick a totally arbitrary root
    * position.
    */

   SVGAScreenObject myScreen = {
      .structSize = sizeof(SVGAScreenObject),
      .id = 0,
      .flags = SVGA_SCREEN_HAS_ROOT | SVGA_SCREEN_IS_PRIMARY,
      .size = { 640, 480 },
      .root = { -500, 10000 },
   };
   Screen_Define(&myScreen);

   /*
    * Create a system memory framebuffer.
    *
    * This step is optional. Most drivers will probably want a
    * framebuffer, but the SVGA device does not require that you have
    * one. For example, a character-cell driver could point the GMRFB
    * at a font table, and draw characters using individual
    * GMRFB-to-screen blits from the font table.  A more complex
    * driver which wants to avoid tearing could double-buffer the
    * whole screen, or even create a queue of smaller update-sized
    * buffers which would be used as GMRFB memory.
    *
    * There are three steps for creating a framebuffer:
    *
    *   1. Allocate system memory. This is done using a trivial
    *      heap allocator, which uses a chunk of contiguous system
    *      memory that begins immediately after our binary's last
    *      segment ends.
    *
    *      This memory doesn't need to be contiguous, but contiguous
    *      memory may give better performance, so it's preferred.
    *
    *   2. Define a Guest Memory Region (GMR), which is basically a
    *      page table which lets the SVGA device access this memory.
    *      GMRs are how we support discontiguous memory. Any pages
    *      which are part of a GMR should be locked down, so your
    *      OS's virtual memory subsystem can't move them or page
    *      them out.
    *
    *      Defining a GMR is relatively costly, so it's best if
    *      drivers use GMRs for very coarse-grained memory allocation.
    *      It would make sense to use a GMR for a general-purpose DMA
    *      heap, or for a 2D framebuffer- but not for a single DMA
    *      buffer.
    *
    *   3. Define a GMRFB. This is a tiny piece of state which tells
    *      future blit operations where to pull their source data
    *      from. It's perfectly fine to redefine the GMRFB as often
    *      as you like, but in this simple example we'll set it once
    *      and leave it alone.
    */

   /*
    * Steps 1 and 2 are handled by GMR_DefineContiguous, in our
    * small 'gmr.c' utility library.
    *
    * Use GMR ID 0, the first user-defined GMR. We'll allocate
    * enough memory for a 32 bit per pixel framebuffer.
    */

   const uint32 gmrId = 0;
   const uint32 bitsPerPixel = 32;
   const uint32 colorDepth = 24;

   const uint32 bytesPerPixel = bitsPerPixel >> 3;
   const uint32 fbBytesPerLine = myScreen.size.width * bytesPerPixel;
   const uint32 fbSizeInBytes = fbBytesPerLine * myScreen.size.height;
   const uint32 fbSizeInPages = (fbSizeInBytes + PAGE_MASK) / PAGE_SIZE;

   PPN fbFirstPage = GMR_DefineContiguous(gmrId, fbSizeInPages);
   uint32 *fbPointer = PPN_POINTER(fbFirstPage);

   /*
    * Step 3: Use the DEFINE_GMRFB command to tell the device about
    * our framebuffer and its format. It is in our user-defined GMR,
    * at offset zero.
    *
    * the SVGAGMRImageFormat type is a packed 32-bit word which
    * encodes our pixel size and color depth. (The upper 16 bits are
    * reserved for future formats.)
    */

   SVGAGuestPtr fbGuestPtr = {
      .gmrId = gmrId,
      .offset = 0,
   };

   SVGAGMRImageFormat fbFormat = {{{
      .bitsPerPixel = bitsPerPixel,
      .colorDepth = colorDepth,
   }}};

   Screen_DefineGMRFB(fbGuestPtr, fbBytesPerLine, fbFormat);

   /*
    * Now we have a framebuffer! Fill it with a test pattern,
    * and blit it to the screen.
    */

   SVGASignedPoint blitOrigin = { 0, 0 };
   SVGASignedRect blitDest = { 0, 0, myScreen.size.width, myScreen.size.height };

   DataFile_Decompress(testPatternData, (void*) fbPointer, fbSizeInBytes);

   Screen_BlitFromGMRFB(&blitOrigin, &blitDest, myScreen.id);
   uint32 dmaFence = SVGA_InsertFence();

   /*
    * Wait for the blit's DMA to complete, then clobber our
    * framebuffer with a different value, to keep the SVGA device
    * honest. It shouldn't access the GMRFB memory any more now that
    * the blit is done. If we were doing animation, we could use this
    * guarantee to perform double-buffering, for example.
    */

   SVGA_SyncToFence(dmaFence);
   memset(fbPointer, 0x42, fbSizeInBytes);

   /*
    * Our work here is done. Sleep indefinitely.
    */

   while (1) {
      Intr_Halt();
   }

   return 0;
}
