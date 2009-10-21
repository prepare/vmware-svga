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
 * screendraw.h --
 *
 *      ScreenDraw is a small utility library for drawing
 *      into SVGA Screen Objects. It supports fills and text.
 *
 *      It uses no system memory framebuffer at all- instead,
 *      we have a small GMRFB which contains our font glyphs
 *      and a tile buffer for blits.
 *
 *      For text rendering, we support the Metalkit Console API.
 */

#include "types.h"
#include "svga_reg.h"
#include "datafile.h"
#include "gmr.h"
#include "svga.h"
#include "screen.h"
#include "screendraw.h"
#include "console_vga.h"

DECLARE_DATAFILE(fontData, ______lib_util_bitstream_vera_15_font_z);

#define TILE_SIZE            64
#define TILE_BUFFER_PIXELS   (TILE_SIZE * TILE_SIZE)
#define TILE_BUFFER_BYTES    (TILE_BUFFER_PIXELS * sizeof(uint32))

#define MAX_FONT_SIZE        200000

#define BACKGROUND_COLOR     0x000000   // This should match our font's color scheme
#define MARGIN_SIZE          10         // Blank margin around the screen edge

/*
 * Our fontData begins with an array of CharMetrics, and is followed
 * by raw image data for each glyph.
 */
typedef struct {
   uint8 width;
   uint8 height;
   uint8 reserved[2];
   uint32 offset;
} CharMetrics;

struct {
   /* Dynamic state */
   SVGASignedPoint  position;
   uint32           screenId;
   uint32           screenWidth;
   uint32           screenHeight;

   /* Allocated resources */
   SVGAGuestPtr     tilePtr;
   uint32           *tileBuffer;
   SVGAGuestPtr     fontPtr;
   CharMetrics      *metrics;

   /* Tile buffer synchronization */
   uint32           tileFence;
   struct {
      enum {
         TILE_OTHER,
         TILE_FILL,
         TILE_CHECKERBOARD,
      } type;
      uint32        color;
   } tileUsage;

} gScreenDraw;

/*
 * Console API
 */
static fastcall void ScreenDrawBeginPanic(void);
static fastcall void ScreenDrawClear(void);
static fastcall void ScreenDrawMoveTo(int x, int y);
static fastcall void ScreenDrawWriteChar(char c);
static fastcall void ScreenDrawFlush(void);


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDraw_Init --
 *
 *    Initializes the ScreenDraw module. Requires that the GMR and
 *    Heap modules are already initialized.
 *
 *    This allocates memory, allocates a GMR, decompresses our font,
 *    and sets the current Metalkit console to our text renderer.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Allocates memory from the Heap.
 *    Defines a private GMR (gmrId).
 *    Decompresses the font.
 *
 *-----------------------------------------------------------------------------
 */

