/*
 * video-formats -- Demonstrate all supported video overlay formats.
 *
 * XXX: There are some known bugs in the currently released VMware
 *      products, which are exposed by this test:
 *
 *   1. The very first VideoFlush may not appear. In this test,
 *      the bug manifests as "No Overlay" for test #1.
 *
 *   2. Software emulated scaling is very low quality.
 *
 *   3. If the host is using hardware video overlay rather than
 *      its software fallback, it assumes that colorkey is always
 *      enabled. This means our video will only draw in the black
 *      portions of the background image (inside the "X", and
 *      the box around the "No overlay" text.)
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */


#include "svga.h"
#include "png.h"
#include "intr.h"
#include "datafile.h"


/*
 * This is our video test card, in UYVY format.
 *
 * It's a 720x576 pixel 4:3 aspect test card designed by Barney
 * Wol. (http://www.barney-wol.net/testpatterns)
 */

DECLARE_DATAFILE(testCardFile, wols4x3_yuv_z);

#define TESTCARD_WIDTH  720
#define TESTCARD_HEIGHT 576


/*
 * Our background image, in PNG format.
 *
 * This has 'cutouts' where we're supposed to display the test
 * pattern. Each of these are described by the table of overlay
 * settings below.
 */

DECLARE_DATAFILE(screenPNGFile, screen_png);

#define OFFSET_YUY2  0x400000
#define OFFSET_UYVY  0x500000
#define OFFSET_YV12  0x600000

static SVGAOverlayUnit overlays[] = {
   // #0 - YUY2 Large
   {
      .enabled = TRUE,
      .format = VMWARE_FOURCC_YUY2,
      .width = TESTCARD_WIDTH,
      .height = TESTCARD_HEIGHT,
      .srcWidth = TESTCARD_WIDTH,
      .srcHeight = TESTCARD_HEIGHT,
      .dstX = 109,
      .dstY = 407,
      .dstWidth = 320,
      .dstHeight = 240,
      .pitches[0] = TESTCARD_WIDTH * 2,
      .dataOffset = OFFSET_YUY2,
   },

   // #1 - YV12 Large
   {
      .enabled = TRUE,
      .format = VMWARE_FOURCC_YV12,
      .width = TESTCARD_WIDTH,
      .height = TESTCARD_HEIGHT,
      .srcWidth = TESTCARD_WIDTH,
      .srcHeight = TESTCARD_HEIGHT,
      .dstX = 564,
      .dstY = 58,
      .dstWidth = 320,
      .dstHeight = 240,
      .pitches[0] = TESTCARD_WIDTH,
      .pitches[1] = TESTCARD_WIDTH / 2,
      .pitches[2] = TESTCARD_WIDTH / 2,
      .dataOffset = OFFSET_YV12,
   },

   // #2 - UYVY Large
   {
      .enabled = TRUE,
      .format = VMWARE_FOURCC_UYVY,
      .width = TESTCARD_WIDTH,
      .height = TESTCARD_HEIGHT,
      .srcWidth = TESTCARD_WIDTH,
      .srcHeight = TESTCARD_HEIGHT,
      .dstX = 564,
      .dstY = 407,
      .dstWidth = 320,
      .dstHeight = 240,
      .pitches[0] = TESTCARD_WIDTH * 2,
      .dataOffset = OFFSET_UYVY,
   },

   // #3 - YUY2 Small
   {
      .enabled = TRUE,
      .format = VMWARE_FOURCC_YUY2,
      .width = TESTCARD_WIDTH,
      .height = TESTCARD_HEIGHT,
      .srcX = 34,
      .srcY = 31,
      .srcWidth = 76,
      .srcHeight = 79,
      .dstX = 109,
      .dstY = 652,
      .dstWidth = 64,
      .dstHeight = 64,
      .pitches[0] = TESTCARD_WIDTH * 2,
      .dataOffset = OFFSET_YUY2,
   },

   // #4 - YV12 Small
   {
      .enabled = TRUE,
      .format = VMWARE_FOURCC_YV12,
      .width = TESTCARD_WIDTH,
      .height = TESTCARD_HEIGHT,
      .srcX = 34,
      .srcY = 31,
      .srcWidth = 76,
      .srcHeight = 79,
      .dstX = 564,
      .dstY = 303,
      .dstWidth = 64,
      .dstHeight = 64,
      .pitches[0] = TESTCARD_WIDTH,
      .pitches[1] = TESTCARD_WIDTH / 2,
      .pitches[2] = TESTCARD_WIDTH / 2,
      .dataOffset = OFFSET_YV12,
   },

   // #5 - UYVY Small
   {
      .enabled = TRUE,
      .format = VMWARE_FOURCC_UYVY,
      .width = TESTCARD_WIDTH,
      .height = TESTCARD_HEIGHT,
      .srcX = 34,
      .srcY = 31,
      .srcWidth = 76,
      .srcHeight = 79,
      .dstX = 564,
      .dstY = 652,
      .dstWidth = 64,
      .dstHeight = 64,
      .pitches[0] = TESTCARD_WIDTH * 2,
      .dataOffset = OFFSET_UYVY,
   },
};


