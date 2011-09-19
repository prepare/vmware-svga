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
 * gmr.c --
 *
 *      Utilities for creating and Guest Memory Regions (GMRs)
 */

#include "svga.h"
#include "gmr.h"

/*
 * Global data
 */

static uint32 heapTop;
GMRState gGMR;


/*
 *-----------------------------------------------------------------------------
 *
 * Heap_Reset --
 * Heap_Alloc --
 * Heap_AllocPages --
 * Heap_Discard --
 * Heap_DiscardPages --
 * Heap_ProbeMem --
 *
 *    Trivial memory heap with 32-bit aligned and page-aligned
 *    allocation. Memory can't be freed, but the entire heap
 *    can be reset.
 *
 *    On 'discard', we don't actually free memory- we just write
 *    over it to ensure its values aren't still being used.
 *
 *    We insert padding pages between each individual memory
 *    allocation, to ensure that separate allocations are not
 *    accidentally contiguous.
 *
 *-----------------------------------------------------------------------------
 */

void
Heap_Reset(void)
{
   extern uint8 _end[];
   heapTop = (uint32) _end;
}

void
Heap_ProbeMem(volatile uint32 *addr, uint32 size)
{
   const uint32 probe = 0x55AA55AA;
   while (size > sizeof *addr) {
      *addr = probe;
      if (*addr != probe) {
         goto error;
      }
      *addr = ~probe;
      if (*addr != ~probe) {
         goto error;
      }
      size -= sizeof *addr;
      addr++;
   }
   return;
 error:
   SVGA_Panic("Out of physical memory.\n\n"
              "Increase the amount of memory allocated to this VM.\n"
              "128MB of RAM is recommended.\n");
}

void *
Heap_Alloc(uint32 bytes)
{
   const uint32 padding = 16;
   void *result;

   heapTop = (heapTop + 3) & ~3;
   result = (void*) heapTop;

   bytes += padding;
   heapTop += bytes;
   Heap_ProbeMem(result, bytes);

   return result;
}

PPN
Heap_AllocPages(uint32 numPages)
{
   const uint32 padding = 1;
   PPN result;
   uint32 bytes;

   heapTop = (heapTop + PAGE_MASK) & ~PAGE_MASK;
   result = heapTop / PAGE_SIZE;

   numPages += padding;
   bytes = numPages * PAGE_SIZE;
   heapTop += bytes;
   Heap_ProbeMem(PPN_POINTER(result), bytes);

   return result;
}

void
Heap_Discard(void *data, uint32 bytes)
{
   memset(data, 0xAA, bytes);
}

void
Heap_DiscardPages(PPN firstPage, uint32 numPages)
{
   memset(PPN_POINTER(firstPage), 0xAA, numPages * PAGE_SIZE);
}


/*
 *-----------------------------------------------------------------------------
 *
 * GMR_AllocDescriptor --
 *
 *    Given a flat array of descriptors, allocate physical pages as
 *    necessary and copy the descriptors into a linked list of
 *    pages. The result is a PPN which is suitable to give directly to
 *    the SVGA device's GMR_DESCRIPTOR register.
 *
 * Results:
 *    If numDescriptors is zero, returns zero. otherwise, returns
 *    a physical page number.
 *
 * Side effects:
 *    Allocates (and never frees) memory for the GMR descriptor.
 *
 *-----------------------------------------------------------------------------
 */

PPN
GMR_AllocDescriptor(SVGAGuestMemDescriptor *descArray,
                    uint32 numDescriptors)
{
   const uint32 descPerPage = PAGE_SIZE / sizeof(SVGAGuestMemDescriptor) - 1;
   SVGAGuestMemDescriptor *desc = NULL;
   PPN firstPage = 0;
   PPN page = 0;
   int i = 0;

   while (numDescriptors) {
      if (!firstPage) {
         firstPage = page = Heap_AllocPages(1);
      }

      desc = PPN_POINTER(page);

      if (i == descPerPage) {
         /*
          * Terminate this page with a pointer to the next one.
          */
         page = Heap_AllocPages(1);
         desc[i].ppn = page;
         desc[i].numPages = 0;
         i = 0;
         continue;
      }

      desc[i] = *descArray;
      i++;
      descArray++;
      numDescriptors--;
   }

   if (desc) {
      /* Terminate the end of the descriptor list. */
      desc[i].ppn = 0;
      desc[i].numPages = 0;
   }

   return firstPage;
}


/*
 *-----------------------------------------------------------------------------
 *
 * GMR_Define --
 *
 *    Initialize a GMR, using a flat array of descriptors.  The array
 *    is copied into a set of physically discontiguous pages, which we
 *    give the host.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Defines or redefines a GMR in the SVGA device.
 *    Allocates (and never frees) memory for the GMR descriptor.
 *
 *-----------------------------------------------------------------------------
 */

