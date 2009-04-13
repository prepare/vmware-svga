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
 * svga3dtext.h --
 *
 *      Emulated text console, built on the SVGA3D protocol.  This
 *      module allows the use of VGAText in 3D mode. We convert the
 *      ROM BIOS font into a texture, and sample character data from
 *      the text-mode framebuffer. Text attributes are ignored.
 *
 *      This is used as a debug/diagnostic facility in the example
 *      programs, plus it's a simple but relatively efficient example
 *      of rendering using dynamic vertex buffer data.
 */

#ifndef __SVGA3DTEXT_H__
#define __SVGA3DTEXT_H__

#include "console.h"

void SVGA3DText_Init(void);
void SVGA3DText_Update(void);
void SVGA3DText_Draw(void);

#endif /* __SVGA3DTEXT_H__ */
