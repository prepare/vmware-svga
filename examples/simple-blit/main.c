/*
 * SVGA3D example: Simple BLIT (Block Image Transfer) which updates
 *                 the render target surface ID.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "types.h"
#include "svga3dutil.h"
#include "svga3dtext.h"

FPSCounterState gFPS;

/*
 * Alpha, red, green, blue components of color.
 */
static uint32 a = 255, r = 0, g = 0, b = 0;


/*
 * DMA pools allow for the allocation of GMR memory.
 * The re-use policy is handled by the allocation routines.
 */
static uint32 blitSize = 0;
DMAPool blitDMA;


/*
 * render --
 *
 *   Set up render state, and use surface DMA to update the
 *   render target with a solid color that goes from black
 *   to white.
 *
 */

void
render(void)
{
   DMAPoolBuffer *dma = NULL;
   uint32 *buffer = NULL;
   uint32 color;

   dma = SVGA3DUtil_DMAPoolGetBuffer(&blitDMA);
   buffer = (uint32 *)dma->buffer;

   /* uint32 memset. */
   color = ((a&255) << 24) | ((r&255) << 16) | ((b&255) << 8) | (g&255);
   memset32(buffer, color, blitSize / sizeof *buffer);
   r++; g++; b++;

   /*
    * Copy pixel data from our temporary memory in the GMR into
    * the render target.  This is a BLIT operation from memory
    * in the guest to the host render target.
    */
   SVGA3DUtil_SurfaceDMA2D(gFullscreen.colorImage.sid, &dma->ptr,
                           SVGA3D_WRITE_HOST_VRAM,
                           gSVGA.width, gSVGA.height);
   SVGA3DUtil_AsyncCall((AsyncCallFn) SVGA3DUtil_DMAPoolFreeBuffer, dma);
}


/*
 * main --
 *
 *    Our example's entry point, invoked directly by the bootloader.
 */

int
main(void)
{
   SVGA3DUtil_InitFullscreen(CID, 800, 600);
   SVGA3DText_Init();

   /*
    * Allocate 2 buffers for DMA.  Each buffer is the size of the display
    * so that we can fill the buffers with color data and DMA that buffer
    * to the render target.
    */
   blitSize = gSVGA.width * gSVGA.height * sizeof(uint32);
   SVGA3DUtil_AllocDMAPool(&blitDMA, blitSize, 4);

   while (1) {
      if (SVGA3DUtil_UpdateFPSCounter(&gFPS)) {
         Console_Clear();
         Console_Format("VMware SVGA3D Example:\n"
                        "Simple BLIT of image into render target.\n%s",
                        gFPS.text);
         SVGA3DText_Update();
         VMBackdoor_VGAScreenshot();
      }

      SVGA3DUtil_ClearFullscreen(CID, SVGA3D_CLEAR_COLOR, 0x113366, 1.0f, 0);

      render();

      SVGA3DText_Draw();
      SVGA3DUtil_PresentFullscreen();
   }

   return 0;
}
