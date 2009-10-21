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

#ifndef __SCREENDRAW_H__
#define __SCREENDRAW_H__

#include "console.h"

void ScreenDraw_Init(uint32 gmrId);
void ScreenDraw_SetScreen(uint32 screenId, int width, int height);
void ScreenDraw_WrapText(char *text, int width);

void ScreenDraw_Rectangle(int left, int top, int right, int bottom,
                          uint32 color);
void ScreenDraw_Border(int left, int top, int right, int bottom,
                       uint32 color, uint32 width);
void ScreenDraw_Checkerboard(int left, int top, int right, int bottom);

#endif /* __SCREENDRAW_H__ */