/*
 * convertUYVYtoYUY2 --
 *
 *    Convert the test card image from UYVY format to YUY2.
 *    Both of these are packed-pixel formats, they just use
 *    different byte orders.
 */

static void
convertUYVYtoYUY2(uint8 *src,   // IN
                  uint8 *dest)  // OUT
{
   uint32 numWords = TESTCARD_WIDTH / 2 * TESTCARD_HEIGHT;

   while (numWords--) {
      uint8 u  = *(src++);
      uint8 y1 = *(src++);
      uint8 v  = *(src++);
      uint8 y2 = *(src++);

      *(dest++) = y1;
      *(dest++) = u;
      *(dest++) = y2;
      *(dest++) = v;
   }
}


/*
 * convertUYVYtoYV12 --
 *
 *    Convert the test card image from UYVY format (packed pixel) to
 *    YV12 (planar). This vertically decimates the chroma planes by
 *    1/2.
 */

static void
convertUYVYtoYV12(uint8 *src,   // IN
                  uint8 *dest)  // OUT
{
   /*
    * Y plane, full resolution.
    */

   uint8 *s = src;
   uint32 numWords = TESTCARD_WIDTH / 2 * TESTCARD_HEIGHT;
   while (numWords--) {
      s++;                 // U
      *(dest++) = *(s++);  // Y1
      s++;                 // V
      *(dest++) = *(s++);  // Y2
   }

   /*
    * U and V planes, at 1/2 height.
    */

   uint32 x, y;
   const uint32 pitch = TESTCARD_WIDTH * 2;
   uint8 *line1 = src;
   uint8 *v = dest;
   uint8 *u = v + (TESTCARD_WIDTH * TESTCARD_HEIGHT) / 4;

   for (y = TESTCARD_HEIGHT/2; y; y--) {
      uint8 *line2 = line1 + pitch;

      for (x = TESTCARD_WIDTH/2; x; x--) {

         uint8 u1 = *(line1)++;  // U
         line1++;                // Y1
         uint8 v1 = *(line1)++;  // V
         line1++;                // Y2

         uint8 u2 = *(line2)++;  // U
         line2++;                // Y1
         uint8 v2 = *(line2)++;  // V
         line2++;                // Y2

         *(u++) = ((int)u1 + (int)u2) >> 1;
         *(v++) = ((int)v1 + (int)v2) >> 1;
      }

      line1 = line2;
   }
}


/*
 * main --
 *
 *    Set up the virtual hardware, decompress the YUV images, and
 *    program the overlay units.
 */

int
main(void)
{
   PNGChunkIHDR *screenPNG = PNG_Header(screenPNGFile->ptr);
   uint32 width = bswap32(screenPNG->width);
   uint32 height = bswap32(screenPNG->height);
   uint32 streamId;

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
    * Decompress the YUY2 image, and use it to generate UYVY and YV12 versions.
    */

   DataFile_Decompress(testCardFile, gSVGA.fbMem + OFFSET_UYVY, 0x100000);
   convertUYVYtoYUY2(gSVGA.fbMem + OFFSET_UYVY, gSVGA.fbMem + OFFSET_YUY2);
   convertUYVYtoYV12(gSVGA.fbMem + OFFSET_UYVY, gSVGA.fbMem + OFFSET_YV12);

   /*
    * Program the overlay units
    */

   for (streamId = 0; streamId < arraysize(overlays); streamId++) {
      SVGA_VideoSetAllRegs(streamId, &overlays[streamId], SVGA_VIDEO_PITCH_3);
      SVGA_VideoFlush(streamId);
   }

   return 0;
}
