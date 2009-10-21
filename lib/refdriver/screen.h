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
 * screen.h --
 *
 *      Utilities for creating, destroying, and blitting SVGA Screen Objects.
 */

#ifndef __SCREEN_H__
#define __SCREEN_H__

#include "svga_reg.h"


/*
 * Verify that we support SVGA Screen Objects. Panic if screen objects are not supported.
 */

void Screen_Init(void);

/*
 * Creating/destroying screens
 */

void Screen_Define(const SVGAScreenObject *screen);
void Screen_Destroy(uint32 id);

/*
 * Set the current GMRFB state. This is a guest-memory image which can
 * be used as the source or destination for blits. This is a very
 * light-weight operation, so it's perfectly fine to set the GMRFB
 * state before every blit if that's necessary.
 */

void Screen_DefineGMRFB(SVGAGuestPtr ptr, uint32 bytesPerLine,
                        SVGAGMRImageFormat format);

/*
 * Blits to and from screens. The screen ID can be specified as
 * SVGA_ID_INVALID to indicate that the coordinates are relative to
 * the virtual coordinate space origin rather than the screen origin.
 */

void Screen_BlitFromGMRFB(const SVGASignedPoint *srcOrigin,
                          const SVGASignedRect *destRect,
                          uint32 destScreen);
void Screen_BlitToGMRFB(const SVGASignedPoint *destOrigin,
                        const SVGASignedRect *srcRect,
                        uint32 srcScreen);

/*
 * Blit annotations. These can be issued prior to a
 * Screen_BlitFromGMRFB in order to give the SVGA device an extra
 * guarantee about the content of the blit.
 */

void Screen_AnnotateFill(SVGAColorBGRX color);
void Screen_AnnotateCopy(const SVGASignedPoint *srcOrigin, uint32 srcScreen);


#endif /* __SCREEN_H__ */
