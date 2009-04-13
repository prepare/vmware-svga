/*
 * SVGA3D example: Stress-test for our FIFO Fence synchronization.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga3dutil.h"
#include "svga3dtext.h"

#define SYNCS_PER_FRAME      1024

int
main(void)
{
   int i, j;
   uint32 fence = 0;
   static FPSCounterState gFPS;

   SVGA3DUtil_InitFullscreen(CID, 640, 480);
   SVGA3DText_Init();

   while (1) {
      SVGA3DUtil_UpdateFPSCounter(&gFPS);

      Console_Clear();
      Console_Format("VMware SVGA3D Example:\n"
                     "FIFO Fence stress-test.\n"
                     "%d syncs per frame.\n"
                     "\n"
                     "%s\n"
                     "\n"
                     "Latest fence: 0x%08x\n"
                     "   IRQ count: %d\n",
                     SYNCS_PER_FRAME, gFPS.text, fence, gSVGA.irq.count);
      SVGA3DText_Update();

      SVGA3DUtil_ClearFullscreen(CID, SVGA3D_CLEAR_COLOR, 0, 1.0f, 0);
      SVGA3DText_Draw();
      SVGA3DUtil_PresentFullscreen();

      for (j = 0; j < SYNCS_PER_FRAME; j++) {
         for (i=0; i<100; i++) {
            SVGA_InsertFence();
         }

         fence = SVGA_InsertFence();

         for (i=0; i<50; i++) {
            SVGA_InsertFence();
         }

         SVGA_SyncToFence(fence);
      }
   }

   return 0;
}
