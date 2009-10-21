/*
 * This is a test app for virtual hardware cursor overlays. It
 * provides a number of test cases which exercise and demonstrate the
 * various cursor modes.
 *
 * This example requires SVGA Screen Object support. (The cursor
 * functionality we're testing has nothing to do with Screen Object,
 * but Screen Object is required by our text drawing code...)
 */

#include "svga.h"
#include "gmr.h"
#include "screen.h"
#include "intr.h"
#include "screendraw.h"
#include "keyboard.h"
#include "vmbackdoor.h"
#include "timer.h"
#include "mt19937ar.h"
#include "math.h"


/*
 * Constants
 */

#define GMRID_SCREEN_DRAW  0
#define GMRID_NOISE        1
#define FRAME_RATE         60


/*
 * Global data
 */

int currentTest = -1;
const int testListX = 150;
const int testListY = 70;
const int testListItemHeight = 22;

SVGAScreenObject myScreen = {
   .structSize = sizeof(SVGAScreenObject),
   .id = 0,
   .flags = SVGA_SCREEN_HAS_ROOT | SVGA_SCREEN_IS_PRIMARY,
   .size = { 800, 600 },
   .root = { 150000, -0x20000000 },
};


/*
 * Test Cases
 */

void
testAlphaArrow(void)
{
   static const SVGAFifoCmdDefineAlphaCursor cursor = {
      .id = 0,
      .hotspotX = 1,
      .hotspotY = 1,
      .width = 36,
      .height = 51,
   };

   static const uint32 data[] = {
#     include "rgba_arrow.h"
   };

   void *fifoData;

   /*
    * Switch to 8-bit mode. Alpha cursors should work even if the
    * legacy framebuffer is in a low color depth.
    */
   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 8);

   SVGA_BeginDefineAlphaCursor(&cursor, &fifoData);
   memcpy(fifoData, data, sizeof data);
   SVGA_FIFOCommitAll();
}

void
testGradient(int size)
{
   const SVGAFifoCmdDefineAlphaCursor cursor = {
      .id = 0,
      .hotspotX = size / 2,
      .hotspotY = size / 2,
      .width = size,
      .height = size,
   };

   uint32 *data;
   int x, y;

   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 8);
   SVGA_BeginDefineAlphaCursor(&cursor, (void**) &data);

   for (y = 0; y < cursor.height; y++) {
      for (x = 0; x < cursor.width; x++) {
         uint8 alpha = y * 255 / cursor.height;

         /* Solid white, with pre-multiplied alpha: L = 255 * alpha / 255 */
         uint8 luma = alpha;

         *(data++) = (alpha << 24) | (luma << 16) | (luma << 8) | luma;
      }
   }

   SVGA_FIFOCommitAll();
}

void
testGradient64(void)
{
   testGradient(64);
}

void
testGradient180(void)
{
   testGradient(180);
}

void
testGradient256(void)
{
   testGradient(256);
}

void
testMonochrome(void)
{
   static const SVGAFifoCmdDefineCursor cursor = {
      .id = 0,
      .hotspotX = 24,
      .hotspotY = 24,
      .width = 48,
      .height = 48,
      .andMaskDepth = 1,
      .xorMaskDepth = 1,
   };

   static const uint8 data[] = {
#     include "beachball_mono.h"
   };

   void *andData, *xorData;

   /*
    * Switch to 32bpp mode, just because we can. Monochrome cursors
    * work in any framebuffer depth.
    */
   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 32);

   SVGA_BeginDefineCursor(&cursor, &andData, &xorData);
   memcpy(andData, data, sizeof data);
   SVGA_FIFOCommitAll();
}

