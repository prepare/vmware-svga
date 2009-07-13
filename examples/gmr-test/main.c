/*
 * SVGA3D example: Test harness and low-level example program for
 * Guest Memory Regions.
 *
 * With Guest Memory regions, the SVGA device can perform DMA
 * operations directly between guest system memory and host
 * VRAM. Guest drivers use the device's GMR registers to set up
 * regions of guest memory which can be accessed by the device, then
 * the driver refers to these regions by ID when sending pointers over
 * the command FIFO.
 *
 * GMRs support physically contiguous or discontiguous memory. This
 * example is a bit contrived because we're testing GMRs without an
 * operating system or a virtual memory subsystem- in a real OS,
 * support for physically discontiguous addresses would often be
 * required in order to ensure that the GMR's address space matches
 * that of a particular virtual address space in the OS. In this
 * example, we just test physically discontiguous regions for the sake
 * of testing them.
 *
 * This test harness is focused on system memory GMRs, however it also
 * ends up testing much of the GLSurface and GLFBO code, since it
 * performs GMR-to-GMR copies by way of surface DMA operations.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga.h"
#include "svga3dutil.h"
#include "svga3dtext.h"
#include "console_vga.h"
#include "gmr.h"
#include "math.h"
#include "mt19937ar.h"

/* Maximum number of copy boxes we'll test with. The host has no limit. */
#define MAX_COPY_BOXES     128

/*
 * Global data
 */

static uint32 tempSurfaceId;
static uint32 randSeed;
static uint32 testIters;
static uint32 testRegionSize;
static const char *testPass;


/*
 * TestPattern_Write --
 * TestPattern_Check --
 *
 *    Write/check an arbitrary deterministic test pattern in the
 *    provided buffer. The buffer must be a multiple of 4 bytes long.
 *
 *    Instead of generating a unique random number for every word,
 *    which would be pretty slow, this generates a prime number of
 *    random words, which then repeat across the entire check range.
 */

#define PATTERN_BUFFER_LEN  41  // Must be prime

void
TestPattern_Write(uint32 *buffer,
                  uint32 size)
{
#ifndef DISABLE_CHECKING
   uint32 pattern[PATTERN_BUFFER_LEN];
   int i;

   init_genrand(randSeed);
   for (i = 0; i < PATTERN_BUFFER_LEN; i++) {
      pattern[i] = genrand_int32();
   }
   i = 0;

   size /= sizeof *buffer;
   while (size--) {
      *(buffer++) = pattern[i];
      if (++i == PATTERN_BUFFER_LEN) {
         i = 0;
      }
   }
#endif
}

void
TestPattern_Check(uint32 *buffer,
                  uint32 size,
                  uint32 offset,
                  uint32 line,
                  uint32 index)
{
#ifndef DISABLE_CHECKING
   uint32 pattern[PATTERN_BUFFER_LEN];
   int i;

   init_genrand(randSeed);
   for (i = 0; i < PATTERN_BUFFER_LEN; i++) {
      pattern[i] = genrand_int32();
   }

   offset /= sizeof *buffer;
   size /= sizeof *buffer;

   i = offset % PATTERN_BUFFER_LEN;

   while (size) {
      uint32 v = pattern[i];
      if (++i == PATTERN_BUFFER_LEN) {
         i = 0;
      }

      if (*buffer != v) {
         SVGA_Disable();
         ConsoleVGA_Init();
         Console_Format("Test pattern mismatch on %4x.%4x\n"
                        "Test pass: %s\n"
                        "Mismatch at %08x, with %08x bytes left in block.\n\n",
                        line, index, testPass, buffer, size * sizeof *buffer);

         size = MIN(size, 16);
         while (size) {
            Console_Format("Actual: %08x  Expected: %08x\n",
                           *buffer, v);
            buffer++;
            size--;

            v = pattern[i];
            if (++i == PATTERN_BUFFER_LEN) {
               i = 0;
            }
         }

         Intr_Disable();
         Intr_Halt();
      }

      buffer++;
      size--;
   }
#endif
}


/*
 * GMR_GenericCopy --
 *
 *    Copy between two GMRs, using an arbitrarily shaped buffer
 *    surface and an arbitrary list of copy boxes.
 *
 *    In the copy boxes, the 'source' represents locations
 *    on both guest surfaces and the 'destination' represents
 *    a locations in host VRAM.
 */

