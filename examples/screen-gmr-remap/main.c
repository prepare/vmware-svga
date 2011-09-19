/*
 * Stress test for GMR-to-screen blits with dynamic GMR mapping.
 *
 * This test uses GMRs in a rather extreme way- it repeatedly repaints
 * the screen in page-sized (32*32*4 = 4096) chunks, using GMRs which
 * are redefined for each DMA operation. Each in-flight DMA has a
 * separate GMR allocated to it.
 *
 * The first page of each GMR contains the data we're actually blitting
 * to the screen. This page comes from a rotating pool of 4096 pages
 * (16 MB of memory) which has a static pattern of colored tiles painted
 * into it during initialization.
 *
 * The rest of each GMR is fluff. After the single page of useful
 * data, we have 128 dummy pages. This makes the GMRs a bit more
 * reasonably sized (512 KB) to simulate the size of a more typical
 * graphics-related mapping.
 */

#include "svga.h"
#include "gmr.h"
#include "screen.h"
#include "intr.h"
#include "math.h"


/*
 * fillPages --
 *
 *    Fill a range of contiguous pages with a test pattern.  In this
 *    case, each page is a solid color and each consecutive page has a
 *    different color.
 */

static void
fillPages(PPN firstPage, uint32 count, uint32 tileSize)
{
   while (count) {
      const uint32 tick = (uint32)firstPage;
      const float rPhase = tick * 0.01;
      const float gPhase = tick * 0.02;
      const float bPhase = tick * 0.03;

      const uint8 r = sinf(rPhase) * 0x60 + 0x80;
      const uint8 g = sinf(gPhase) * 0x60 + 0x80;
      const uint8 b = sinf(bPhase) * 0x60 + 0x80;

      const uint32 color = (r << 16) | (g << 8) | b;

      memset32(PPN_POINTER(firstPage), color, tileSize * tileSize);

      count--;
      firstPage++;
   }
}


/*
 * main --
 *
 *    Main loop and initialization.
 */

int
main(void)
{
   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);
   SVGA_Init();
   GMR_Init();
   Heap_Reset();
   SVGA_SetMode(0, 0, 32);
   Screen_Init();

   SVGAScreenObject myScreen = {
      .structSize = sizeof(SVGAScreenObject),
      .id = 0,
      .flags = SVGA_SCREEN_HAS_ROOT | SVGA_SCREEN_IS_PRIMARY,
      .size = { 800, 600 },
      .root = { -1234, 5678 },
   };
   Screen_Create(&myScreen);

   const uint32 numPages = 4096;
   const uint32 tileSize = 32;

   const uint32 bitsPerPixel = 32;
   const uint32 colorDepth = 24;

   const uint32 tileBytesPerLine = tileSize * sizeof(uint32);

   const SVGAGMRImageFormat tileFormat = {{{
      .bitsPerPixel = bitsPerPixel,
      .colorDepth = colorDepth,
   }}};

   /*
    * Allocate a pool of system memory pages. In a real driver, these
    * might be pages we get at runtime from a user-mode app and lock
    * down prior to a DMA operation. We have many more pages than we
    * have GMRs or in-flight DMAs.
    */

   PPN firstPage = Heap_AllocPages(numPages);
   PPN lastPage = firstPage + numPages - 1;
   PPN currentPage = firstPage;
   fillPages(firstPage, numPages, tileSize);

   /*
    * Allocate a GMR descriptor full of dummy mappings. We'll throw a
    * handful of these into each GMR, so that the resulting GMRs have
    * a more realistic size than just a single page.
    */

   static SVGAGuestMemDescriptor dummyPages[128];
   int i;

   for (i = 0; i < arraysize(dummyPages); i++) {
      dummyPages[i].ppn = 1024 + (i & 0xF) * 3;
      dummyPages[i].numPages = 1;
   }

   PPN dummyDescriptor = GMR_AllocDescriptor(dummyPages, arraysize(dummyPages));

   /*
    * Allocate a single page to use for the GMR descriptor
    * head. Instead of using GMR_Define(), we'll do this manually so
    * that we can keep recycling the same single-page descriptor.
    */

   PPN descPage = Heap_AllocPages(1);
   SVGAGuestMemDescriptor *desc = PPN_POINTER(descPage);

   /*
    * Each GMR has a fence, so we can wait to re-use it until after
    * the DMA has expired. In this example, we use the first 32 GMRs.
    */

   uint32 gmrFences[32] = { 0 };
   uint32 gmrIndex = 5;

   /*
    * Keep looping over the whole screen, tile by tile.
    */

   while (1) {
      int x, y;
      for (y = 0; y < myScreen.size.height; y += tileSize) {
         for (x = 0; x < myScreen.size.width; x += tileSize) {

            /*
             * Wait until we're done with the old GMR.
             */

            SVGA_SyncToFence(gmrFences[gmrIndex]);

            /*
             * Define the new GMR, point it to the next sysmem page in
             * our pool.  The first page in this GMR will be our
             * filled page, the rest will be dummy pages.
             */

            desc[0].ppn = currentPage;
            desc[0].numPages = 1;
            desc[1].ppn = dummyDescriptor;
            desc[1].numPages = 0;

            SVGA_WriteReg(SVGA_REG_GMR_ID, gmrIndex);
            SVGA_WriteReg(SVGA_REG_GMR_DESCRIPTOR, descPage);

            currentPage++;
            if (currentPage == lastPage) {
               currentPage = firstPage;
            }

            /*
             * Do a blit from this GMR to the screen, and set a fence so that
             * we can wait to recycle the GMR until after this DMA completes.
             */

            SVGAGuestPtr gPtr = {
               .gmrId = gmrIndex,
               .offset = 0,
            };
            Screen_DefineGMRFB(gPtr, tileBytesPerLine, tileFormat);

            SVGASignedPoint blitOrigin = { 0, 0 };
            SVGASignedRect blitDest = { x, y, x+tileSize, y+tileSize };

            Screen_BlitFromGMRFB(&blitOrigin, &blitDest, myScreen.id);
            gmrFences[gmrIndex] = SVGA_InsertFence();

            /*
             * Next GMR..
             */
            gmrIndex = (gmrIndex + 1) % arraysize(gmrFences);
         }
      }
   }

   return 0;
}