void
testMonochromeXOR(void)
{
   static const SVGAFifoCmdDefineCursor cursor = {
      .id = 0,
      .hotspotX = 24,
      .hotspotY = 24,
      .width = 48,
      .height = 48,
      .andMaskDepth = 1,
      .xorMaskDepth = 1,
   };

   static const uint8 data[] = {
#     include "beachball_mono_xor.h"
   };

   void *andData, *xorData;

   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 32);
   SVGA_BeginDefineCursor(&cursor, &andData, &xorData);
   memcpy(andData, data, sizeof data);
   SVGA_FIFOCommitAll();
}

void
testMonochromeLarge(void)
{
   static const SVGAFifoCmdDefineCursor cursor = {
      .id = 0,
      .hotspotX = 50,
      .hotspotY = 50,
      .width = 100,
      .height = 98,
      .andMaskDepth = 1,
      .xorMaskDepth = 1,
   };

   static const uint8 data[] = {
#     include "chip_mono.h"
   };

   void *andData, *xorData;

   /*
    * Switch to 32bpp mode, just because we can. Monochrome cursors
    * work in any framebuffer depth.
    */
   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 32);

   SVGA_BeginDefineCursor(&cursor, &andData, &xorData);
   memcpy(andData, data, sizeof data);
   SVGA_FIFOCommitAll();
}

void
testANDXOR32(void)
{
   static const SVGAFifoCmdDefineCursor cursor = {
      .id = 0,
      .hotspotX = 16,
      .hotspotY = 16,
      .width = 32,
      .height = 32,
      .andMaskDepth = 32,
      .xorMaskDepth = 32
   };

   uint32 *andData, *xorData;
   int x, y;

   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 32);
   SVGA_BeginDefineCursor(&cursor, (void**) &andData, (void**) &xorData);

   for (y = 0; y < cursor.height; y++) {
      for (x = 0; x < cursor.width; x++) {

         *(andData++) = 0x808080;
         *(xorData++) = y * 127 / cursor.height;

      }
   }

   SVGA_FIFOCommitAll();
}

static const uint32 yellowCrabData[] = {
#   include "yellow_crab_rgba.h"
};

void
buildCrabANDMask(uint8 *andData)
{
   const uint32 *rgba = yellowCrabData;
   const int width = 48;
   const int height = 50;

   uint32 andPitch = ((width + 31) / 32) * 4;
   uint8 *andLine;
   int x, y;


   for (y = 0; y < height; y++) {
      andLine = andData;
      andData += andPitch;
      memset(andLine, 0, andPitch);

      for (x = 0; x < width; x++) {
         uint32 color = *(rgba++);

         *andLine <<= 1;

         if ((color & 0xFF000000) == 0) {
            /*
             * Transparent pixel
             */
            *andLine |= 1;
         }

         if ((x & 7) == 7) {
            andLine++;
         }
      }
   }
}

void
testCrabAlpha(void)
{
   static const SVGAFifoCmdDefineAlphaCursor cursor = {
      .id = 0,
      .hotspotX = 24,
      .hotspotY = 25,
      .width = 48,
      .height = 50,
   };

   void *fifoData;

   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 32);
   SVGA_BeginDefineAlphaCursor(&cursor, &fifoData);
   memcpy(fifoData, yellowCrabData, sizeof yellowCrabData);
   SVGA_FIFOCommitAll();
}

void
testCrabANDXOR32(void)
{
   static const SVGAFifoCmdDefineCursor cursor = {
      .id = 0,
      .hotspotX = 24,
      .hotspotY = 25,
      .width = 48,
      .height = 50,
      .andMaskDepth = 1,
      .xorMaskDepth = 32
   };

   uint8 *andData;
   uint32 *xorData;

   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 32);
   SVGA_BeginDefineCursor(&cursor, (void**) &andData, (void**) &xorData);

   buildCrabANDMask(andData);
   memcpy(xorData, yellowCrabData, sizeof yellowCrabData);

   SVGA_FIFOCommitAll();
}