void
GMR_GenericCopy(SVGAGuestPtr *dest,
                SVGAGuestPtr *src,
                SVGA3dSize *surfSize,
                SVGA3dSurfaceFormat format,
                SVGA3dCopyBox *boxes,
                uint32 numBoxes)
{
   SVGA3dSize *mipSizes;
   SVGA3dSurfaceFace *faces;
   SVGA3dCopyBox *dmaBoxes;
   SVGA3dGuestImage srcImage = { *src };
   SVGA3dGuestImage destImage = { *dest };
   SVGA3dSurfaceImageId hostImage = { tempSurfaceId };

   SVGA3D_BeginDefineSurface(tempSurfaceId, 0, format, &faces, &mipSizes, 1);
   faces[0].numMipLevels = 1;
   mipSizes[0] = *surfSize;
   SVGA_FIFOCommitAll();

   SVGA3D_BeginSurfaceDMA(&srcImage, &hostImage, SVGA3D_WRITE_HOST_VRAM,
                          &dmaBoxes, numBoxes);
   memcpy(dmaBoxes, boxes, numBoxes * sizeof boxes[0]);
   SVGA_FIFOCommitAll();

   SVGA3D_BeginSurfaceDMA(&destImage, &hostImage, SVGA3D_READ_HOST_VRAM,
                          &dmaBoxes, numBoxes);
   memcpy(dmaBoxes, boxes, numBoxes * sizeof boxes[0]);
   SVGA_FIFOCommitAll();

   SVGA3D_DestroySurface(tempSurfaceId);

   /*
    * Wait for both DMA operations to finish. For better test
    * coverage, we'll alternate between using fences and legacy syncs.
    */
   if (testIters & 1) {
      SVGA_SyncToFence(SVGA_InsertFence());
   } else {
      SVGA_WriteReg(SVGA_REG_SYNC, 1);
      while (SVGA_ReadReg(SVGA_REG_BUSY));
   }
}


/*
 * Display_BeginPass --
 *
 *    Begin a new test pass, and update the on-screen display.
 */

void
Display_BeginPass(const char *pass)
{
   testPass = pass;

   Console_Clear();
   Console_Format("VMware SVGA3D Example:\n"
                  "Guest Memory Region stress-test.\n"
                  "\n"
                  "Host capabilities\n"
                  "-----------------\n"
                  "\n"
                  "            Max IDs: %d\n"
                  " Max Descriptor Len: %d\n"
                  "\n"
                  "Test status\n"
                  "-----------\n"
                  "\n"
                  "   Iterations: %d\n"
                  "         Seed: %08x\n"
                  "      Running: %s\n"
                  "\n"
#ifdef DISABLE_CHECKING
                  "CHECKING DISABLED. This test can't fail.\n",
#else
                  "Test is running successfully so far. Will Panic on failure.\n",
#endif
                  gGMR.maxIds, gGMR.maxDescriptorLen, testIters,
                  randSeed, testPass);

   VMBackdoor_VGAScreenshot();
   SVGA3DText_Update();
   SVGA3DUtil_ClearFullscreen(CID, SVGA3D_CLEAR_COLOR, 0x000080, 1.0f, 0);
   SVGA3DText_Draw();
   SVGA3DUtil_PresentFullscreen();
}


/*
 * runTestPass --
 *
 *    Run one test pass- create two large GMRs, one contiguous and one
 *    discontiguous.  Copy a test pattern back and forth between the
 *    two buffers, using the provided surface size and type.
 */

void
runTestPass(uint32 testRegionSize,
            SVGA3dSize *surfSize,
            SVGA3dSurfaceFormat format,
            SVGA3dCopyBox *boxes,
            uint32 numBoxes)
{
   SVGAGuestPtr contig = { 0, 0 };
   SVGAGuestPtr evenPages = { gGMR.maxIds - 1, 0 };
   int i;

   uint32 contigPages = GMR_DefineContiguous(contig.gmrId, gGMR.maxDescriptorLen * 2);
   uint32 discontigPages = GMR_DefineEvenPages(evenPages.gmrId, gGMR.maxDescriptorLen);

   /*
    * Write a test pattern into the contiguous GMR.
    */

   TestPattern_Write(PPN_POINTER(contigPages), testRegionSize);
   TestPattern_Check(PPN_POINTER(contigPages), testRegionSize, 0, __LINE__, 0);

   /*
    * Copy from contiguous to discontiguous.
    */

   GMR_GenericCopy(&evenPages, &contig, surfSize, format, boxes, numBoxes);

   /*
    * Check the discontiguous GMR, page-by-page.
    */

   for (i = 0; i < testRegionSize / PAGE_SIZE; i++) {
      TestPattern_Check(PPN_POINTER(discontigPages + 2*i),
                        PAGE_SIZE, PAGE_SIZE * i, __LINE__, i);
   }

   /*
    * Clear the contiguous GMR, then copy data back into it from the discontiguous GMR.
    */

   memset(PPN_POINTER(contigPages), 0x42, testRegionSize);
   GMR_GenericCopy(&contig, &evenPages, surfSize, format, boxes, numBoxes);

   /*
    * Check the contiguous GMR again.
    */

   TestPattern_Check(PPN_POINTER(contigPages), testRegionSize, 0, __LINE__, i);

   GMR_FreeAll();
   Heap_Reset();
}


