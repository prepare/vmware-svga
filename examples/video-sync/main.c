/*
 * video-sync -- Test video DMA synchronization, by displaying a
 * sequence of animated frames with flow control and multi-frame
 * buffering.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */


#include "svga.h"
#include "png.h"
#include "intr.h"
#include "datafile.h"

/*
 * Our background image, in PNG format.
 */

DECLARE_DATAFILE(screenPNGFile, screen_png);

/*
 * generateFrame --
 *
 *    Generate one frame of video, in UYVY format.
 */

static void
generateFrame(uint8 *buffer,  // OUT
              uint32 width,   // IN
              uint32 height,  // IN
              uint32 frame)   // IN
{
   uint32 wordPitch = width / 2;
   uint32 numWords = wordPitch * height;
   int x = frame % width;
   uint32 *linePtr = (uint32*)buffer + (x >> 1);
   uint32 lineWord;

   /*
    * Clear it multiple times, so it will be obvious if the
    * host reads a frame that we're still writing to.
    */

   //                 Y1VVY0UU
   memset32(buffer, 0xFFFFFFFF, numWords);
   memset32(buffer, 0x40804080, numWords);

   /*
    * Draw a vertical line that moves right on each frame.  This is
    * the easiest way to make it obvious when the image tears.
    *
    * This test will also show when the luminance bytes in the
    * packed-pixel decoder are out of order.
    */

   if (x & 1) {
      lineWord = 0xFF804080;
   } else {
      lineWord = 0x4080FF80;
   }

   while (height--) {
      *linePtr = lineWord;
      linePtr += wordPitch;
   }
}


/*
 * main --
 *
 *    Initialization and main loop.
 */

int
main(void)
{
   PNGChunkIHDR *screenPNG = PNG_Header(screenPNGFile->ptr);
   uint32 width = bswap32(screenPNG->width);
   uint32 height = bswap32(screenPNG->height);

   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);
   SVGA_Init();
   SVGA_SetMode(width, height, 32);

   /*
    * Draw the background image
    */

   PNG_DecompressBGRX(screenPNG, (uint32*) gSVGA.fbMem, gSVGA.pitch);
   SVGA_Update(0, 0, width, height);

   /*
    * Initialize the video overlay unit. We're displaying DVD-resolution
    * letterboxed 16:9 video, in UYVY (packed-pixel) format.
    */

   SVGAOverlayUnit overlay = {
      .enabled = TRUE,
      .format = VMWARE_FOURCC_UYVY,
      .width = 720,
      .height = 480,
      .srcWidth = 720,
      .srcHeight = 480,
      .dstX = 1,
      .dstY = 92,
      .dstWidth = 1022,
      .dstHeight = 574,
      .pitches[0] = 1440,
   };

   SVGA_VideoSetAllRegs(0, &overlay, SVGA_VIDEO_PITCH_3);

   /*
    * Main loop. Loop over each frame in the ring buffer repeatedly.
    * We wait for the DMA buffer to become available, fill it with the
    * next frame, then program the overlay unit to display that frame.
    */

   uint32 frameCounter = 0;

   uint32 baseOffset = width * height * 4;
   uint32 frameSize = overlay.pitches[0] * overlay.height;
   static uint32 fences[16];

   while (1) {
      uint32 bufferId;

      for (bufferId = 0; bufferId < arraysize(fences); bufferId++) {
         uint32 bufferOffset = baseOffset + bufferId * frameSize;
         uint8 *bufferPtr = gSVGA.fbMem + bufferOffset;

         SVGA_SyncToFence(fences[bufferId]);

         generateFrame(bufferPtr, overlay.width, overlay.height, frameCounter++);

         SVGA_VideoSetReg(0, SVGA_VIDEO_DATA_OFFSET, bufferOffset);
         SVGA_VideoFlush(0);

         fences[bufferId] = SVGA_InsertFence();
      }
   }

   return 0;
}