void
testCrabANDXOR16(void)
{
   static const SVGAFifoCmdDefineCursor cursor = {
      .id = 0,
      .hotspotX = 24,
      .hotspotY = 25,
      .width = 48,
      .height = 50,
      .andMaskDepth = 1,
      .xorMaskDepth = 16
   };

   const uint32 *rgba = yellowCrabData;
   uint8 *andData;
   uint16 *xorData;
   int x, y;

   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 16);
   if (SVGA_ReadReg(SVGA_REG_DEPTH) != 16) {
      Console_Panic("Expected SVGA_REG_DEPTH == 16 for 16bpp mode");
   }

   SVGA_BeginDefineCursor(&cursor, (void**) &andData, (void**) &xorData);

   buildCrabANDMask(andData);

   for (y = 0; y < cursor.height; y++) {
      for (x = 0; x < cursor.width; x++) {
         uint32 color = *(rgba++);

         /*
          * Convert to RGB 5-6-5
          */

         uint8 r = (color >> 19) & 0x1F;
         uint8 g = (color >> 10) & 0x3F;
         uint8 b = (color >> 3) & 0x1F;

         *(xorData++) = (r << 11) | (g << 5) | b;
      }
   }

   SVGA_FIFOCommitAll();
}

void
testCrabANDXOR8(void)
{
   static const SVGAFifoCmdDefineCursor cursor = {
      .id = 0,
      .hotspotX = 24,
      .hotspotY = 25,
      .width = 48,
      .height = 50,
      .andMaskDepth = 1,
      .xorMaskDepth = 8
   };

   static const uint8 crabPixels[] = {
#     include "yellow_crab_256_pixels.h"
   };

   static const uint8 crabColormap[] = {
#     include "yellow_crab_256_colormap.h"
   };

   uint8 *andData, *xorData;
   int i;

   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 8);
   if (SVGA_ReadReg(SVGA_REG_PSEUDOCOLOR) != TRUE) {
      Console_Panic("Expected SVGA_REG_PSUEDOCOLOR == TRUE for 8bpp mode");
   }

   /*
    * Load the crab's colormap into the SVGA palette registers.
    */
   for (i = 0; i < arraysize(crabColormap); i++) {
      SVGA_WriteReg(SVGA_PALETTE_BASE + i, crabColormap[i]);
   }

   SVGA_BeginDefineCursor(&cursor, (void**) &andData, (void**) &xorData);
   buildCrabANDMask(andData);
   memcpy(xorData, crabPixels, sizeof crabPixels);
   SVGA_FIFOCommitAll();
}

void
createPaletteCursor(void)
{
   /*
    * Set up a cursor which shows every color in the palette.  It has
    * a 1-pixel border of color 255, but every other color forms a
    * 16x16 grid in which each square is a different color. Each grid
    * square is 3x3 pixels.
    *
    * We also take this opportunity to use an 8-bit AND mask, but it's
    * fully opaque (all zeroes).
    *
    * This does mean that to function correctly, this cursor needs
    * color 0 to be black. The SVGA device doesn't specify whether
    * AND/XOR masks are applied before or after pseudocolor emulation,
    * so we need to be okay with either.
    */

   static const SVGAFifoCmdDefineCursor cursor = {
      .id = 0,
      .hotspotX = 24,
      .hotspotY = 24,
      .width = 49,
      .height = 49,
      .andMaskDepth = 8,
      .xorMaskDepth = 8
   };

   uint8 *andData, *xorData;
   uint8 *line;
   int x, y;
   uint32 pitch = roundup(cursor.width, sizeof(uint32)) * sizeof(uint32);

   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 8);
   if (SVGA_ReadReg(SVGA_REG_PSEUDOCOLOR) != TRUE) {
      Console_Panic("Expected SVGA_REG_PSUEDOCOLOR == TRUE for 8bpp mode");
   }

   SVGA_BeginDefineCursor(&cursor, (void**) &andData, (void**) &xorData);
   memset(andData, 0, pitch * cursor.height);

   for (y = 0; y < cursor.height; y++) {
      line = xorData;
      xorData += pitch;

      for (x = 0; x < cursor.width; x++) {
         uint8 color;

         if (y == 0 || x == 0 || y == cursor.height - 1 || x == cursor.width - 1) {
            /* Border color */
            color = 0xFF;

         } else {
            int row = (y - 1) / 3;
            int column = (x - 1) / 3;

            color = row * 16 + column;
         }

         *(line++) = color;
      }
   }

   SVGA_FIFOCommitAll();
}