/*
 * createBoxes --
 *
 *    Create an array of N copyboxes which cover an entire surface.
 *    This begins with a single large copybox, and iteratively splits
 *    small boxes off from a random face on the original box.
 *
 *    This function can and will generate degenerate copy boxes
 *    (zero-size).  The SVGA3D device must ignore those boxes.
 */

void
createBoxes(SVGA3dSize *size,
            SVGA3dCopyBox *boxes,
            uint32 numBoxes)
{
   uint32 i;
   SVGA3dCopyBox space = {
      .w = size->width,
      .h = size->height,
      .d = size->depth,
   };

   init_genrand(randSeed);

   for (i = 0; i < numBoxes - 1; i++) {
      uint32 rand = genrand_int32();
      uint32 a;
      memcpy(&boxes[i], &space, sizeof space);
      switch (rand % 6) {

      case 0:                   /* X- */
         a = rand % space.w;
         boxes[i].w = a;
         space.x += a;
         space.w -= a;
         break;

      case 1:                   /* Y- */
         a = rand % space.h;
         boxes[i].h = a;
         space.y += a;
         space.h -= a;
         break;

      case 2:                   /* Z- */
         a = rand % space.d;
         boxes[i].d = a;
         space.z += a;
         space.d -= a;
         break;

      case 3:                   /* X+ */
         a = rand % space.w;
         boxes[i].w = a;
         space.w -= a;
         boxes[i].x += space.w;
         break;

      case 4:                   /* Y+ */
         a = rand % space.h;
         boxes[i].h = a;
         space.h -= a;
         boxes[i].y += space.h;
         break;

      case 5:                   /* Z+ */
         a = rand % space.d;
         boxes[i].d = a;
         space.d -= a;
         boxes[i].z += space.d;
         break;
      }
   }

   boxes[i] = space;

   for (i = 0; i < numBoxes; i++) {
      boxes[i].srcx = boxes[i].x;
      boxes[i].srcy = boxes[i].y;
      boxes[i].srcz = boxes[i].z;
   }
}


/*
 * createMisaligned1dBoxes --
 *
 *    Create an array of N 1-dimensional copyboxes, most of which
 *    have a width of PAGE_SIZE-1 bytes.
 *
 *    The boxes may extend past the end of 'size'. This is okay,
 *    the SVGA3D device is responsible for clipping them.
 */

void
createMisaligned1dBoxes(uint32 size,
                        SVGA3dCopyBox *boxes,
                        uint32 numBoxes)
{
   uint32 offset = 0;
   uint32 i;

   memset(boxes, 0, sizeof *boxes * numBoxes);

   for (i = 0; i < numBoxes - 1; i++) {
      boxes[i].x = boxes[i].srcx = offset;
      boxes[i].w = PAGE_SIZE-1;
      boxes[i].h = 1;
      boxes[i].d = 1;
      offset += boxes[i].w;
   }

   boxes[i].x = boxes[i].srcx = offset;
   boxes[i].w = size - offset;
   boxes[i].h = 1;
   boxes[i].d = 1;
}


/*
 * runTests --
 *
 *    Main function to run one iteration of all tests.
 */

