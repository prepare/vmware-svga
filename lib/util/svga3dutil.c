/**********************************************************
 * Copyright 2008-2010 VMware, Inc.  All rights reserved.
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
 * svga3dutil.c --
 *
 *      Higher-level convenience functions built on top of the SVGA3D
 *      FIFO command layer.
 */

#include "svga3dutil.h"
#include "intr.h"

FullscreenState gFullscreen;


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_InitFullscreen --
 *
 *      This is a "big hammer" for initializing the SVGA device in
 *      our simple 3D example programs. It does the following:
 *
 *       - Initializes interrupts
 *       - Initializes the SVGA adapter
 *       - Switches video modes
 *       - Initializes the absolute mouse
 *       - Initializes the 3D subsystem
 *       - Creates a color buffer and depth buffer
 *       - Creates a context
 *       - Attaches the color/depth buffers to the context
 *       - Sets the viewport
 *       - Sets default render states
 *
 * Results:
 *      void.
 *
 * Side effects:
 *      Panic if anything fails.
 *      Stores the fullscreen color buffer surface ID.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DUtil_InitFullscreen(uint32 cid,     // IN
                          uint32 width,   // IN
                          uint32 height)  // IN
{
   SVGA3dRenderState *rs;

   gFullscreen.screen.x = 0;
   gFullscreen.screen.y = 0;
   gFullscreen.screen.w = width;
   gFullscreen.screen.h = height;

   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);

   SVGA_Init();
   SVGA_SetMode(width, height, 32);
   VMBackdoor_MouseInit(TRUE);
   SVGA3D_Init();

   gFullscreen.colorImage.sid = SVGA3DUtil_DefineSurface2D(width, height,
                                                           SVGA3D_X8R8G8B8);

   gFullscreen.depthImage.sid = SVGA3DUtil_DefineSurface2D(width, height,
                                                           SVGA3D_Z_D16);

   SVGA3D_DefineContext(cid);

   SVGA3D_SetRenderTarget(cid, SVGA3D_RT_COLOR0, &gFullscreen.colorImage);
   SVGA3D_SetRenderTarget(cid, SVGA3D_RT_DEPTH, &gFullscreen.depthImage);

   SVGA3D_SetViewport(cid, &gFullscreen.screen);
   SVGA3D_SetZRange(cid, 0.0f, 1.0f);

   /*
    * The device defaults to flat shading, but to retain compatibility
    * across OpenGL and Direct3D it may be much slower in this
    * mode. Usually we don't want flat shading, so go ahead and switch
    * into smooth shading mode.
    *
    * Note that this is a per-context render state.
    *
    * XXX: There is also a bug in VMware Workstation 6.5.2 which shows
    *      up if you're in flat shading mode and you're using a drawing
    *      command which does not include an SVGA3dVertexDivisor array.
    *      Avoiding flat shading is one workaround, another is to include
    *      a dummy SVGA3dVertexDivisor array on every draw.
    */

   SVGA3D_BeginSetRenderState(cid, &rs, 1);
   {
      rs[0].state     = SVGA3D_RS_SHADEMODE;
      rs[0].uintValue = SVGA3D_SHADEMODE_SMOOTH;
   }
   SVGA_FIFOCommitAll();
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_PresentFullscreen --
 *
 *      This is a simplified version of SVGA3D_BeginPresent(), which
 *      assumes that we're running in full-screen mode and that the
 *      whole screen needs updating.
 *
 *      It also includes the recommended flow control, to prevent
 *      the SVGA3D device from lagging too far behind the driver-
 *      we use FIFO fences to ensure that only one Present command
 *      is in the FIFO at once.
 *
 *      This is much better than performing a full Sync after each
 *      Present, since it allows the guest to be preparing frame N+1
 *      while the host is still rendering frame N.
 *
 *      Note that this simplified logic needs to be modified if the
 *      application/driver is presenting from multiple
 *      surfaces. Ideally, we actaully want only a single frame per
 *      surface in the FIFO at once.  It is recommended that drivers
 *      store lastPresentFence per-surface.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      SVGA_SyncToFence().
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DUtil_PresentFullscreen(void)
{
   SVGA3dCopyRect *cr;

   SVGA_SyncToFence(gFullscreen.lastPresentFence);

   SVGA3D_BeginPresent(gFullscreen.colorImage.sid, &cr, 1);
   memset(cr, 0, sizeof *cr);
   cr->w = gSVGA.width;
   cr->h = gSVGA.height;
   SVGA_FIFOCommitAll();

   gFullscreen.lastPresentFence = SVGA_InsertFence();
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_AsyncCall --
 *
 *      This is a simple asynchronous call mechanism, which is used to
 *      invoke a specified function once the SVGA3D device's FIFO
 *      processing has reached the current point in the command
 *      stream. It can be used, for example, to asynchronously garbage
 *      collect DMA buffers or asynchronously handle downloads from
 *      host VRAM.
 *
 *      This single function both dispatches previous calls and
 *      optionall enqueues a new call.
 *
 *      A fixed number of asynchronous calls may be in flight at any
 *      given time. If MAX_ASYNC_CALLS calls are pending, we wait for
 *      the oldest one to finish.
 *
 *      You can call this function with handler==NULL to just flush
 *      any existing async calls which have completed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      SVGA_SyncToFence(), any side-effects caused by other async
 *      call handlers. If we're enqueueing a handler, this inserts
 *      a FIFO fence.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DUtil_AsyncCall(AsyncCallFn handler,  // IN (optional)
                     void *arg)            // IN (optional)
{
   static struct {
      uint32 head, tail, count;
      struct {
         AsyncCallFn  handler;
         void        *arg;
         uint32       fence;
      } calls[MAX_ASYNC_CALLS];
   } queue;

   if (queue.count > MAX_ASYNC_CALLS ||
       queue.head >= MAX_ASYNC_CALLS ||
       queue.tail >= MAX_ASYNC_CALLS) {
      SVGA_Panic("Async call queue corrupted");
   }

   if (queue.count == MAX_ASYNC_CALLS) {
      SVGA_SyncToFence(queue.calls[queue.tail].fence);
   }

   while (queue.count && SVGA_HasFencePassed(queue.calls[queue.tail].fence)) {
      queue.calls[queue.tail].handler(queue.calls[queue.tail].arg);
      queue.tail = (queue.tail + 1) % MAX_ASYNC_CALLS;
      queue.count--;
   }

   if (handler) {
      queue.calls[queue.head].handler = handler;
      queue.calls[queue.head].arg = arg;
      queue.calls[queue.head].fence = SVGA_InsertFence();
      queue.head = (queue.head + 1) % MAX_ASYNC_CALLS;
      queue.count++;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_UpdateFPSCounter --
 *
 *      This is a simple self-contained frame and frame rate counter.
 *
 *      Initialize an FPSCounterState structure to zero (or allocate
 *      it in the BSS segment) and call this function once per frame.
 *      Any time it returns TRUE, the caller should update the
 *      on-screen display with a new frame rate.
 *
 *      Guaranteed to return TRUE on the first call.
 *
 * Results:
 *      TRUE if the frame rate needs updating.
 *
 * Side effects:
 *      Occasionally requests the current host wallclock time via the
 *      VMware backdoor port.
 *
 *----------------------------------------------------------------------
 */

static char
FPSDigit(uint32 n, uint32 divisor)
{
   n /= divisor;
   if (n == 0) {
      return ' ';
   } else {
      return (n % 10) + '0';
   }
}

Bool
SVGA3DUtil_UpdateFPSCounter(FPSCounterState *self)  // IN/OUT
{
   uint32 msecDiff;
   uint32 frameDiff;

   if (!self->initialized) {
      VMBackdoor_GetTime(&self->now);
      self->lastUpdate.time = self->now;
      self->initialized = TRUE;
      return TRUE;
   }

   self->frame++;

   /*
    * Only check the time every several frames, to avoid the small
    * overhead of making backdoor calls on every frame.
    */
   if (self->frame & 0x0F) {
      return FALSE;
   }

   VMBackdoor_GetTime(&self->now);

   msecDiff = VMBackdoor_TimeDiffUS(&self->lastUpdate.time, &self->now) / 1000;

   if (msecDiff < 500) {
      /* Too soon since the last update */
      return FALSE;
   }

   frameDiff = self->frame - self->lastUpdate.frame;
   self->hundredths = (frameDiff * (1000 * 100)) / msecDiff;

   self->text[0] = FPSDigit(self->hundredths, 1000000);
   self->text[1] = FPSDigit(self->hundredths, 100000);
   self->text[2] = FPSDigit(self->hundredths, 10000);
   self->text[3] = FPSDigit(self->hundredths, 1000);
   self->text[4] = FPSDigit(self->hundredths, 100);
   self->text[5] = '.';
   self->text[6] = FPSDigit(self->hundredths, 10);
   self->text[7] = FPSDigit(self->hundredths, 1);
   self->text[8] = ' ';
   self->text[9] = 'F';
   self->text[10] = 'P';
   self->text[11] = 'S';
   self->text[12] = '\0';

   self->lastUpdate.time = self->now;
   self->lastUpdate.frame = self->frame;
   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_AllocSurfaceID --
 *
 *      Allocate the next available surface ID.
 *
 *      XXX: This is a trivial implementation which just returns
 *           an incrementing integer.
 *
 * Results:
 *      Returns an unused sid.
 *
 * Side effects:
 *      Marks this sid as used.
 *
 *----------------------------------------------------------------------
 */

uint32
SVGA3DUtil_AllocSurfaceID(void)
{
   static uint32 nextSid = 0;
   return nextSid++;
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_AllocDMABuffer --
 *
 *      Allocate a buffer for DMA operations. Returns both a pointer
 *      (for us to use) and an SVGAGuestPtr (for the SVGA3D device to
 *      use).
 *
 * Results:
 *      Returns a local pointer and an SVGAGuestPtr to unused memory.
 *
 * Side effects:
 *      Allocates memory.
 *
 *----------------------------------------------------------------------
 */

void *
SVGA3DUtil_AllocDMABuffer(uint32 size,        // IN
                          SVGAGuestPtr *ptr)  // OUT
{
   return SVGA_AllocGMR(size, ptr);
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_DefineSurface2D --
 *
 *      This is a simplified version of SVGA3D_BeginDefineSurface(),
 *      which does not support cube maps, mipmaps, or volume textures.
 *
 * Results:
 *      Returns the new surface ID.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

uint32
SVGA3DUtil_DefineSurface2D(uint32 width,                // IN
                           uint32 height,               // IN
                           SVGA3dSurfaceFormat format)  // IN
{
   uint32 sid;
   SVGA3dSize *mipSizes;
   SVGA3dSurfaceFace *faces;

   sid = SVGA3DUtil_AllocSurfaceID();
   SVGA3D_BeginDefineSurface(sid, 0, format, &faces, &mipSizes, 1);

   faces[0].numMipLevels = 1;

   mipSizes[0].width = width;
   mipSizes[0].height = height;
   mipSizes[0].depth = 1;

   SVGA_FIFOCommitAll();

   return sid;
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_DefineSurface2DFlags --
 *
 *      This is a simplified version of SVGA3D_BeginDefineSurface(),
 *      which does not support cube maps, mipmaps, or volume textures,
 *      but which does allow the caller to specify whether this is an
 *      index or vertex buffer, and whether it is static or dynamic.
 *
 * Results:
 *      Returns the new surface ID.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
uint32
SVGA3DUtil_DefineSurface2DFlags(uint32 width,                // IN
                                uint32 height,               // IN
                                SVGA3dSurfaceFormat format,  // IN
                                uint32 flags)                // IN
{
   uint32 sid;
   SVGA3dSize *mipSizes;
   SVGA3dSurfaceFace *faces;

   sid = SVGA3DUtil_AllocSurfaceID();
   SVGA3D_BeginDefineSurface(sid, flags, format, &faces, &mipSizes, 1);

   faces[0].numMipLevels = 1;

   mipSizes[0].width = width;
   mipSizes[0].height = height;
   mipSizes[0].depth = 1;

   SVGA_FIFOCommitAll();

   return sid;
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_SurfaceDMA2D --
 *
 *      This is a simplified version of SVGA3D_BeginSurfaceDMA(),
 *      which copies a single 2D rectangle rooted at 0,0. It does
 *      not support volume textures, mipmaps, cube maps, or guest
 *      images with non-default pitch.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Begins an asynchronous DMA operation.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DUtil_SurfaceDMA2D(uint32 sid,                   // IN
                        SVGAGuestPtr *guestPtr,       // IN
                        SVGA3dTransferType transfer,  // IN
                        uint32 width,                 // IN
                        uint32 height)                // IN
{
   SVGA3dCopyBox *boxes;
   SVGA3dGuestImage guestImage;
   SVGA3dSurfaceImageId hostImage = { sid };

   guestImage.ptr = *guestPtr;
   guestImage.pitch = 0;

   SVGA3D_BeginSurfaceDMA(&guestImage, &hostImage, transfer, &boxes, 1);
   boxes[0].w = width;
   boxes[0].h = height;
   boxes[0].d = 1;
   SVGA_FIFOCommitAll();
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_DefineStaticBuffer --
 *
 *      This is a simplified function which defines a 1 dimensional
 *      surface with the format SVGA3D_BUFFER, copies the supplied
 *      data into a new DMA buffer, and begins a DMA transfer into
 *      the new surface.
 *
 * Results:
 *      Returns the new surface ID.
 *
 * Side effects:
 *      Allocates a surface.
 *      Allocates a DMA buffer.
 *      Begins an asynchronous DMA operation.
 *
 *----------------------------------------------------------------------
 */

uint32
SVGA3DUtil_DefineStaticBuffer(const void *data,  // IN
                              uint32 size)       // IN
{
   void *buffer;
   SVGAGuestPtr gPtr;
   uint32 sid = SVGA3DUtil_DefineSurface2D(size, 1, SVGA3D_BUFFER);

   buffer = SVGA3DUtil_AllocDMABuffer(size, &gPtr);
   memcpy(buffer, data, size);

   SVGA3DUtil_SurfaceDMA2D(sid, &gPtr, SVGA3D_WRITE_HOST_VRAM, size, 1);

   return sid;
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_LoadCompressedBuffer --
 *
 *      This is a simplified function which defines a 1 dimensional
 *      surface with the format SVGA3D_BUFFER, decompresses a DataFile
 *      object into a new DMA buffer, and begins a DMA transfer into
 *      the new surface.
 *
 * Results:
 *      Returns the new surface ID. Optionally returns the
 *      decompressed size of the buffer.
 *
 * Side effects:
 *      Allocates a surface.
 *      Allocates a DMA buffer.
 *      Begins an asynchronous DMA operation.
 *
 *----------------------------------------------------------------------
 */

uint32
SVGA3DUtil_LoadCompressedBuffer(const DataFile *file,  // IN
                                uint32 flags,          // IN
                                uint32 *pSize)         // OUT (optional)
{
   void *buffer;
   SVGAGuestPtr gPtr;
   uint32 size = DataFile_GetDecompressedSize(file);
   uint32 sid = SVGA3DUtil_DefineSurface2DFlags(size, 1, SVGA3D_BUFFER, flags);

   buffer = SVGA3DUtil_AllocDMABuffer(size, &gPtr);
   DataFile_Decompress(file, buffer, size);

   SVGA3DUtil_SurfaceDMA2D(sid, &gPtr, SVGA3D_WRITE_HOST_VRAM, size, 1);

   if (pSize) {
      *pSize = size;
   }

   return sid;
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_ClearFullscreen --
 *
 *      This is a simplified version of SVGA3D_BeginClear(), which
 *      assumes we're in full-screen mode and that the entire render
 *      target needs to be cleared.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DUtil_ClearFullscreen(uint32 cid,             // IN
                           SVGA3dClearFlag flags,  // IN
                           uint32 color,           // IN
                           float depth,            // IN
                           uint32 stencil)         // IN
{
   SVGA3dRect *rect;

   SVGA3D_BeginClear(cid, flags, color, depth, stencil, &rect, 1);
   memset(rect, 0, sizeof *rect);
   rect->w = gSVGA.width;
   rect->h = gSVGA.height;
   SVGA_FIFOCommitAll();
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_AllocDMAPool --
 *
 *      This is a simple example implementation of a DMA buffer pool-
 *      a collection of identically-sized DMA buffers which are
 *      allocated once and recycled.
 *
 * Results:
 *      Initializes the provided DMAPool structure.
 *
 * Side effects:
 *      Allocates DMA memory.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DUtil_AllocDMAPool(DMAPool *self,      // OUT
                        uint32 bufferSize,  // IN
                        uint32 numBuffers)  // IN
{
   int i;

   memset(self, 0, sizeof *self);

   self->bufferSize = bufferSize;
   self->numBuffers = numBuffers;

   if (numBuffers > MAX_DMA_POOL_BUFFERS) {
      SVGA_Panic("DMA pool larger than MAX_DMA_POOL_BUFFERS");
   }

   for (i = 0; i < numBuffers; i++) {

      self->buffers[i].pool = self;
      self->buffers[i].buffer = SVGA3DUtil_AllocDMABuffer(bufferSize,
                                                          &self->buffers[i].ptr);

      self->buffers[i].next = self->freeList;
      self->freeList = &self->buffers[i];
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_DMAPoolGetBuffer --
 *
 *      Retrieve an available buffer from a DMAPool. This
 *      returns the first available buffer from our freelist.
 *
 *      If no buffers are on the freelist, we use Sync and
 *      AsyncCall to give them a chance to become available.
 *
 *      If that fails, there must have been a buffer leak. We panic.
 *
 * Results:
 *      Returns an available DMAPoolBuffer.
 *
 * Side effects:
 *      May Sync. May run AsyncCall callbacks.
 *
 *----------------------------------------------------------------------
 */

DMAPoolBuffer *
SVGA3DUtil_DMAPoolGetBuffer(DMAPool *self)  // IN/OUT
{
   DMAPoolBuffer *buffer;

   if (!self->freeList) {
      SVGA_SyncToFence(SVGA_InsertFence());
      SVGA3DUtil_AsyncCall(NULL, NULL);
   }

   buffer = self->freeList;
   if (!buffer) {
      SVGA_Panic("No DMA buffers available from pool");
   }

   self->freeList = buffer->next;
   return buffer;
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_DMAPoolFreeBuffer --
 *
 *      Recycle an old DMAPool buffer. This function should be called
 *      only once the DMA transfer has been completed by the device.
 *      You must use the FIFO fence mechanism to guarantee this.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DUtil_DMAPoolFreeBuffer(DMAPoolBuffer *buffer)  // IN
{
   buffer->next = buffer->pool->freeList;
   buffer->pool->freeList = buffer;
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DUtil_SetShaderConstMatrix --
 *
 *      This is a simple wrapper around SVGA3D_SetShaderConst which
 *      makes it easier to set a constant matrix.
 *
 *      Each column of the matrix is stored separately, in four
 *      consecutive float4 vectors.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DUtil_SetShaderConstMatrix(uint32 cid,             // IN
                                uint32 reg,             // IN
                                SVGA3dShaderType type,  // IN
                                const float *matrix)    // IN
{
   int col;

   for (col = 0; col < 4; col++) {
      float vector[4];

      vector[0] = matrix[col + 0];
      vector[1] = matrix[col + 4];
      vector[2] = matrix[col + 8];
      vector[3] = matrix[col + 12];

      SVGA3D_SetShaderConst(cid, reg + col, type,
                            SVGA3D_CONST_TYPE_FLOAT, vector);
   }
}
