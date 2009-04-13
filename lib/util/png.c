/**********************************************************
 * Copyright 2008-2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

/*
 * png.c --
 *
 *      An extremely simple library for reading in-memory PNG images.
 */

#include "png.h"
#include "puff.h"


/*
 *----------------------------------------------------------------------
 *
 * PNG_Header --
 *
 *      Get a pointer to the header chunk of an in-memory PNG file.
 *
 * Results:
 *      Always returns a PNGChunkIHDR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

PNGChunkIHDR *
PNG_Header(void *pngData)  // IN
{
   return (PNGChunkIHDR*) ((uint8*)pngData + 8);
}


/*
 *----------------------------------------------------------------------
 *
 * PNG_NextChunk --
 *
 *      Given a pointer to one PNG chunk, return the next one.
 *
 * Results:
 *      Always returns a PNGChunk. Does not check for end-of-file.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

PNGChunk *
PNG_NextChunk(PNGChunk *lastChunk)  // IN
{
   return (PNGChunk*) (lastChunk->data + bswap32(lastChunk->length) + 4);
}


/*
 *----------------------------------------------------------------------
 *
 * PNGJoinIDAT --
 *
 *      Join all of the IDAT chunks together into a contiguous block
 *      of compressed data.
 *
 * Results:
 *      Returns a PNGChunk header which contains the full compressed
 *      data content of the PNG file.
 *
 * Side effects:
 *      Modifies the PNG file, by joining all IDAT chunks.
 *
 *----------------------------------------------------------------------
 */

PNGChunk *
PNGJoinIDAT(PNGChunk *firstChunk)  // IN
{
   PNGChunk *firstIDAT = NULL;
   PNGChunk *current = firstChunk;
   uint8 *tail = NULL;

   while (current->type != PNG_IEND) {
      PNGChunk *nextChunk = PNG_NextChunk(current);
      uint32 len = bswap32(current->length);

      if (current->type == PNG_IDAT) {
         if (firstIDAT == NULL) {
            /*
             * First IDAT chunk.
             */
            firstIDAT = current;
            tail = firstIDAT->data + len;

         } else {
            /*
             * Additional IDAT chunk. Append the data.
             */
            memcpy(tail, current->data, len);
            tail += len;
         }
      }

      current = nextChunk;
   }

   /*
    * Fix up the IDAT header.
    */
   firstIDAT->length = bswap32(tail - firstIDAT->data);

   /*
    * Write an IEND afterwards, so this process is repeatable.
    */
   current = PNG_NextChunk(firstIDAT);
   current->type = PNG_IEND;
   current->length = 0;

   return firstIDAT;
}


/*
 *----------------------------------------------------------------------
 *
 * PaethPredictor --
 *
 *      This is a simple image predictor which works based on the values
 *      of the pixels above, left, and to the top-left of the pixel we're
 *      predicting. It is defined in the PNG spec:
 *
 *      http://www.libpng.org/pub/png/spec/1.2/PNG-Filters.html
 *
 * Results:
 *      Returns the predicted byte.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static uint8
PaethPredictor(uint8 a,  // IN
               uint8 b,  // IN
               uint8 c)  // IN
{
   int p = a + b - c;
   int pa = p > a ? p - a : a - p;
   int pb = p > b ? p - b : b - p;
   int pc = p > c ? p - c : c - p;

   if (pa <= pb && pa <= pc) {
      return a;
   } else if (pb <= pc) {
      return b;
   }
   return c;
}


/*
 *----------------------------------------------------------------------
 *
 * PNG_DecompressBGRX --
 *
 *      This is a very simple PNG decompressor, which requires very
 *      little code and no separate temporary buffers.
 *
 *      Limitations:
 *
 *        - Modifies the input PNG file, in order to join all IDAT chunks.
 *
 *        - Only supports RGB (24-bit) PNG files.
 *
 *        - Always writes the output in 32-bit BGRX format.
 *
 *        - Does not support interlaced PNGs.
 *
 *        - Does not support filter types other than "None", "Sub", and "Up".
 *
 *        - Has no validation whatsoever. Unexpected or broken PNGs
 *          will just cause us to produce incorrect results or crash.
 *
 *        - Uses one extra line ('pitch' bytes) of memory in 'framebuffer'
 *          past the end of the decompressed image, for scratch space.
 *
 * Results:
 *      Writes the decompressed image to 'framebuffer'.
 *
 * Side effects:
 *      Uses the framebuffer as temporary space during decompression.
 *      Modifies the input PNG data in a non-reversible way.
 *
 *----------------------------------------------------------------------
 */