void
runTests(void)
{
   /* Maximum size of worst-case-discontiguous region we can represent */
   uint32 largeRegionSize = gGMR.maxDescriptorLen * PAGE_SIZE;

   /* Smaller region, to speed up other testing. */
   uint32 regionSize = 0x20 * PAGE_SIZE;

   /* Smallest region, suitable for 1D textures. */
   uint32 tinyRegionSize = 1024;

   SVGA3dSize size1dLarge = {
      .width = largeRegionSize,
      .height = 1,
      .depth = 1,
   };

   SVGA3dSize size1d = {
      .width = tinyRegionSize,
      .height = 1,
      .depth = 1,
   };

   SVGA3dSize size2d = {
      .width = 0x100,
      .height = regionSize / 0x100,
      .depth = 1,
   };

   SVGA3dSize size3d = {
      .width = 0x40,
      .height = 0x40,
      .depth = regionSize / 0x1000,
   };

   /* A single maximally-sized 1D copybox. The host will clip it. */
   SVGA3dCopyBox maxBox1d = {
      .w = 0xFFFFFFFFUL,
      .h = 1,
      .d = 1,
   };

   SVGA3dCopyBox boxes[MAX_COPY_BOXES];

   /*
    * Basic per-surface-format tests.
    *
    * Note that 3D compressed textures are not expected to work yet,
    * so we skip those tests.
    */

#define TEST_FORMAT_2D(f, b)                                               \
   {                                                                       \
      Display_BeginPass("Single copy via 1D " #f " surface.");             \
      runTestPass(tinyRegionSize*b, &size1d, SVGA3D_ ## f, &maxBox1d, 1);  \
                                                                           \
      Display_BeginPass("Single copy via 2D " #f " surface.");             \
      createBoxes(&size2d, boxes, 1);                                      \
      runTestPass(regionSize*b, &size2d, SVGA3D_ ## f, boxes, 1);          \
   }

#define TEST_FORMAT(f, b)                                                  \
   {                                                                       \
      TEST_FORMAT_2D(f, b)                                                 \
                                                                           \
      Display_BeginPass("Single copy via 3D " #f " surface.");             \
      createBoxes(&size3d, boxes, 1);                                      \
      runTestPass(regionSize*b, &size3d, SVGA3D_ ## f, boxes, 1);          \
   }

   TEST_FORMAT(BUFFER, 1)       // Buffers use their own host VRAM type
   TEST_FORMAT(LUMINANCE8, 1)   // Test a simple 8bpp format
   TEST_FORMAT(ALPHA8, 1)       // To isolate alpha channel bugs
   TEST_FORMAT(A8R8G8B8, 4)     // ARGB surfaces have more readback paths than others
   TEST_FORMAT_2D(DXT2, 1)      // Test 4x4 block size, and compressed texture upload/download

#undef TEST_FORMAT
#undef TEST_FORMAT_2D

   /*
    * Test large buffers (Limited by max size of worst-case fragmented GMR)
    */

   Display_BeginPass("Single copy via 1D BUFFER surface. (Large region)");
   runTestPass(largeRegionSize, &size1dLarge, SVGA3D_BUFFER, &maxBox1d, 1);

   /*
    * Test with randomly subdivided copyboxes.
    */

#define TEST_FORMAT_2D(f, b)                                                    \
   {                                                                            \
      Display_BeginPass("Subdivided copy via 2D " #f " surface.");              \
      createBoxes(&size2d, boxes, MAX_COPY_BOXES);                              \
      runTestPass(regionSize*b, &size2d, SVGA3D_ ## f, boxes, MAX_COPY_BOXES);  \
   }

#define TEST_FORMAT(f, b)                                                       \
   {                                                                            \
      TEST_FORMAT_2D(f, b)                                                      \
                                                                                \
      Display_BeginPass("Subdivided copy via 3D " #f " surface.");              \
      createBoxes(&size3d, boxes, MAX_COPY_BOXES);                              \
      runTestPass(regionSize*b, &size3d, SVGA3D_ ## f, boxes, MAX_COPY_BOXES);  \
   }

   TEST_FORMAT(BUFFER, 1)
   TEST_FORMAT(ALPHA8, 1)
   TEST_FORMAT(A8R8G8B8, 4)
   TEST_FORMAT_2D(DXT2, 1)      // Test compressed texture rectangle clipping

#undef TEST_FORMAT
#undef TEST_FORMAT_2D

   /*
    * Test another large 1D copy, split into slightly misaligned chunks.
    */

   Display_BeginPass("Misaligned copies via 1D BUFFER surface. (Large region)");
   createMisaligned1dBoxes(largeRegionSize, boxes, MAX_COPY_BOXES);
   runTestPass(largeRegionSize, &size1dLarge, SVGA3D_BUFFER, boxes, MAX_COPY_BOXES);
}


/*
 * main --
 *
 *    Entry point and main loop for the example.
 */

int
main(void)
{
   SVGA3DUtil_InitFullscreen(CID, 640, 480);
   SVGA3DText_Init();
   GMR_Init();
   Heap_Reset();

   tempSurfaceId = SVGA3DUtil_AllocSurfaceID();
   testRegionSize = gGMR.maxDescriptorLen * PAGE_SIZE;

   while (1) {
      runTests();

      randSeed = genrand_int32();
      testIters++;
   }

   return 0;
}
