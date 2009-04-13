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
 * svga3dutil.h --
 *
 *      Higher-level convenience functions built on top of the SVGA3D
 *      FIFO command layer.
 */

#ifndef __SVGA3DUTIL_H__
#define __SVGA3DUTIL_H__

#include "svga3d.h"
#include "vmbackdoor.h"
#include "datafile.h"

/*
 * Default cid for single-context example programs.  This constant
 * isn't used by svga3dutil itself, but it's provided as a convenience
 * for example programs that don't need multiple contexts.
 */

#define CID                  1

#define MAX_ASYNC_CALLS      128
#define MAX_DMA_POOL_BUFFERS 128

typedef struct DMAPool DMAPool;

typedef struct DMAPoolBuffer {
   DMAPool *pool;
   struct DMAPoolBuffer *next;
   void *buffer;
   SVGAGuestPtr ptr;
} DMAPoolBuffer;

struct DMAPool {
   uint32 bufferSize;
   uint32 numBuffers;
   DMAPoolBuffer *freeList;
   DMAPoolBuffer buffers[MAX_DMA_POOL_BUFFERS];
};

typedef struct FPSCounterState {
   VMTime  now;
   uint32  frame;
   Bool    initialized;

   char    text[16];
   int     hundredths;

   struct {
      VMTime time;
      uint32 frame;
   } lastUpdate;
} FPSCounterState;

/*
 * Global data used by the "Fullscreen" utility functions.  These
 * utilities make extra assumptions: We're rendering from a single
 * color buffer, and we're using the SVGA device's full resolution.
 */

typedef struct FullscreenState {
   SVGA3dSurfaceImageId colorImage;
   SVGA3dSurfaceImageId depthImage;
   uint32 lastPresentFence;
   SVGA3dRect screen;
} FullscreenState;

extern FullscreenState gFullscreen;

typedef void (*AsyncCallFn)(void *);


/*
 * Device-level Functionality
 */

void SVGA3DUtil_InitFullscreen(uint32 cid, uint32 width, uint32 height);
void SVGA3DUtil_PresentFullscreen(void);
void SVGA3DUtil_AsyncCall(AsyncCallFn handler, void *arg);
Bool SVGA3DUtil_UpdateFPSCounter(FPSCounterState *self);

/*
 * Surface Management
 */

uint32 SVGA3DUtil_AllocSurfaceID(void);
void *SVGA3DUtil_AllocDMABuffer(uint32 size, SVGAGuestPtr *ptr);

uint32 SVGA3DUtil_DefineSurface2D(uint32 width, uint32 height,
                                  SVGA3dSurfaceFormat format);
void SVGA3DUtil_SurfaceDMA2D(uint32 sid, SVGAGuestPtr *guestPtr,
                             SVGA3dTransferType transfer, uint32 width, uint32 height);

uint32 SVGA3DUtil_DefineStaticBuffer(const void *data, uint32 size);
uint32 SVGA3DUtil_LoadCompressedBuffer(const DataFile *file, uint32 *pSize);

void SVGA3DUtil_AllocDMAPool(DMAPool *self, uint32 bufferSize, uint32 numBuffers);
DMAPoolBuffer *SVGA3DUtil_DMAPoolGetBuffer(DMAPool *self);
void SVGA3DUtil_DMAPoolFreeBuffer(DMAPoolBuffer *buffer);

/*
 * Shaders
 */

void SVGA3DUtil_SetShaderConstMatrix(uint32 cid, uint32 reg,
                                     SVGA3dShaderType type, const float *matrix);

/*
 * Drawing Operations
 */

void SVGA3DUtil_ClearFullscreen(uint32 cid, SVGA3dClearFlag flags,
                                uint32 color, float depth, uint32 stencil);

#endif /* __SVGA3DUTIL_H__ */