void
PNG_DecompressBGRX(PNGChunkIHDR *ihdr,   // IN
                   uint32 *framebuffer,  // OUT
                   uint32 pitch)         // OUT
{
   uint32 width = bswap32(ihdr->width);
   uint32 height = bswap32(ihdr->height);

   /*
    * Size of raw decompressed image: 3 bytes per pixel, plus one byte
    * (filter type) per scanline.
    */
   uint32 rawPitch = (width * 3) + 1;
   unsigned long rawSize = height * rawPitch;

   /*
    * Size of final decompressed image
    */
   uint32 finalSize = height * pitch;

   /*
    * Use the bottom of the framebuffer for temporary memory.  The
    * final raw-to-final conversion must read from higher addresses
    * and write to lower addresses, so we don't overwrite our
    * temporary buffer prematurely. To do this with all filter types,
    * we need to use one extra line of temporary space.
    */
   uint8 *rawBuffer = (uint8*)framebuffer + finalSize - rawSize + pitch;

   /*
    * Decompress all IDAT data into our raw buffer.  We need to join
    * all IDAT chunks to get a raw zlib data stream, then strip off
    * the 2-byte ZLIB header and 4-byte checksum to get a raw DEFLATE
    * stream.
    */
   PNGChunk *idat = PNGJoinIDAT(&ihdr->hdr);
   unsigned long compressedSize = bswap32(idat->length) - 6;
   puff(rawBuffer, &rawSize, idat->data + 2, &compressedSize);

   /*
    * Line-by-line, expand the decompressed filtered data into BGRX.
    */

   uint32 lines = height;
   Bool notFirstRow = FALSE;

   while (lines--) {
      uint8 *rawLine = rawBuffer;
      uint32 *fbLine = framebuffer;
      uint32 pixels = width;

      framebuffer = (uint32*) (pitch + (uint8*)framebuffer);
      rawBuffer += rawPitch;

      uint8 filterType = *(rawLine++);
      Bool notFirstColumn = FALSE;

      while (pixels--) {

         /*
          * Undo the per-scanline filtering.
          */

         uint8 *up = rawLine - rawPitch;
         uint8 *left = rawLine - 3;
         uint8 *upLeft = rawLine - 3 - rawPitch;
         uint32 i;

         for (i = 0; i < 3; i++) {
            switch (filterType) {

            case 0:   // None
               break;

            case 1:   // Sub
               if (notFirstColumn) {
                  rawLine[i] += left[i];
               }
               break;

            case 2:   // Up
               if (notFirstRow) {
                  rawLine[i] += up[i];
               }
               break;

            case 3:   // Average
               rawLine[i] += ((notFirstColumn ? left[i] : 0) +
                              (notFirstRow ? up[i] : 0)) >> 1;
               break;

            case 4:   // Paeth
               rawLine[i] += PaethPredictor(notFirstColumn ? left[i] : 0,
                                            notFirstRow ? up[i] : 0,
                                            (notFirstRow && notFirstColumn)
                                               ? upLeft[i] : 0);
               break;
            }
         }

         /*
          * Decode RGB to BGRX.
          */

         uint8 r = rawLine[0];
         uint8 g = rawLine[1];
         uint8 b = rawLine[2];

         *(fbLine++) = (r << 16) | (g << 8) | b;

         rawLine += 3;
         notFirstColumn = TRUE;
      }
      notFirstRow = TRUE;
   }
}
