/*
 * Stress test for GMR-to-screen blits with a static but highly
 * discontiguous mapping.
 *
 * In this test, we define a single large discontiguous GMR, and blit
 * to the screen from random addresses in this GMR.
 *
 * To measure this test's performance, use mksPerfTool to read the
 * number of SWB messages processed per second on the host.
 */

#include "svga.h"
#include "gmr.h"
#include "screen.h"
#include "intr.h"
#include "math.h"


int
main(void)
{
   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);
   SVGA_Init();
   GMR_Init();
   Heap_Reset();
   SVGA_SetMode(0, 0, 32);
   Screen_Init();

   SVGAScreenObject myScreen = {
      .structSize = sizeof(SVGAScreenObject),
      .id = 0,
      .flags = SVGA_SCREEN_HAS_ROOT | SVGA_SCREEN_IS_PRIMARY,
      .size = { 1600, 1200 },
      .root = { -1234, 5678 },
   };
   Screen_Create(&myScreen);

   const uint32 gmrId = 0;
   uint32 numPages = 1 + (myScreen.size.width * myScreen.size.height *
                          sizeof(uint32)) / PAGE_SIZE;

   PPN pages = GMR_DefineEvenPages(gmrId, numPages);

   const uint32 bitsPerPixel = 32;
   const uint32 colorDepth = 24;
   const uint32 bytesPerLine = myScreen.size.width * sizeof(uint32);

   const SVGAGMRImageFormat format = {{{
      .bitsPerPixel = bitsPerPixel,
      .colorDepth = colorDepth,
   }}};

   SVGAGuestPtr gPtr = {
      .gmrId = gmrId,
      .offset = 0,
   };
   Screen_DefineGMRFB(gPtr, bytesPerLine, format);

   /*
    * This is much like the tiny-2d-updates test, except with Screen
    * Object and with larger blits... We emit an endless series of
    * frames, each one of which has a different screen color (we XOR a
    * constant with the GMR). For each frame, we update the screen in
    * a grid of small tiles.
    */

   const uint32 tileSize = 21;  // Some random odd size...

   while (1) {
      PPN p;
      int x, y;

      /*
       * We don't synchronize these writes to the host's DMA
       * completion.  This means we'll get tearing- you'll see a
       * horizontal line at some point, which indicates how far along
       * in this repaint we are at the time the host gets around to
       * refreshing the screen.
       *
       * No big deal. This isn't intended to be an example of correct
       * rendering, we're just testing to see how fast the host can
       * perform these blits out of discontiguous memory.
       */

      p = pages;
      for (x = 0; x < numPages; x++) {
         uint32 *pageData = PPN_POINTER(p);
         for (y = 0; y < PAGE_SIZE / sizeof(uint32); y++) {
            pageData[y] ^= 0xFFFFFF;
         }
         p += 2;
      }

      for (y = 0; y < myScreen.size.height; y += tileSize) {
         for (x = 0; x < myScreen.size.width; x += tileSize) {

            SVGASignedPoint blitOrigin = { x, y };
            SVGASignedRect blitDest = { x, y, x+tileSize, y+tileSize };

            Screen_BlitFromGMRFB(&blitOrigin, &blitDest, myScreen.id);
         }
      }
   }

   return 0;
}
