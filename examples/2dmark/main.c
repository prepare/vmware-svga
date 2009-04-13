/*
 * Simple 2D graphics benchmark.
 *
 * The VMware SVGA device typically coalesces update rectangles and
 * processes them asynchronously. This makes it difficult to get
 * meaningful 2D benchmark numbers from tools which run inside a
 * normal guest OS.
 *
 * This tool sweeps through multiple 2D update sizes on multiple
 * video modes. After the test, results are summarized to the screen
 * (in VGA text mode) and to vmware.log.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga.h"
#include "intr.h"
#include "console_vga.h"
#include "vmbackdoor.h"
#include "svga3dutil.h"

struct {
   uint32 value;
   const char *label;
} sizes[] = {
   { 1,    "    1" },
   { 8,    "    8" },
   { 64,   "   64" },
   { 233,  "  233" },  /* Prime */
   { 256,  "  256" },
   { 2048, " 2048" },
   { 2099, " 2099" },  /* Prime */
   { 4096, " 4096" },
};


/*
 * benchmarkAtSize --
 *
 *    Inner benchmarking loop, tests one combination of fb and update sizes.
 */

static FPSCounterState *
benchmarkAtSize(uint32 screen, uint32 update)
{
   int i = 3;
   static FPSCounterState fps;

   memset(&fps, 0, sizeof fps);

   /* Clear the screen and change modes */
   memset(gSVGA.fbMem, 0x40, screen * screen * sizeof(uint32));
   SVGA_SetMode(screen, screen, 32);

   /* Make sure the FIFO is empty */
   SVGA_SyncToFence(SVGA_InsertFence());

   /*
    * UpdateFPSCounter returns TRUE each time it's output is updated.
    * The first time, it won't have an FPS reading available yet. (It is
    * guaranteed to return TRUE on its first call.) The second time,
    * an FPS reading will be ready. We wait until the third time, in
    * order to give the readings extra time to stabilize.
    *
    * Note that the 'i--' part of this expression only executes when
    * UpdateFPSCounter returns TRUE.
    */
   do {
      /* Synchronously update the screen */
      SVGA_Update(0, 0, update, update);
      SVGA_SyncToFence(SVGA_InsertFence());
   } while (!SVGA3DUtil_UpdateFPSCounter(&fps) || i--);

   return &fps;
}


/*
 * runBenchmark --
 *
 *    Main benchmark loop. Run through all valid combinations
 *    of update and display sizes.
 */

static void
runBenchmark()
{
   const int numSizes = sizeof sizes / sizeof sizes[0];
   int i, j;

   Console_WriteString("Synchronous 2D updates per second.\n"
                       "Video mode width/height on Y axis, update size on X axis.\n"
                       "\n");

   /* Size headings across the top of the screen */
   Console_WriteString("      | ");
   for (i = 0; i < numSizes; i++) {
      Console_WriteString("   ");
      Console_WriteString(sizes[i].label);
   }
   Console_WriteString("\n");
   for (i = 0; i < 79; i++) {
      Console_WriteString("-");
   }
   Console_WriteString("\n");

   for (i = 0; i < numSizes; i++) {
      Console_Format("%s | ", sizes[i].label);

      for (j = 0; j <= i; j++) {
         char *fps = benchmarkAtSize(sizes[i].value, sizes[j].value)->text;

         /* Hack to make the string shorter by cutting off "FPS" label. */
         fps[7] = '\0';

         Console_Format(" %s", fps);
      }

      Console_WriteString("\n");
   }

   Console_WriteString("\nBenchmark complete. Results are "
                       "also available in the VMX log.");
}


/*
 * main --
 *
 *    Initialization and results reporting.
 */

int
main(void)
{
   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);
   ConsoleVGA_Init();
   SVGA_Init();

   runBenchmark();

   SVGA_Disable();
   VMBackdoor_VGAScreenshot();

   return 0;
}
