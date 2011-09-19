/*
 * This is an example of dynamic multi-monitor support in the VMware
 * SVGA device, using the Screen Object extension. This demo lets you
 * interactively modify, create, and destroy screens.
 */

#include "svga.h"
#include "gmr.h"
#include "screen.h"
#include "intr.h"
#include "screendraw.h"
#include "math.h"
#include "keyboard.h"
#include "vmbackdoor.h"
#include "timer.h"

SVGAScreenObject screens[9];
int currentScreen;
int screenContainingCursor;
volatile uint32 keyBuffer;
int movementAmount = 10;
SVGASignedPoint screenCursorPos;
SVGASignedRect boundingRect;

#define POLL_RATE_HZ   60
#define CURSOR_WIDTH   35
#define CURSOR_HEIGHT  40


/*
 * updateBoundingRect --
 *
 *    Recalculate the bounding rectangle of all rooted screens and
 *    store the result in boundingRect.
 */

void
updateBoundingRect(void)
{
   uint32 id;

   boundingRect.left = boundingRect.top = 0x7fffffff;
   boundingRect.right = boundingRect.bottom = 0x80000000;

   for (id = 0; id < arraysize(screens); id++) {
      SVGAScreenObject *screen = &screens[id];

      if (screen->id == SVGA_ID_INVALID ||
          !(screen->flags & SVGA_SCREEN_HAS_ROOT)) {
         continue;
      }

      boundingRect.left = MIN(boundingRect.left, screen->root.x);
      boundingRect.top = MIN(boundingRect.top, screen->root.y);
      boundingRect.right = MAX(boundingRect.right,
                               (int32)(screen->root.x + screen->size.width));
      boundingRect.bottom = MAX(boundingRect.bottom,
                                (int32)(screen->root.y + screen->size.height));
   }
}


/*
 * defineAlphaArrow --
 *
 *    Defines an alpha cursor (a pointing arrow).
 */

void
defineAlphaArrow(void)
{
   static const SVGAFifoCmdDefineAlphaCursor cursor = {
      .id = 0,
      .hotspotX = 1,
      .hotspotY = 1,
      .width = CURSOR_WIDTH,
      .height = CURSOR_HEIGHT,
   };

   static const uint32 data[] = {
#     include "rgba_arrow.h"
   };

   void *fifoData;

   SVGA_BeginDefineAlphaCursor(&cursor, &fifoData);
   memcpy(fifoData, data, sizeof data);
   SVGA_FIFOCommitAll();
}


/*
 * drawScreenBorder --
 *
 *    Redraw the border of a screen, indicating whether or not it's current.
 */

void
drawScreenBorder(const SVGAScreenObject *screen)
{
   const int t = 4;   // Thickness
   const uint32 color = currentScreen == screen->id ? 0xffffdd : 0x555555;
   const int w = screen->size.width;
   const int h = screen->size.height;

   if (screen->id != SVGA_ID_INVALID) {
      ScreenDraw_SetScreen(screen->id, screen->size.width, screen->size.height);
      ScreenDraw_Border(0, 0, w, h, color, t);
   }
}


/*
 * drawScreenText --
 *
 *    Draw informational text on a screen.
 */

void
drawScreenText(const SVGAScreenObject *screen)
{
   ScreenDraw_SetScreen(screen->id, screen->size.width, screen->size.height);
   Console_MoveTo(10, 10);
   Console_Format("Screen #%d\n"
                  "%dx%d at (%d,%d)      \n",
                  screen->id, screen->size.width, screen->size.height,
                  screen->root.x, screen->root.y);

   if (screen->id == screenContainingCursor) {
      Console_Format("Cursor: (%d,%d)         \n",
                     screenCursorPos.x, screenCursorPos.y);
   } else {
      Console_Format("                                     \n");
   }

   Console_Format("\n"
                  "1-%d or mouse click selects screen.\n"
                  "Arrow keys move screen.\n"
                  "'wasd' adjusts size.\n"
                  "'WASD' adjusts size without repaint.\n"
                  "Space bar toggles create/destroy.\n"
                  "\n"
                  "Moving %d pixels at a time.   \n"
                  "(Adjust with [ ] keys.)\n",
                  arraysize(screens),
                  movementAmount);
}


