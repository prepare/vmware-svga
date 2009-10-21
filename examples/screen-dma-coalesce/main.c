/*
 * Demonstration for DMA coalescing in SVGA devices which support SVGA
 * Screen Object.
 *
 * All DMA operations, including Screen Object DMAs and legacy
 * "UPDATE" commands" include extra guarantees on hosts which support
 * the Screen Object extension.  DMAs must occur when and only when
 * the guest specifies them in the FIFO. All DMA side-effects must
 * occur in FIFO order, and at any FENCE the guest is guaranteed that
 * all DMAs prior to that fence have taken place.
 *
 * So, this is fairly strict compared to pre-Screen-Object hosts, but
 * the SVGA device still has room to optimize away redundant DMA
 * operations that occur between two FENCEs. If the guest asks the
 * SVGA device to perform the same DMA 100 times, there is no way to
 * tell the difference between performing one DMA and performing 100
 * DMAs if they all occur at exactly the same instant.
 *
 * Many things can act as a barrier for this optimization: other types
 * of DMA operations, including readback. DMA from a different
 * GMRFB. DMA with incompatible source/dest offsets. FENCEs or legacy
 * SYNCs.
 *
 * As a simple demonstration of this feature, this demo runs three
 * timed tests:
 *
 *  - One DMA followed by a fence
 *  - Ten overlapping DMAs, followed by one fence
 *  - Ten overlapping DMAs, each followed by a fence
 *
 * If this optimization is working correctly, the first two tests
 * should take nearly the same amount of time, with the third test
 * running at about 1/10th the speed.
 */

#include "svga.h"
#include "gmr.h"
#include "screen.h"
#include "intr.h"
#include "screendraw.h"
#include "vmbackdoor.h"
#include "math.h"
#include "mt19937ar.h"

#define GMRID_SCREEN_DRAW  0
#define GMRID_NOISE        1

typedef struct {
   int numDMAs;
   Bool fencePerDMA;
   Bool finalFence;
   const char *text;
} TestInfo;

TestInfo testInfoArray[] = {
   {
      1, FALSE, TRUE,
      "One DMA followed by one Fence."
   },
   {
      10, FALSE, TRUE,
      "Ten DMAs followed by one Fence.\nShould be nearly the same as #1."
   },
   {
      10, TRUE, FALSE,
      "Ten DMAs, each followed by a Fence.\nShould take 10x as long as #1."
   },
};


/*
 * allocNoise --
 *
 *    Allocates a new GMR, and fills it with random noise.
 */

static void
allocNoise(void)
{
   const uint32 numPages = 500;
   const uint32 numWords = numPages * PAGE_SIZE / sizeof(uint32);

   PPN pages = GMR_DefineContiguous(GMRID_NOISE, numPages);
   uint32 *ptr = PPN_POINTER(pages);
   int i;

   init_genrand(0);

   for (i = 0; i < numWords; i++) {
      ptr[i] = genrand_int32();
   }
}

/*
 * prepareNoiseRect --
 *
 *    Prepare some noise as the source for a blit.
 *    This defines the GMRFB, and generates a random source origin.
 */

static void
prepareNoiseRect(SVGASignedPoint *origin)  // OUT
{
   const uint32 bytesPerLine = 512;
   static const SVGAGMRImageFormat format = {{{ 32, 24 }}};
   const SVGAGuestPtr gPtr = { GMRID_NOISE, 0 };
   const uint32 rand = genrand_int32();

   Screen_DefineGMRFB(gPtr, bytesPerLine, format);

   origin->x = rand & 0x7F;
   origin->y = (rand >> 8) & 0x7F;
}


/*
 * main --
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
   ScreenDraw_Init(GMRID_SCREEN_DRAW);
   allocNoise();

   /*
    * Define a screen.
     */

   SVGAScreenObject myScreen = {
      .structSize = sizeof(SVGAScreenObject),
      .id = 0,
      .flags = SVGA_SCREEN_HAS_ROOT | SVGA_SCREEN_IS_PRIMARY,
      .size = { 800, 600 },
      .root = { 0, 0 },
   };
   Screen_Define(&myScreen);

   /*
    * Draw some intro text.
    */

   ScreenDraw_SetScreen(myScreen.id, myScreen.size.width, myScreen.size.height);
   Console_Clear();
   ScreenDraw_Border(0, 0, myScreen.size.width, myScreen.size.height, 0xFF0000, 1);
   Console_WriteString("Screen DMA Coalescing test.\n"
                       "\n"
                       "This example demonstrates an optimization which "
                       "eliminates redundant DMA operations.\n"
                       "The three tests below each issue a different "
                       "combination of DMAs and Fences. The text\n"
                       "below explains the expected peformance of each test.\n");

   /*
    * Main loop. Alternate between the three tests, timing each.
    */

   while (1) {
      int testNum;
      const int numRepeats = 200;

      for (testNum = 0; testNum < arraysize(testInfoArray); testNum++) {
         TestInfo *testInfo = &testInfoArray[testNum];
         VMTime before, after;
         int repeat;

         SVGA_SyncToFence(SVGA_InsertFence());
         VMBackdoor_GetTime(&before);

         for (repeat = 0; repeat < numRepeats; repeat++) {
            int numDMA;
            SVGASignedPoint blitOrigin;

            /*
             * To be coalesced, the DMAs below need the same offset
             * between source and dest.
             */
            prepareNoiseRect(&blitOrigin);

            for (numDMA = 0; numDMA < testInfo->numDMAs; numDMA++) {

               const uint32 dmaWidth = 256;
               const uint32 dmaHeight = 256;
               const uint32 margin = 5;
               SVGASignedRect blitDest = { myScreen.size.width - margin - dmaWidth,
                                           myScreen.size.height - margin - dmaHeight,
                                           myScreen.size.width - margin,
                                           myScreen.size.height - margin };

               /*
                * We can redefine the GMRFB, but coalescing will only
                * occur if it's exactly the same for each DMA.
                */
               Screen_BlitFromGMRFB(&blitOrigin, &blitDest, myScreen.id);

               if (testInfo->fencePerDMA) {
                  SVGA_InsertFence();
               }
            }
            if (testInfo->finalFence) {
               SVGA_InsertFence();
            }
         }

         SVGA_SyncToFence(SVGA_InsertFence());
         VMBackdoor_GetTime(&after);

         Console_MoveTo(10, 150 + 120 * testNum);
         Console_Format("Test #%d: %s\n\nSpeed: %d us   ",
                        testNum + 1, testInfo->text,
                        VMBackdoor_TimeDiffUS(&before, &after) / numRepeats);
      }
   }

   return 0;
}