void
animatePalette(void)
{
   /*
    * Animate the palette. This is stolen from the vbe-palette test...
    */

   static int tick = 0;
   int i;

   /*
    * Let the phase of each color channel slowly drift around.
    */
   const float rPhase = tick * 0.001;
   const float gPhase = tick * 0.002;
   const float bPhase = tick * 0.003;

   /*
    * Animate every color except 0 (used for the AND mask) and 255
    * (for the border).
    */

   for (i = 1; i < 255; i++) {
      const int x = (i & 0x0F) - 3;
      const int y = (i >> 4) - 3;
      const float t = (x*x + y*y) * 0.05 + tick * 0.02;

      const uint8 r = sinf(t + rPhase) * 0x7f + 0x80;
      const uint8 g = sinf(t + gPhase) * 0x7f + 0x80;
      const uint8 b = sinf(t + bPhase) * 0x7f + 0x80;

      SVGA_WriteReg(SVGA_PALETTE_BASE + i * 3 + 0, r);
      SVGA_WriteReg(SVGA_PALETTE_BASE + i * 3 + 1, g);
      SVGA_WriteReg(SVGA_PALETTE_BASE + i * 3 + 2, b);
   }

   tick++;
}

void
blit32(const uint32 *src, int srcX, int srcY, int srcWidth,
       uint32 *dest, int destX, int destY, int destWidth,
       int copyWidth, int copyHeight)
{
   src += srcX + srcY * srcWidth;
   dest += destX + destY * destWidth;

   while (copyHeight--) {
      memcpy32(dest, src, copyWidth);
      src += srcWidth;
      dest += destWidth;
   }
}

void
testCursorAnim(void)
{
   /*
    * Animate a cursor image of a moon orbiting a planet.  The cursor
    * hotspot is always centered on the planet, but we dynamically
    * size the cursor according to the bounding box around the planet
    * and moon, and we dynamically adjust the hotspot to keep it
    * centered on the moon.
    */

   const int moonWidth = 10;
   const int planetWidth = 20;

   static int tick = 0;
   float angle = tick * 0.03;
   tick++;

   const int orbitRadius = 40;
   int orbitX = cosf(angle) * orbitRadius;
   int orbitY = sinf(angle) * orbitRadius;

   int width, height, planetX, planetY, moonX, moonY;

   if (orbitX >= 0) {
      width = planetWidth + orbitX;
      planetX = planetWidth/2;
      moonX = planetX + orbitX;
   } else {
      width = planetWidth - orbitX;
      moonX = planetWidth/2;
      planetX = moonX - orbitX;
   }

   if (orbitY >= 0) {
      height = planetWidth + orbitY;
      planetY = planetWidth/2;
      moonY = planetY + orbitY;
   } else {
      height = planetWidth - orbitY;
      moonY = planetWidth/2;
      planetY = moonY - orbitY;
   }

   const SVGAFifoCmdDefineAlphaCursor cursor = {
      .id = 0,
      .hotspotX = planetX,
      .hotspotY = planetY,
      .width = width,
      .height = height,
   };

   uint32 *image;

   static const uint32 planet[] = {
#     include "planet_rgba.h"
   };
   static const uint32 moon[] = {
#     include "moon_rgba.h"
   };

   SVGA_WriteReg(SVGA_REG_BITS_PER_PIXEL, 8);
   SVGA_BeginDefineAlphaCursor(&cursor, (void**) &image);
   memset32(image, 0, width * height);

   blit32(planet, 0, 0, planetWidth,
          image, planetX - planetWidth/2, planetY - planetWidth/2, width,
          planetWidth, planetWidth);

   blit32(moon, 0, 0, moonWidth,
          image, moonX - moonWidth/2, moonY - moonWidth/2, width,
          moonWidth, moonWidth);

   SVGA_FIFOCommitAll();
}