/*
 * paintScreen --
 *
 *    Draw informational text, a background, and a border to each
 *    screen.
 */

void
paintScreen(const SVGAScreenObject *screen)
{
   ScreenDraw_SetScreen(screen->id, screen->size.width, screen->size.height);
   Console_Clear();
   drawScreenText(screen);
   drawScreenBorder(screen);
}


/*
 * setCurrentScreen --
 *
 *    Switch to a new 'current' screen, and indicate it visually by
 *    repainting the border. If the new screen ID is bad, do nothing.
 */

void
setCurrentScreen(int nextScreen)
{
   if (nextScreen >= 0 && nextScreen < arraysize(screens)) {
      int prevScreen = currentScreen;
      currentScreen = nextScreen;

      drawScreenBorder(&screens[prevScreen]);
      drawScreenBorder(&screens[nextScreen]);
   }
}


/*
 * toggleScreenExistence --
 *
 *    Define or undefine the current screen. When we first define it,
 *    paint its contents.
 */

void
toggleScreenExistence(void)
{
   SVGAScreenObject *screen = &screens[currentScreen];

   if (screen->id == SVGA_ID_INVALID) {
      screen->id = currentScreen;
      // FIXME: Need to call Screen_Create
      Screen_Define(screen);
      paintScreen(screen);
   } else {
      Screen_Destroy(screen->id);
      screen->id = SVGA_ID_INVALID;
   }
}


/*
 * kbIRQ --
 *
 *    Keyboard IRQ handler. This runs with IRQs disabled, so we need
 *    to thunk this key back to the main loop before we do any SVGA
 *    drawing as a result.
 */

fastcall void
kbIRQ(KeyEvent *event)
{
   if (event->key && event->pressed) {
      keyBuffer = event->key;
   }
}


/*
 * kbHandler --
 *
 *    Keyboard event handler, running in the main loop.
 */

fastcall void
kbHandler(uint32 key)
{
   /* Digits select a screen */
   setCurrentScreen(key - '1');

   SVGAScreenObject *screen = &screens[currentScreen];

   if (key == ' ') {
      toggleScreenExistence();
      updateBoundingRect();
      return;
   }

   if (screen->id == SVGA_ID_INVALID) {
      return;
   }

   switch (key) {

   case '[':
      movementAmount = MAX(1, movementAmount - 1);
      drawScreenText(screen);
      break;

   case ']':
      movementAmount++;
      drawScreenText(screen);
      break;

   case KEY_LEFT:
      screen->root.x -= movementAmount;
      drawScreenText(screen);
      Screen_Define(screen);
      break;

   case KEY_RIGHT:
      screen->root.x += movementAmount;
      drawScreenText(screen);
      Screen_Define(screen);
      break;

   case KEY_UP:
      screen->root.y -= movementAmount;
      drawScreenText(screen);
      Screen_Define(screen);
      break;

   case KEY_DOWN:
      screen->root.y += movementAmount;
      drawScreenText(screen);
      Screen_Define(screen);
      break;

   case 'A':
      screen->size.width -= movementAmount;
      drawScreenText(screen);
      Screen_Define(screen);
      break;

   case 'D':
      screen->size.width += movementAmount;
      drawScreenText(screen);
      Screen_Define(screen);
      break;

   case 'W':
      screen->size.height -= movementAmount;
      drawScreenText(screen);
      Screen_Define(screen);
      break;

   case 'S':
      screen->size.height += movementAmount;
      drawScreenText(screen);
      Screen_Define(screen);
      break;

   case 'a':
      screen->size.width -= movementAmount;
      Screen_Define(screen);
      paintScreen(screen);
      break;

   case 'd':
      screen->size.width += movementAmount;
      Screen_Define(screen);
      paintScreen(screen);
      break;

   case 'w':
      screen->size.height -= movementAmount;
      Screen_Define(screen);
      paintScreen(screen);
      break;

   case 's':
      screen->size.height += movementAmount;
      Screen_Define(screen);
      paintScreen(screen);
      break;
   }

   updateBoundingRect();
}


