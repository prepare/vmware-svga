/*
 * This example illustrates annotated GMRFB-to-screen blits.
 * Annotations are a way to give the device extra information about
 * the data in a blit. In this case, we're guaranteeing that the
 * contents of the blit are the result of a fill or copy operation.
 *
 * This example cheats a bit, and it actually lies to the SVGA device
 * in its annotations. This lets you see whether the annotations are
 * working or not- if the host uses the blit data we supply, we'll see
 * one result. If it uses the annotation, we'll see something
 * different.
 *
 * This example requires SVGA Screen Object support.
 */

#include "svga.h"
#include "gmr.h"
#include "screen.h"
#include "intr.h"
#include "screen.h"
#include "screendraw.h"
#include "math.h"
#include "mt19937ar.h"
#include "timer.h"

#define GMRID_SCREEN_DRAW  0
#define GMRID_NOISE        1
#define FRAME_RATE         60
#define SCREEN_ID          0

static volatile uint32 timerTick;


/*
 * prepareNoiseRect --
 *
 *    Prepare some noise as the source for a blit.
 *    This defines the GMRFB, and generates a random source origin.
 */

static void
prepareNoiseRect(SVGASignedPoint *origin)  // OUT
{
   const uint32 bytesPerLine = 1024;
   static const SVGAGMRImageFormat format = {{{ 32, 24 }}};
   const SVGAGuestPtr gPtr = { GMRID_NOISE, 0 };
   const uint32 rand = genrand_int32();

   Screen_DefineGMRFB(gPtr, bytesPerLine, format);

   origin->x = rand & 0x7F;
   origin->y = (rand >> 8) & 0x7F;
}


/*
 * updateBorders --
 *
 *    Clean up after the previous frame of animation, and draw the
 *    white border around the current frame.
 */

static void
updateBorders(const SVGASignedRect *pOld,
              const SVGASignedRect *pNew)
{
   const uint32 color = 0;
   SVGASignedRect newRect = *pNew;

   /*
    * Outset each rectangle by one pixel, to make room for the border.
    */

   newRect.left--;
   newRect.right++;
   newRect.top--;
   newRect.bottom++;

   /*
    * Draw the white borders
    */

   ScreenDraw_Border(newRect.left, newRect.top, newRect.right, newRect.bottom,
                     0xFFFFFF, 1);

   /*
    * Clear any pixels from 'oldRect' that are not in 'newRect'.
    */

   if (pOld) {
      SVGASignedRect oldRect = *pOld;

      oldRect.left--;
      oldRect.right++;
      oldRect.top--;
      oldRect.bottom++;

      /* Clear the right edge */
      if (newRect.right < oldRect.right) {
         ScreenDraw_Rectangle(newRect.right, oldRect.top,
                              oldRect.right, oldRect.bottom,
                              color);
      }

      /* Clear the left edge */
      if (newRect.left > oldRect.left) {
         ScreenDraw_Rectangle(oldRect.left, oldRect.top,
                              newRect.left, oldRect.bottom,
                              color);
      }

      /* Clear the top edge */
      if (newRect.top > oldRect.top) {
         ScreenDraw_Rectangle(oldRect.left, oldRect.top,
                              oldRect.right, newRect.top,
                              color);
      }

      /* Clear the bottom edge */
      if (newRect.bottom < oldRect.bottom) {
         ScreenDraw_Rectangle(oldRect.left, newRect.bottom,
                              oldRect.right, oldRect.bottom,
                              color);
      }
   }
}


/*
 * updateFillRect --
 *
 *    Animate the test rectangle which demonstrates fill annotations.
 */

static void
updateFillRect(const SVGASignedRect *oldRect,
               const SVGASignedRect *newRect)
{
   SVGASignedPoint srcOrigin;
   static const SVGAColorBGRX color = {{{ 0xFF, 0xCC, 0xCC }}};  // Light blue

   prepareNoiseRect(&srcOrigin);

   Screen_AnnotateFill(color);
   Screen_BlitFromGMRFB(&srcOrigin, newRect, SCREEN_ID);

   updateBorders(oldRect, newRect);
}