void
ScreenDraw_Init(uint32 gmrId)
{
   const uint32 gmrSize = TILE_BUFFER_BYTES + MAX_FONT_SIZE;
   PPN gmrPages = GMR_DefineContiguous(gmrId, (gmrSize + PAGE_MASK) / PAGE_SIZE);

   gScreenDraw.tilePtr.gmrId = gmrId;
   gScreenDraw.tilePtr.offset = 0;
   gScreenDraw.tileBuffer = (uint32*)PPN_POINTER(gmrPages);

   gScreenDraw.fontPtr.gmrId = gmrId;
   gScreenDraw.fontPtr.offset = TILE_BUFFER_BYTES;
   gScreenDraw.metrics = (CharMetrics*) (TILE_BUFFER_BYTES +
                                         (uint8*)PPN_POINTER(gmrPages));

   DataFile_Decompress(fontData, (void*)gScreenDraw.metrics, MAX_FONT_SIZE);

   gConsole.beginPanic = ScreenDrawBeginPanic;
   gConsole.clear = ScreenDrawClear;
   gConsole.moveTo = ScreenDrawMoveTo;
   gConsole.writeChar = ScreenDrawWriteChar;
   gConsole.flush = ScreenDrawFlush;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDraw_SetScreen --
 *
 *    Define the ID and size of the screen we're drawing to.
 *
 *    The size affects the dimensions of the rectangle we blit
 *    when clearing the screen, and text will wrap at the right
 *    edge.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

void
ScreenDraw_SetScreen(uint32 screenId, int width, int height)
{
   gScreenDraw.screenId = screenId;
   gScreenDraw.screenWidth = width;
   gScreenDraw.screenHeight = height;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDrawTiledRectangle --
 *
 *    Internal function which blits multiple copies of the tile buffer
 *    to the screen in order to fill a rectangle.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Updates the tile buffer fence.
 *
 *-----------------------------------------------------------------------------
 */

static void
ScreenDrawTiledRectangle(int left,
                         int top,
                         int right,
                         int bottom)
{
   static const SVGAGMRImageFormat format = {{{ 32, 24 }}};
   Screen_DefineGMRFB(gScreenDraw.tilePtr, TILE_SIZE * sizeof(uint32), format);

   static const SVGASignedPoint srcOrigin = { 0, 0 };
   SVGASignedRect destRect;

   for (destRect.top = top; destRect.top < bottom; destRect.top += TILE_SIZE) {
      destRect.bottom = MIN(bottom, destRect.top + TILE_SIZE);

      for (destRect.left = left; destRect.left < right; destRect.left += TILE_SIZE) {
         destRect.right = MIN(right, destRect.left + TILE_SIZE);

         /*
          * If this is a fill, annotate each blit.
          */
         if (gScreenDraw.tileUsage.type == TILE_FILL) {
            SVGAColorBGRX color;
            color.value = gScreenDraw.tileUsage.color;
            Screen_AnnotateFill(color);
         }

         Screen_BlitFromGMRFB(&srcOrigin, &destRect, gScreenDraw.screenId);
      }
   }

   gScreenDraw.tileFence = SVGA_InsertFence();
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDraw_Rectangle --
 *
 *    Draw a solid-color rectangle to the screen. The color
 *    is specified in hex RGB (0xRRGGBB) format.
 *
 *    This implementation is only moderately efficient, and is
 *    provided mostly for simplicity and low memory usage.  We have a
 *    single tile-sized DMA buffer. That buffer is cleared with the
 *    fill color, then we splat it onto the screen as many times as it
 *    takes to cover the rectangle.
 *
 *    There are several ways this could be improved:
 *
 *      - For small fills, we don't need to memset the entire tile buffer.
 *
 *      - We could use multiple DMA buffers, so we'd sync less often.
 *
 *      - If we really cared about framebuffer-less fill performance,
 *        we'd have an accelerated command for this- but we actually
 *        don't care that much.
 *
 *      - If we wanted to cheat a bit, we could use a GMRFB
 *        bytesPerLine of zero. That should work in theory, but I
 *        don't think the SVGA device can guarantee that it will
 *        always work.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    May modify the tile buffer. If the tile buffer is busy,
 *    we will sync the FIFO. Modifies the GMRFB state.
 *
 *-----------------------------------------------------------------------------
 */

void
ScreenDraw_Rectangle(int left,
                     int top,
                     int right,
                     int bottom,
                     uint32 color)
{
   if (gScreenDraw.tileUsage.type != TILE_FILL || gScreenDraw.tileUsage.color != color) {
      SVGA_SyncToFence(gScreenDraw.tileFence);
      gScreenDraw.tileUsage.type = TILE_FILL;
      gScreenDraw.tileUsage.color = color;
      memset32(gScreenDraw.tileBuffer, color, TILE_BUFFER_PIXELS);
   }

   ScreenDrawTiledRectangle(left, top, right, bottom);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDraw_Checkerboard --
 *
 *    Draw a gray checkerboard pattern within the specified rectangle.
 *
 *    This is very similar to the implementation of
 *    ScreenDraw_Rectangle(), except that we fill the tile buffer with
 *    a checkerboard pattern instead of a solid color.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    May modify the tile buffer. If the tile buffer is busy,
 *    we will sync the FIFO. Modifies the GMRFB state.
 *
 *-----------------------------------------------------------------------------
 */

void
ScreenDraw_Checkerboard(int left,
                        int top,
                        int right,
                        int bottom)
{
   if (gScreenDraw.tileUsage.type != TILE_CHECKERBOARD) {
      const uint32 checkerSize = 8;
      const uint32 color1 = 0x666677;
      const uint32 color2 = 0x9999aa;

      SVGA_SyncToFence(gScreenDraw.tileFence);
      gScreenDraw.tileUsage.type = TILE_CHECKERBOARD;

      uint32 *pixel = gScreenDraw.tileBuffer;
      uint32 x, y;

      for (y = 0; y < TILE_SIZE; y++) {
         for (x = 0; x < TILE_SIZE; x++) {
            *(pixel++) = (x ^ y) & checkerSize ? color1 : color2;
         }
      }
   }

   ScreenDrawTiledRectangle(left, top, right, bottom);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDraw_Border --
 *
 *    Draw a border along the interior edge of a rectangle.
 *
 *    If 'width' is 1, we fill the pixels along the 'left' and 'top'
 *    edge, and the pixels just left/above the right and bottom
 *    edges. This corresponds to the outermost pixels that would have
 *    been written by ScreenDraw_Rectangle().
 *
 *    If 'width' is greater than 1, we will cover additional pixels
 *    within these border pixels.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    May modify the tile buffer. If the tile buffer is busy,
 *    we will sync the FIFO. Modifies the GMRFB state.
 *
 *-----------------------------------------------------------------------------
 */

void
ScreenDraw_Border(int left,
                  int top,
                  int right,
                  int bottom,
                  uint32 color,
                  uint32 width)
{
   ScreenDraw_Rectangle(left, top, right, top + width, color);
   ScreenDraw_Rectangle(left, top, left + width, bottom, color);
   ScreenDraw_Rectangle(right - width, top, right, bottom, color);
   ScreenDraw_Rectangle(left, bottom - width, right, bottom, color);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDrawBeginPanic --
 *
 *    Prepare for a Console_Panic(). This switches back to the VGA
 *    console, and asks it to prepare instead.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Switches consoles.
 *
 *-----------------------------------------------------------------------------
 */

fastcall void
ScreenDrawBeginPanic(void)
{
   ConsoleVGA_Init();
   gConsole.beginPanic();
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDrawClear --
 *
 *    Reset the cursor location, and clear the screen.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

fastcall void
ScreenDrawClear(void)
{
   ScreenDrawMoveTo(MARGIN_SIZE, MARGIN_SIZE);
   ScreenDraw_Rectangle(0, 0, gScreenDraw.screenWidth, gScreenDraw.screenHeight,
                        BACKGROUND_COLOR);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDrawMoveTo --
 *
 *    Set the cursor location, in pixels.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

fastcall void
ScreenDrawMoveTo(int x, int y)
{
   gScreenDraw.position.x = x;
   gScreenDraw.position.y = y;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDrawWriteChar --
 *
 *    Write one visible glyph to the screen, and adjust the glyph position.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    May redefine the current GMRFB.
 *
 *-----------------------------------------------------------------------------
 */

fastcall void
ScreenDrawWriteChar(char c)
{
   CharMetrics *cm = &gScreenDraw.metrics[(uint8)c];

   if (c == '\n') {
      gScreenDraw.position.x = MARGIN_SIZE;
      gScreenDraw.position.y += gScreenDraw.metrics[(uint8)' '].height;
      return;
   }

   if (!cm->height) {
      /* Character not present in this font */
      return;
   }

   SVGAGuestPtr gPtr = gScreenDraw.fontPtr;
   gPtr.offset += cm->offset;

   static const SVGAGMRImageFormat format = {{{ 32, 24 }}};

   Screen_DefineGMRFB(gPtr, sizeof(uint32) * cm->width, format);

   SVGASignedPoint blitOrigin = { 0, 0 };
   SVGASignedRect blitDest = {
      gScreenDraw.position.x,
      gScreenDraw.position.y,
      gScreenDraw.position.x + cm->width,
      gScreenDraw.position.y + cm->height,
   };

   Screen_BlitFromGMRFB(&blitOrigin, &blitDest, gScreenDraw.screenId);

   gScreenDraw.position.x += cm->width;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDraw_WrapText --
 *
 *    This is a utility function for word-wrapping text at runtime. We wrap
 *    a provided string in-place, by replacing spaces with newlines.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

void
ScreenDraw_WrapText(char *text,  // IN/OUT
                    int width)   // IN
{
   char *word = text;
   int x = 0;

   while (*word) {

      if (*word == '\n') {
         x = 0;
      }

      if (*word == ' ' && x > 0) {
         /*
          * We're pointing just prior to a word that we can wrap. Should we?
          */

         char *p = word + 1;
         int wordWidth = 0;

         while (*p != ' ' && *p != '\0' && *p != '\n') {
            wordWidth += gScreenDraw.metrics[(uint8) *p].width;
            p++;
         }

         if (x + wordWidth > width) {
            x = 0;
            *word = '\n';
         }
      }

      word++;
      x += gScreenDraw.metrics[(uint8) *word].width;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * ScreenDrawFlush --
 *
 *    No-op. We don't need to flush after writing text.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

fastcall void
ScreenDrawFlush(void)
{
   /* No-op */
}
