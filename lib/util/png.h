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
 * png.h --
 *
 *      An extremely simple library for reading in-memory PNG images.
 *
 *      This library has no validation at all, so it must be used only
 *      with images from trusted sources.
 */

#ifndef __PNG_H__
#define __PNG_H__

#include "types.h"


/*
 * PNG fields are in Big Endian byte order.  Define a new data type
 * for Big Endian numbers, and an inline function to do 32-bit byte
 * swapping.
 */

typedef uint32 be32;

static inline uint32
bswap32(uint32 x)
{
   __asm__ ("bswap %0" : "+r" (x));
   return x;
}


/*
 * PNG data types
 */

typedef struct {
   be32    length;
   uint32  type;
   uint8   data[0];
} PNGChunk;

typedef struct {
   PNGChunk hdr;
   be32     width;
   be32     height;
   uint8    bitDepth;
   uint8    colorType;
   uint8    compression;
   uint8    filter;
   uint8    interlace;
} PNGChunkIHDR;

#define PNG_CHUNK(a,b,c,d)  ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))
#define PNG_IHDR            PNG_CHUNK('I','H','D','R')
#define PNG_IDAT            PNG_CHUNK('I','D','A','T')
#define PNG_IEND            PNG_CHUNK('I','E','N','D')


/*
 * Public functions
 */

PNGChunkIHDR *PNG_Header(void *pngData);
PNGChunk *PNG_NextChunk(PNGChunk *lastChunk);
void PNG_DecompressBGRX(PNGChunkIHDR *ihdr, uint32 *framebuffer, uint32 pitch);


#endif /* __PNG_H_ */