/*
 * gTestCases --
 *
 *    Master array of cursor test cases. Each one has a title and a
 *    function pointer.  The function is run once when a test mode is
 *    selected.
 */

struct {
   void (*fn)(void);
   const char *title;
   void (*animateFn)(void);
} gTestCases[] = {
   { testAlphaArrow, "Translucent arrow cursor (36x51)" },
   { testGradient64, "Gradient from transparent white to opaque white (64x64)" },
   { testGradient180, "Gradient from transparent white to opaque white (180x180)" },
   { testGradient256, "Gradient from transparent white to opaque white (256x256)" },
   { testMonochrome, "Monochrome beachball cursor (48x48)" },
   { testMonochromeXOR, "Monochrome beachball cursor with XOR pixels (48x48)" },
   { testMonochromeLarge, "Monochrome chip cursor (100x96)" },
   { testANDXOR32, "AND masks off 7 LSBs, XOR draws blue gradient (32x32)" },
   { testCrabAlpha, "Yellow crab, alpha blended (48x50)" },
   { testCrabANDXOR32, "Yellow crab, 1-bit AND, 32-bit XOR (48x50)" },
   { testCrabANDXOR16, "Yellow crab, 1-bit AND, 16-bit XOR (48x50)" },
   { testCrabANDXOR8, "Yellow crab, 1-bit AND, 8-bit XOR (48x50)" },
   { createPaletteCursor, "Palette animation, 8-bit AND/XOR (49x49)", animatePalette },
   { testCursorAnim, "Animated cursor (variable size and hotspot)", testCursorAnim }
};


/*
 * selectTest --
 *
 *    Switch to a new current test case. Hilight the new test, un-hilight
 *    the old one, and call the new test's function.
 */