void
GMR_Define(uint32 gmrId,
           SVGAGuestMemDescriptor *descArray,
           uint32 numDescriptors)
{
   PPN desc = GMR_AllocDescriptor(descArray, numDescriptors);

   /*
    * Define/undefine the GMR. Defining an empty GMR is equivalent to
    * undefining a GMR.
    */

   SVGA_WriteReg(SVGA_REG_GMR_ID, gmrId);
   SVGA_WriteReg(SVGA_REG_GMR_DESCRIPTOR, desc);

   if (desc) {
      /*
       * Clobber the first page, to verify that the device reads our
       * descriptors synchronously when we write the GMR registers.
       */
      Heap_DiscardPages(desc, 1);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * GMR_DefineContiguous --
 *
 *    Allocate and define a physically contiguous GMR, consisting of a
 *    single SVGAGuestMemDescriptor.
 *
 * Results:
 *    Returns the first PPN of the allocated GMR region. All of
 *    'numPages' subsequent PPNs are also part of the GMR.
 *
 * Side effects:
 *    Allocates and never frees memory for the GMR descriptor and for
 *    the GMR contents itself.
 *
 *-----------------------------------------------------------------------------
 */

PPN
GMR_DefineContiguous(uint32 gmrId, uint32 numPages)
{
   SVGAGuestMemDescriptor desc = {
      .ppn = Heap_AllocPages(numPages),
      .numPages = numPages,
   };

   GMR_Define(gmrId, &desc, 1);

   return desc.ppn;
}


/*
 *-----------------------------------------------------------------------------
 *
 * GMR_DefineEvenPages --
 *
 *    Define a worst-case discontiguous GMR, in which we map only the
 *    even pages. This allocates twice numPages, and creates a GMR
 *    which skips every other page.  To be extra annoying, we also
 *    make sure that descriptors are allocated on physically
 *    discontiguous pages.
 *
 * Results:
 *    Returns the first PPN of the allocated GMR region. All of
 *    'numPages*2' subsequent PPNs have been allocated, but only
 *    the even-numbered pages in that sequence are mapped into the GMR.
 *
 * Side effects:
 *    Allocates and never frees memory for the GMR descriptor and for
 *    the GMR contents itself.
 *
 *-----------------------------------------------------------------------------
 */

PPN
GMR_DefineEvenPages(uint32 gmrId, uint32 numPages)
{
   SVGAGuestMemDescriptor *desc;
   PPN region = Heap_AllocPages(numPages * 2);
   int i;

   desc = Heap_Alloc(sizeof *desc * numPages);

   for (i = 0; i < numPages; i++) {
      desc[i].ppn = region + i*2;
      desc[i].numPages = 1;
   }

   GMR_Define(gmrId, desc, numPages);

   return region;
}


/*
 *-----------------------------------------------------------------------------
 *
 * GMR_FreeAll --
 *
 *    Undefine all GMRs.
 *
 *    This frees device resources, but it doesn't actually free
 *    any memory from our trivial heap. If you are done with the
 *    heap, call Heap_Reset() separately.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Undefines GMRs.
 *
 *-----------------------------------------------------------------------------
 */

void
GMR_FreeAll(void)
{
   uint32 id;

   for (id = 0; id < gGMR.maxIds; id++) {
      GMR_Define(id, NULL, 0);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * GMR_Init --
 *
 *    Read GMR capabilities, or panic if GMRs aren't supported.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Fills in 'gGMR'.
 *
 *-----------------------------------------------------------------------------
 */

void
GMR_Init(void)
{
   if (gSVGA.capabilities & SVGA_CAP_GMR) {
      gGMR.maxIds = SVGA_ReadReg(SVGA_REG_GMR_MAX_IDS);
      gGMR.maxDescriptorLen = SVGA_ReadReg(SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH);
   } else {
      SVGA_Panic("Virtual device does not have Guest Memory Region (GMR) support.");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * GMR2_Init --
 *
 *    Read GMR2 capabilities, or panic if GMR2 isn't supported.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Fills in 'gGMR'.
 *
 *-----------------------------------------------------------------------------
 */

void
GMR2_Init(void)
{
   if (gSVGA.capabilities & SVGA_CAP_GMR2) {
      gGMR.maxIds = SVGA_ReadReg(SVGA_REG_GMR_MAX_IDS);
      gGMR.maxPages = SVGA_ReadReg(SVGA_REG_GMRS_MAX_PAGES);
   } else {
      SVGA_Panic("Virtual device does not have Guest Memory Region version 2 (GMR2) support.");
   }
}
