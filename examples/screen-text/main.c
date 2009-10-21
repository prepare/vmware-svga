/*
 * Simple example for text rendering in the ScreenDraw utility
 * library, and a stress-test for GMRFB-to-screen blits.
 *
 * This example requires SVGA Screen Object support.
 */

#include "svga.h"
#include "gmr.h"
#include "screen.h"
#include "intr.h"
#include "screendraw.h"
#include "math.h"


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
   ScreenDraw_Init(0);

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
    * Draw some text.
    */

   ScreenDraw_SetScreen(myScreen.id, myScreen.size.width, myScreen.size.height);
   Console_Clear();

   Console_WriteString(
"Hello, World!\n"
"\n"
"This is a text rendering demo which uses SVGA Screen Objects to draw\n"
"text without using any guest framebuffer at all.  We define a table of\n"
"font glyphs in a GMRFB, and blit those individually to a Screen Object.\n"
"\n"
"The red screen border should be 1 pixel on all sides. You should see text\n"
"below, clipped to the right and bottom edges of the screen. You should \n"
"also see a moving gradient right here, which can be used as a test for\n"
"tightly fenced rendering of many tiny GMRFB blits:\n"
"\n"
"\n"
"\n"
"\n"
"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Morbi mattis"
"gravida diam. Pellentesque tincidunt sem in nunc. Donec ultrices\n"
"tempor orci. Fusce auctor urna eget dui. Cum sociis natoque penatibus"
"et magnis dis parturient montes, nascetur ridiculus mus. Sed nec\n"
"arcu. Donec eget nisl. Maecenas eget arcu a arcu cursus adipiscing. In"
"hac habitasse platea dictumst. Nam in nibh. Morbi pede. Proin\n"
"ultrices.\n"
"\n"
"Aliquam sodales urna id sem. Nulla ultrices aliquam libero. Curabitur"
"faucibus. Integer nibh enim, scelerisque ac, tincidunt ac, scelerisque\n"
"quis, leo. Integer quis lectus sodales mi interdum cursus. Sed euismod"
"rutrum magna. Etiam eleifend ipsum eu mauris. Nullam nulla tellus,\n"
"mollis sed, varius at, ullamcorper non, augue. Integer ut arcu ut sem"
"lobortis ultricies. Nunc vel diam sed erat pretium tempus. Proin\n"
"bibendum. Integer nulla orci, pharetra sed, venenatis rutrum, cursus"
"a, eros. Aliquam nec lectus. Nulla blandit dolor bibendum lorem. In\n"
"posuere.\n"
"\n"
"Vivamus vel lacus nec nisi luctus sodales. In ullamcorper magna vitae"
"magna. Duis sit amet arcu. Suspendisse mollis purus quis neque. Donec\n"
"sagittis fringilla pede. Praesent sem diam, semper vel, dapibus at,"
"rhoncus in, velit. Vivamus ac est. Nullam mauris. Sed justo dolor,\n"
"sollicitudin id, viverra at, varius id, orci. Ut dapibus hendrerit"
"mi. Aliquam gravida. Praesent sit amet nunc. Praesent ac tortor eu\n"
"urna porttitor imperdiet. Phasellus dignissim tincidunt augue. Quisque"
"odio. Mauris quis ligula id metus posuere scelerisque. Phasellus\n"
"pede. Integer quis sem. Phasellus vitae odio.\n"
"\n"
"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nam"
"nisi. Proin sem. Phasellus malesuada augue vitae quam. Phasellus"
"lacinia porttitor ante. Curabitur leo erat, gravida sed, commodo eu,"
"imperdiet porta, risus. Suspendisse molestie tortor sed odio. Nam\n"
"tellus. Etiam odio purus, pellentesque eget, sagittis eget, ornare a,"
"odio. Nunc iaculis. Duis sed velit et est ornare ornare. Curabitur id\n"
"nunc. Sed malesuada purus vitae libero.\n"
);

   /*
    * Draw a 1-pixel red border.
    */

   ScreenDraw_Border(0, 0, myScreen.size.width, myScreen.size.height, 0xFF0000, 1);

   /*
    * Endlessly animate the gradient as fast as we can.  This will be
    * sync'ed closely with the host, because we have to keep waiting
    * on our single DMA buffer to become available in
    * ScreenDraw_Rectangle. You wouldn't want to do this in a real
    * app, but it's a good stress-test for DMA operations.
    */

   while (1) {
      static uint32 tick = 0;
      tick++;

      const int width = 10;
      const float rPhase = tick * 0.001;
      const float gPhase = tick * 0.002;
      const float bPhase = tick * 0.003;

      int x;

      for (x=50; x<400; x+=width) {
         const float t = x * 0.05 + tick * 0.02;

         const uint8 r = sinf(t + rPhase) * 0x60 + 0x80;
         const uint8 g = sinf(t + gPhase) * 0x60 + 0x80;
         const uint8 b = sinf(t + bPhase) * 0x60 + 0x80;

         const uint32 color = (r << 16) | (g << 8) | b;

         ScreenDraw_Rectangle(x, 210, x+width, 240, color);
      }
   }

   return 0;
}
