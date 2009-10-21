/*
 * Very tiny benchmark for very tiny 2D updates.
 *
 * This sets an SVGA video mode, and uses a sequence of very tiny 2D
 * FIFO updates to repaint the screen pixel-by-pixel in a loop. To
 * measure performance, attach mksPerfTool and view the amount of CPU
 * time used by the MKS and the number of FIFO commands per second.
 * Do *not* use the MKS "frames per second" as a performance metric
 * for this test, as the MKS's "frames" are arbitrary and unrelated
 * to our test's frames.
 */

#include "svga.h"
#include "intr.h"

/*
 * paintScreen --
 *
 *    Repaint the screen once: Fill the framebuffer,
 *    and use very tiny FIFO updates to cover it all.
 */

void
paintScreen(uint32 color)
{
   uint32 *fb = (uint32*) gSVGA.fbMem;
   int x, y;
   static uint32 fence = 0;

   /*
    * Flow control: Before we re-write the beginning of the
    * framebuffer, make sure the host has at least finished drawing
    * the beginning of the last frame.
    *
    * This isn't as strict as it should be: For proper 2D flow
    * control, we really need to either do a full sync at the end of
    * each frame, or double-buffer (alternating buffers on every other
    * frame). This method can produce glitches, but it's still decent
    * for benchmarking purposes.
    */

   SVGA_SyncToFence(fence);
   fence = SVGA_InsertFence();

   /*
    * Update the screen, pixel by pixel.
    */

   for (y = 0; y < gSVGA.height; y++) {
      uint32 *row = fb;
      fb = (uint32*) (gSVGA.pitch + (uint8*)fb);

      for (x = 0; x < gSVGA.width; x++) {
         *(row++) = color;
         SVGA_Update(x, y, 1, 1);
      }
   }
}

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
   SVGA_SetMode(640, 480, 32);

   while (1) {
      /* Alternate colors on each frame */
      paintScreen(0x747cba);
      paintScreen(0xbebebe);
   }

   return 0;
}