/*
 * updateCopyRect --
 *
 *    Animate the test rectangle which demonstrates copy annotations.
 */

static void
updateCopyRect(const SVGASignedRect *oldRect,
               const SVGASignedRect *newRect)
{

   /*
    * The first time through, draw a checkerboard test pattern.
    * On subsequent frames, annotate a copy of it from the previous frame.
    */
   if (oldRect) {
      SVGASignedPoint noiseSrc, copySrc;

      prepareNoiseRect(&noiseSrc);

      copySrc.x = oldRect->left;
      copySrc.y = oldRect->top;

      Screen_AnnotateCopy(&copySrc, SCREEN_ID);
      Screen_BlitFromGMRFB(&noiseSrc, newRect, SCREEN_ID);

   } else {
      ScreenDraw_Checkerboard(newRect->left, newRect->top,
                              newRect->right, newRect->bottom);
   }

   updateBorders(oldRect, newRect);
}


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
 * timerISR --
 *
 *    Trivial timer ISR, just increments our tick counter.
 */

void
timerISR(int vector)
{
   timerTick++;
}


/*
 * main --
 *
 *    Initialization and main loop.
 */

int
main(void)
{
   uint32 frame = 0;
   uint32 lastTick = 0;

   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);

   Timer_InitPIT(PIT_HZ / FRAME_RATE);
   Intr_SetMask(PIT_IRQ, TRUE);
   Intr_SetHandler(IRQ_VECTOR(PIT_IRQ), timerISR);

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
      .id = SCREEN_ID,
      .flags = SVGA_SCREEN_HAS_ROOT | SVGA_SCREEN_IS_PRIMARY,
      .size = { 800, 600 },
      .root = { 0, 0 },
   };
   Screen_Define(&myScreen);

   /*
    * Draw some explanatory text.
    */

   char docString[] =
      "Annotated Blit Sample:"
      "\n\n"
      "You should see two moving rectangles. The left one is animated "
      "using a fill-annotated blit. The blit itself contains random "
      "noise, but the annotation is a blue fill. If your host is "
      "using the annotation, you will see the blue. If not, you'll "
      "see noise. Either one is correct, but it is often more efficient "
      "to use the fill."
      "\n\n"
      "The right one is a copy-annotated blit. The blit data is again "
      "random noise, and the copy is a screen-to-screen copy which "
      "moves the rectangle from its old position to the new position. "
      "We drew a checkerboard pattern to the screen once, and that "
      "pattern should be preserved indefinitely if the annotation is "
      "being executed correctly."
      "\n\n"
      "Both rectangles should have a 1-pixel solid white border, and "
      "in both cases we use a fill-annotated blit to clear the screen "
      "behind each rectangle. This annotation doesn't lie, its blit data "
      "matches the advertised fill color.";

   ScreenDraw_SetScreen(myScreen.id, myScreen.size.width, myScreen.size.height);
   Console_Clear();
   ScreenDraw_WrapText(docString, 770);
   Console_WriteString(docString);

   /*
    * Animate the two rectangles indefinitely, sleeping between frames.
    */

   while (1) {
      SVGASignedRect oldRect1, oldRect2;
      SVGASignedRect newRect1, newRect2;

      /*
       * Move them around in a circle.
       */

      float theta = frame * 0.01;

      newRect1.left = 190 + cosf(theta) * 60;
      newRect1.top = 350 + sinf(theta) * 60;
      newRect1.right = newRect1.left + 80;
      newRect1.bottom = newRect1.top + 120;

      newRect2.left = 530 + sinf(theta) * 60;
      newRect2.top = 350 + cosf(theta) * 60;
      newRect2.right = newRect2.left + 80;
      newRect2.bottom = newRect2.top + 120;

      /*
       * Update the position of each.
       */

      updateFillRect(frame ? &oldRect1 : NULL, &newRect1);
      updateCopyRect(frame ? &oldRect2 : NULL, &newRect2);

      oldRect1 = newRect1;
      oldRect2 = newRect2;

      /*
       * Wait for the next timer tick.
       */

      while (timerTick == lastTick) {
         Intr_Halt();
      }
      lastTick = timerTick;
      frame++;
   }

   return 0;
}
