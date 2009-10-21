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
 * screen.c --
 *
 *      Utilities for creating, destroying, and blitting SVGA Screen Objects.
 */

#include "svga.h"
#include "svga3d.h"
#include "screen.h"


/*
 *-----------------------------------------------------------------------------
 *
 * Screen_Init --
 *
 *    Verify that we support SVGA Screen Objects. Panic if screen
 *    objects are not supported.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Might panic.
 *
 *-----------------------------------------------------------------------------
 */

void
Screen_Init(void)
{
   if (!SVGA_HasFIFOCap(SVGA_FIFO_CAP_SCREEN_OBJECT)) {
      SVGA_Panic("Virtual device does not have Screen Object support.");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Screen_Define --
 *
 *    Create or modify an SVGA Screen Object.
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
Screen_Define(const SVGAScreenObject *screen)  // IN
{
   SVGAFifoCmdDefineScreen *cmd = SVGA_FIFOReserveCmd(SVGA_CMD_DEFINE_SCREEN,
                                                      screen->structSize);
   memcpy(cmd, screen, screen->structSize);
   SVGA_FIFOCommitAll();
}


/*
 *-----------------------------------------------------------------------------
 *
 * Screen_Destroy --
 *
 *    Delete an SVGA Screen Object.
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
Screen_Destroy(uint32 id)
{
   SVGAFifoCmdDestroyScreen *cmd = SVGA_FIFOReserveCmd(SVGA_CMD_DESTROY_SCREEN,
                                                       sizeof *cmd);
   cmd->screenId = id;
   SVGA_FIFOCommitAll();
}


/*
 *-----------------------------------------------------------------------------
 *
 * Screen_DefineGMRFB --
 *
 *    Set the current GMRFB state. This is a guest-memory image which
 *    can be used as the source or destination for blits. This is a
 *    very light-weight operation, so it's perfectly fine to set the
 *    GMRFB state before every blit if that's necessary.
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
Screen_DefineGMRFB(SVGAGuestPtr ptr,           // IN
                   uint32 bytesPerLine,        // IN
                   SVGAGMRImageFormat format)  // IN
{
   SVGAFifoCmdDefineGMRFB *cmd = SVGA_FIFOReserveCmd(SVGA_CMD_DEFINE_GMRFB, sizeof *cmd);
   cmd->ptr = ptr;
   cmd->bytesPerLine = bytesPerLine;
   cmd->format = format;
   SVGA_FIFOCommitAll();
}


/*
 *-----------------------------------------------------------------------------
 *
 * Screen_BlitFromGMRFB --
 *
 *    This is a rectangular DMA operation which copies pixels from
 *    a GMR region (defined in the current GMRFB) to zero or more Screens.
 *
 *    The blit is performed asynchronously, in order with other FIFO
 *    commands. When the FIFO command is complete, the SVGA device is
 *    guaranteed to be done reading from the GMRFB.
 *
 *    The destination rectangle can be specified relative to a screen
 *    origin, or relative to the virtual coordinate space's origin (if
 *    the screen ID is SVGA_ID_INVALID).
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
Screen_BlitFromGMRFB(const SVGASignedPoint *srcOrigin,  // IN
                     const SVGASignedRect *destRect,    // IN
                     uint32 destScreen)                 // IN
{
   SVGAFifoCmdBlitGMRFBToScreen *cmd = SVGA_FIFOReserveCmd(SVGA_CMD_BLIT_GMRFB_TO_SCREEN,
                                                           sizeof *cmd);
   cmd->srcOrigin = *srcOrigin;
   cmd->destRect = *destRect;
   cmd->destScreenId = destScreen;
   SVGA_FIFOCommitAll();
}


/*
 *-----------------------------------------------------------------------------
 *
 * Screen_BlitToGMRFB --
 *
 *    This is a rectangular DMA operation which copies pixels from
 *    zero or more screens back to a GMR region (defined in the current GMRFB).
 *
 *    This command can be used to read back the contents of the
 *    device's front buffer. It includes all 2D and 3D rendering, but
 *    it does not include video or cursor overlays.
 *
 *    The blit is performed asynchronously, in order with other FIFO
 *    commands. When the FIFO command is complete, the SVGA device is
 *    guaranteed to be done writing to the GMRFB.
 *
 *    The source rectangle can be specified relative to a screen
 *    origin, or relative to the virtual coordinate space's origin (if
 *    the screen ID is SVGA_ID_INVALID).
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
Screen_BlitToGMRFB(const SVGASignedPoint *destOrigin,  // IN
                   const SVGASignedRect *srcRect,      // IN
                   uint32 srcScreen)                   // IN
{
   SVGAFifoCmdBlitScreenToGMRFB *cmd = SVGA_FIFOReserveCmd(SVGA_CMD_BLIT_SCREEN_TO_GMRFB,
                                                           sizeof *cmd);
   cmd->destOrigin = *destOrigin;
   cmd->srcRect = *srcRect;
   cmd->srcScreenId = srcScreen;
   SVGA_FIFOCommitAll();
}


/*
 *-----------------------------------------------------------------------------
 *
 * Screen_AnnotateFill --
 *
 *    Store an annotation for the next blit-to-Screen operation.
 *
 *    This annotation is a promise about the contents of the next
 *    blit: The video driver is guaranteeing that all pixels in that
 *    blit will have the same value, specified here as a color in BGRX
 *    (0xRRGGBB) format.
 *
 *    The SVGA device can still render the blit correctly even if it
 *    ignores this annotation, but the annotation may allow it to
 *    perform the blit more efficiently, for example by ignoring the
 *    source data and performing a fill in hardware.
 *
 *    This annotation is most important for performance when the
 *    user's display is being remoted over a network connection.
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
Screen_AnnotateFill(SVGAColorBGRX color)  // IN
{
   SVGAFifoCmdAnnotationFill *cmd = SVGA_FIFOReserveCmd(SVGA_CMD_ANNOTATION_FILL,
                                                        sizeof *cmd);
   cmd->color = color;
   SVGA_FIFOCommitAll();
}


/*
 *-----------------------------------------------------------------------------
 *
 * Screen_AnnotateCopy --
 *
 *    Store an annotation for the next blit-to-Screen operation.
 *
 *    This annotation is a promise about the contents of the next
 *    blit: The video driver is guaranteeing that all pixels in that
 *    blit will have the same value as those which already exist at an
 *    identically-sized region on the same or a different screen.
 *
 *    Note that the source pixels for the COPY in this annotation are
 *    sampled before applying the anqnotation's associated blit. They
 *    are allowed to overlap with the blit's destination pixels.
 *
 *    The copy source rectangle is specified the same way as the blit
 *    destination: it can be a rectangle which spans zero or more
 *    screens, specified relative to either a screen or to the virtual
 *    coordinate system's origin. If the source rectangle includes
 *    pixels which are not from exactly one screen, the results are
 *    undefined.
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
Screen_AnnotateCopy(const SVGASignedPoint *srcOrigin,  // IN
                    uint32 srcScreen)                  // IN
{
   SVGAFifoCmdAnnotationCopy *cmd = SVGA_FIFOReserveCmd(SVGA_CMD_ANNOTATION_COPY,
                                                        sizeof *cmd);
   cmd->srcOrigin = *srcOrigin;
   cmd->srcScreenId = srcScreen;
   SVGA_FIFOCommitAll();
}