/*
 * main --
 *
 *    Initialization and main loop.
 */

int
main(void)
{
   uint32 id;

   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);

   /*
    * TODO: We currently poll the mouse on a timer. We could instead
    * make this interrupt-driven, but that might need changes to the
    * PS/2 controller.
    */
   Timer_InitPIT(PIT_HZ / POLL_RATE_HZ);
   Intr_SetMask(PIT_IRQ, TRUE);
   Keyboard_Init();
   Keyboard_SetHandler(kbIRQ);
   VMBackdoor_MouseInit(TRUE);
   SVGA_Init();
   GMR_Init();
   Heap_Reset();
   SVGA_SetMode(0, 0, 32);
   Screen_Init();
   ScreenDraw_Init(0);


   /*
    * Initialize default parameters for each screen.  None of these
    * exist yet, and we indicate that by setting their ID to
    * SVGA_ID_INVALID.
    */

   for (id = 0; id < arraysize(screens); id++) {
      SVGAScreenObject *screen = &screens[id];

      screen->structSize = sizeof *screen;
      screen->id = SVGA_ID_INVALID;
      screen->flags = SVGA_SCREEN_HAS_ROOT;
      screen->size.width = 320;
      screen->size.height = 240;
      screen->root.x = 320 * id;
      screen->root.y = 0;
   }

   /*
    * Make the first screen primary, to work around bug 399528 in the
    * Workstation UI.
    */
   screens[0].flags |= SVGA_SCREEN_IS_PRIMARY;

   /*
    * Make a screen visible, so the user can see our instructions.
    */
   toggleScreenExistence();

   updateBoundingRect();

   defineAlphaArrow();

   /*
    * Main loop: Wait for keyboard interrupts, and handle keys by
    * pulling them out of eventBuffer.
    */

   while (1) {
      uint32 key = KEY_NONE;
      Bool needCursorUpdate = FALSE;
      static VMMousePacket mouseState;

      Intr_Halt();
      Atomic_Exchange(keyBuffer, key);
      if (key != KEY_NONE) {
         kbHandler(key);
      }
      while (VMBackdoor_MouseGetPacket(&mouseState)) {
         needCursorUpdate = TRUE;
      }

      if (needCursorUpdate) {
         SVGASignedPoint virtualCursorPos;
         Bool cursorOnScreen = FALSE;

         virtualCursorPos.x = boundingRect.left +
            (mouseState.x * (boundingRect.right - boundingRect.left)) / 65535;
         virtualCursorPos.y = boundingRect.top +
            (mouseState.y * (boundingRect.bottom - boundingRect.top)) / 65535;

         for (id = 0; id < arraysize(screens); id++) {
            SVGAScreenObject *screen = &screens[id];
            if (screen->id == SVGA_ID_INVALID ||
                !(screen->flags & SVGA_SCREEN_HAS_ROOT)) {
               continue;
            }

            if (screen->root.x <= virtualCursorPos.x &&
                screen->root.x + screen->size.width > virtualCursorPos.x &&
                screen->root.y <= virtualCursorPos.y &&
                screen->root.y + screen->size.height > virtualCursorPos.y) {

               cursorOnScreen = TRUE;
               screenCursorPos.x = virtualCursorPos.x - screen->root.x;
               screenCursorPos.y = virtualCursorPos.y - screen->root.y;

               if (mouseState.buttons & VMMOUSE_LEFT_BUTTON) {
                  setCurrentScreen(screen->id);
               }

               if (screenContainingCursor != screen->id) {
                  int oldScreenContainingCursor = screenContainingCursor;
                  screenContainingCursor = screen->id;
                  drawScreenText(&screens[oldScreenContainingCursor]);
               }
               drawScreenText(screen);
            }
         }

         SVGA_MoveCursor(cursorOnScreen,
                         screenCursorPos.x,
                         screenCursorPos.y,
                         screenContainingCursor);
      }
   }

   return 0;
}
