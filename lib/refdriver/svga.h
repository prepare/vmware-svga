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
 * svga.h --
 *
 *      This is a simple example driver for the VMware SVGA device.
 *      It handles initialization, register accesses, low-level
 *      command FIFO writes, and host/guest synchronization.
 */

#ifndef __SVGA_H__
#define __SVGA_H__

#include "types.h"
#include "pci.h"
#include "intr.h"

// XXX: Shouldn't have to do this here.
#define INLINE __inline__

#include "svga_reg.h"
#include "svga_escape.h"
#include "svga_overlay.h"
#include "svga3d_reg.h"

typedef struct SVGADevice {
   PCIAddress pciAddr;
   uint32     ioBase;
   uint32    *fifoMem;
   uint8     *fbMem;
   uint32     fifoSize;
   uint32     fbSize;

   uint32     deviceVersionId;
   uint32     capabilities;

   uint32     width;
   uint32     height;
   uint32     bpp;
   uint32     pitch;

   struct {
      uint32  reservedSize;
      Bool    usingBounceBuffer;
      uint8   bounceBuffer[1024 * 1024];
      uint32  nextFence;
   } fifo;

   volatile struct {
      uint32        pending;
      uint32        switchContext;
      IntrContext   oldContext;
      IntrContext   newContext;
      uint32        count;
   } irq;

} SVGADevice;

extern SVGADevice gSVGA;

void SVGA_Init(void);
void SVGA_SetMode(uint32 width, uint32 height, uint32 bpp);
void SVGA_Disable(void);
void SVGA_Panic(const char *err);
void SVGA_DefaultFaultHandler(int vector);

uint32 SVGA_ReadReg(uint32 index);
void SVGA_WriteReg(uint32 index, uint32 value);
uint32 SVGA_ClearIRQ(void);
uint32 SVGA_WaitForIRQ();

Bool SVGA_IsFIFORegValid(int reg);
Bool SVGA_HasFIFOCap(int cap);

void *SVGA_FIFOReserve(uint32 bytes);
void *SVGA_FIFOReserveCmd(uint32 type, uint32 bytes);
void *SVGA_FIFOReserveEscape(uint32 nsid, uint32 bytes);
void SVGA_FIFOCommit(uint32 bytes);
void SVGA_FIFOCommitAll(void);

uint32 SVGA_InsertFence(void);
void SVGA_SyncToFence(uint32 fence);
Bool SVGA_HasFencePassed(uint32 fence);
void SVGA_RingDoorbell(void);

/* 2D commands */

void SVGA_Update(uint32 x, uint32 y, uint32 width, uint32 height);
void SVGA_BeginDefineCursor(const SVGAFifoCmdDefineCursor *cursorInfo,
                            void **andMask, void **xorMask);
void SVGA_BeginDefineAlphaCursor(const SVGAFifoCmdDefineAlphaCursor *cursorInfo,
                                 void **data);
void SVGA_MoveCursor(uint32 visible, uint32 x, uint32 y, uint32 screenId);

void SVGA_BeginVideoSetRegs(uint32 streamId, uint32 numItems,
                            SVGAEscapeVideoSetRegs **setRegs);
void SVGA_VideoSetAllRegs(uint32 streamId, SVGAOverlayUnit *regs, uint32 maxReg);
void SVGA_VideoSetReg(uint32 streamId, uint32 registerId, uint32 value);
void SVGA_VideoFlush(uint32 streamId);

#endif /* __SVGA_H__ */