void
selectTest(int newTest)
{
   if (newTest < 0) {
      newTest += arraysize(gTestCases);
   } else if (newTest >= arraysize(gTestCases)) {
      newTest -= arraysize(gTestCases);
   }

   if (currentTest != newTest) {

      if (currentTest >= 0) {
         ScreenDraw_Border(testListX,
                           testListY + testListItemHeight * currentTest,
                           myScreen.size.width - testListX,
                           testListY + testListItemHeight * (currentTest + 1),
                           0x000000, 2);
      }

      currentTest = newTest;

      ScreenDraw_Border(testListX,
                        testListY + testListItemHeight * currentTest,
                        myScreen.size.width - testListX,
                        testListY + testListItemHeight * (currentTest + 1),
                        0xFFFF00, 2);

      gTestCases[newTest].fn();
   }
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
 *
 *    Initialization and main loop. This reads a global array of test
 *    cases, and presents a menu which cycles through them. The main
 *    loop services keyboard and mouse input, and draws an animated test
 *    pattern for testing cursor interaction with the base layer.
 */

int
main(void)
{

   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);

   Timer_InitPIT(PIT_HZ / FRAME_RATE);
   Intr_SetMask(PIT_IRQ, TRUE);

   SVGA_Init();
   GMR_Init();

   Keyboard_Init();
   VMBackdoor_MouseInit(TRUE);
   Heap_Reset();
   SVGA_SetMode(0, 0, 32);
   Screen_Init();
   ScreenDraw_Init(GMRID_SCREEN_DRAW);
   allocNoise();
   Screen_Define(&myScreen);

   /*
    * Draw the menu of test modes.
    */

   ScreenDraw_SetScreen(myScreen.id, myScreen.size.width, myScreen.size.height);
   Console_Clear();
   ScreenDraw_Border(0, 0, myScreen.size.width, myScreen.size.height, 0x808080, 1);

   Console_WriteString("Cursor tests:\n"
                       "Select with up/down arrows. "
                       "Move cursor with mouse or WASD keys.\n");

   for (currentTest = 0; currentTest < arraysize(gTestCases); currentTest++) {
      Console_MoveTo(testListX + 2, testListY + testListItemHeight * currentTest + 2);
      Console_Format("%d. %s", currentTest + 1, gTestCases[currentTest].title);
   }

   /*
    * Draw a white square on the right side of the menu, so we can see
    * what the cursors look like on a solid white background.
    */

   ScreenDraw_Rectangle(myScreen.size.width - testListX + 10,
                        testListY,
                        myScreen.size.width - 10,
                        myScreen.size.height - 10,
                        0xFFFFFF);

   /*
    * Main loop.
    */

   selectTest(0);

   while (1) {
      int prevTest = currentTest - 1;
      int nextTest = currentTest + 1;
      static VMMousePacket mouseState;
      const int kbdMouseSpeed = 100;
      Bool needCursorUpdate = FALSE;

      while (Keyboard_IsKeyPressed(KEY_UP)) {
         selectTest(prevTest);
      }

      while (Keyboard_IsKeyPressed(KEY_DOWN)) {
         selectTest(nextTest);
      }

      while (VMBackdoor_MouseGetPacket(&mouseState)) {
         needCursorUpdate = TRUE;
      }

      if (Keyboard_IsKeyPressed('w')) {
         mouseState.y -= kbdMouseSpeed;
         needCursorUpdate = TRUE;
      }

      if (Keyboard_IsKeyPressed('s')) {
         mouseState.y += kbdMouseSpeed;
         needCursorUpdate = TRUE;
      }

      if (Keyboard_IsKeyPressed('a')) {
         mouseState.x -= kbdMouseSpeed;
         needCursorUpdate = TRUE;
      }

      if (Keyboard_IsKeyPressed('d')) {
         mouseState.x += kbdMouseSpeed;
         needCursorUpdate = TRUE;
      }

      if (needCursorUpdate) {
         /*
          * Send a cursor position update.  In order to test the SVGA
          * device's cursor screen ID support, we alternate between
          * sending cursor updates in virtual coordinates and in
          * screen-relative coordinates.
          *
          * If all is well, cursor movement should still be smooth. If
          * there's a bug in decoding, the cursor will jitter as it's
          * moved, or the cursor may flicker if it's moved over an
          * animated region like the static bar on the left side of
          * the screen.
          */

         /*
          * Fixed-point to pixels.
          */
         SVGASignedPoint pixelLocation = {
            mouseState.x * myScreen.size.width / 65535,
            mouseState.y * myScreen.size.height / 65535
         };

         uint32 screenId;
         static Bool toggle;

         if (toggle) {
            screenId = SVGA_ID_INVALID;
            pixelLocation.x += myScreen.root.x;
            pixelLocation.y += myScreen.root.y;
            toggle = FALSE;
         } else {
            screenId = myScreen.id;
            toggle = TRUE;
         }

         SVGA_MoveCursor(TRUE, pixelLocation.x, pixelLocation.y, screenId);
      }

      /*
       * Draw some noise on the side of the screen, for testing overlay compositing.
       */

      SVGASignedPoint srcOrigin;
      SVGASignedRect noiseRect = {
         10, testListY, testListX - 10, myScreen.size.height - 10,
      };

      prepareNoiseRect(&srcOrigin);
      Screen_BlitFromGMRFB(&srcOrigin, &noiseRect, myScreen.id);

      /*
       * Some tests are animated...
       */

      if (gTestCases[currentTest].animateFn) {
         gTestCases[currentTest].animateFn();
      }

      /*
       * Wait for the next frame.
       */

      Intr_Halt();
   }

   return 0;
}
