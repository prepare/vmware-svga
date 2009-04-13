/*
 * PongOS v2.0
 *
 * Micah Dowty <micah@vmware.com>
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga.h"
#include "intr.h"
#include "io.h"
#include "timer.h"
#include "keyboard.h"
#include "vmbackdoor.h"

#define PONG_DOT_SIZE           8
#define PONG_DIGIT_PIXEL_SIZE   10
#define PONG_BG_COLOR           0x000000
#define PONG_SPRITE_COLOR       0xFFFFFF
#define PONG_PLAYFIELD_COLOR    0xAAAAAA
#define PONG_FRAME_RATE         60

#define MAX_DIRTY_RECTS         128
#define MAX_SPRITES             8

typedef struct {
   float x, y;
} Vector2;

typedef struct {
   int x, y, w, h;
} Rect;

typedef struct {
   Rect r;
   uint32 color;
} FillRect;

static struct {
   uint32 *buffer;
   Rect dirtyRects[MAX_DIRTY_RECTS];
   uint32 numDirtyRects;
} back;

static struct {
   FillRect paddles[2];
   FillRect ball;
   uint8 scores[2];

   float ballSpeed;
   float paddleVelocities[2];
   float paddlePos[2];
   Vector2 ballVelocity;
   Vector2 ballPos;

   Bool playfieldDirty;
} pong;


/*
 *-----------------------------------------------------------------------------
 *
 * Random32 --
 *
 *    "Random" number generator. To save code space, we actually just use
 *    the low bits of the TSC. This of course isn't actually random, but
 *    it's good enough for Pong.
 *
 *-----------------------------------------------------------------------------
 */

static uint32
Random32(void)
{
   uint64 t;

   __asm__ __volatile__("rdtsc" : "=A" (t));

   return (uint32)t;
}


/*
 *-----------------------------------------------------------------------------
 *
 * RectTestIntersection --
 *
 *    Returns TRUE iff two Rects intersect with each other.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
RectTestIntersection(Rect *a,  // IN
                     Rect *b)  // IN
{
   return !(a->x + a->w < b->x ||
            a->x > b->x + b->w ||
            a->y + a->h < b->y ||
            a->y > b->y + b->h);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BackFill --
 *
 *    Perform a color fill on the backbuffer.
 *
 *-----------------------------------------------------------------------------
 */

static void
BackFill(FillRect fr)   // IN
{
   int i, j;

   for (i = 0; i < fr.r.h; i++) {
      uint32 *line = &back.buffer[(fr.r.y + i) * gSVGA.width + fr.r.x];

      for (j = 0; j < fr.r.w; j++) {
         line[j] = fr.color;
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * BackMarkDirty --
 *
 *    Mark a region of the backbuffer as dirty. We'll copy it to the
 *    front buffer and ask the host to update it on the next
 *    BackUpdate().
 *
 *-----------------------------------------------------------------------------
 */

static void
BackMarkDirty(Rect rect)  // IN
{
   back.dirtyRects[back.numDirtyRects++] = rect;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BackUpdate --
 *
 *    Copy all dirty regions of the backbuffer to the frontbuffer, and
 *    send updates to the SVGA device. Clears the dirtyRects list.
 *
 *    For flow control, this also waits for the host to process the
 *    batch of updates we just queued into the FIFO.
 *
 *-----------------------------------------------------------------------------
 */

static void
BackUpdate()  // IN
{
   int rectNum;

   for (rectNum = 0; rectNum < back.numDirtyRects; rectNum++) {
      Rect rect = back.dirtyRects[rectNum];
      uint32 i, j;

      for (i = 0; i < rect.h; i++) {
         uint32 offset = (rect.y + i) * gSVGA.width + rect.x;
         uint32 *src = &back.buffer[offset];
         uint32 *dest = &((uint32*) gSVGA.fbMem)[offset];

         for (j = 0; j < rect.w; j++) {
            dest[j] = src[j];
         }
      }

      SVGA_Update(rect.x, rect.y, rect.w, rect.h);
   }

   back.numDirtyRects = 0;
   SVGA_SyncToFence(SVGA_InsertFence());
}


/*
 *-----------------------------------------------------------------------------
 *
 * PongDrawString --
 *
 *    Draw a string of digits, using our silly blocky font. The
 *    string's origin is the top-middle.
 *
 *-----------------------------------------------------------------------------
 */

static void
PongDrawString(uint32 x,         // IN
               uint32 y,         // IN
               const char *str,  // IN
               uint32 strLen)    // IN
{
   const int charW = 4;
   const int charH = 5;
   static const uint8 font[] = {

      0xF1,  // **** ...*
      0x91,  // *..* ...*
      0x91,  // *..* ...*
      0x91,  // *..* ...*
      0xF1,  // **** ...*

      0xFF,  // **** ****
      0x11,  // ...* ...*
      0xFF,  // **** ****
      0x81,  // *... ...*
      0xFF,  // **** ****

      0x9F,  // *..* ****
      0x98,  // *..* *...
      0xFF,  // **** ****
      0x11,  // ...* ...*
      0x1F,  // ...* ****

      0xFF,  // **** ****
      0x81,  // *... ...*
      0xF1,  // **** ...*
      0x91,  // *..* ...*
      0xF1,  // **** ...*

      0xFF,  // **** ****
      0x99,  // *..* *..*
      0xFF,  // **** ****
      0x91,  // *..* ...*
      0xF1,  // **** ...*
   };

   x -= (PONG_DIGIT_PIXEL_SIZE * (strLen * (charW + 1) - 1)) / 2;

   while (*str) {
      int digit = *str - '0';
      if (digit >= 0 && digit <= 9) {
         int i, j;

         for (j = 0; j < charH; j++) {
            for (i = 0; i < charW; i++) {
               if ((font[digit / 2 * 5 + j] << i) & (digit & 1 ? 0x08 : 0x80)) {
                  FillRect pixel = {
                     {x + i * PONG_DIGIT_PIXEL_SIZE,
                      y + j * PONG_DIGIT_PIXEL_SIZE,
                      PONG_DIGIT_PIXEL_SIZE,
                      PONG_DIGIT_PIXEL_SIZE},
                     PONG_PLAYFIELD_COLOR,
                  };
                  BackFill(pixel);
               }
            }
         }
      }
      x += PONG_DIGIT_PIXEL_SIZE * (charW + 1);
      str++;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * DecDigit --
 *
 *    Utility for extracting a decimal digit.
 *
 *-----------------------------------------------------------------------------
 */

static char
DecDigit(int i, int div, Bool blank)
{
   if (blank && i < div) {
      return ' ';
   }
   return (i / div) % 10 + '0';
}


/*
 *-----------------------------------------------------------------------------
 *
 * PongDrawPlayfield --
 *
 *    Redraw the playfield for Pong.
 *
 *-----------------------------------------------------------------------------
 */

static void
PongDrawPlayfield()
{
   int i;

   /*
    * Clear the screen
    */
   FillRect background = {
      {0, 0, gSVGA.width, gSVGA.height},
      PONG_BG_COLOR,
   };
   BackFill(background);

   /*
    * Draw the dotted dividing line
    */
   for (i = PONG_DOT_SIZE;
        i <= gSVGA.height - PONG_DOT_SIZE * 2;
        i += PONG_DOT_SIZE * 2) {
      FillRect dot = {
         {(gSVGA.width - PONG_DOT_SIZE) / 2, i,
          PONG_DOT_SIZE, PONG_DOT_SIZE},
         PONG_PLAYFIELD_COLOR,
      };
      BackFill(dot);
   }

   /*
    * Draw the score counters.
    *
    * sprintf() is big, so we'll format this the old-fashioned way.
    * Right-justify the left score, and left-justify the right score.
    */
   {
      char scoreStr[7] = "       ";
      char *p = scoreStr;

      *(p++) = DecDigit(pong.scores[0], 100, TRUE);
      *(p++) = DecDigit(pong.scores[0], 10, TRUE);
      *(p++) = DecDigit(pong.scores[0], 1, FALSE);
      p++;
      if (pong.scores[1] >= 100) {
         *(p++) = DecDigit(pong.scores[1], 100, TRUE);
      }
      if (pong.scores[1] >= 10) {
         *(p++) = DecDigit(pong.scores[1], 10, TRUE);
      }
      *(p++) = DecDigit(pong.scores[1], 1, FALSE);

      PongDrawString(gSVGA.width/2, PONG_DIGIT_PIXEL_SIZE,
                     scoreStr, sizeof scoreStr);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * PongDrawScreen --
 *
 *    Top-level redraw function for Pong. This does a lot of unnecessary
 *    drawing to the backbuffer, but we're careful to only send update
 *    rectangles for a few things:
 *
 *      - When the playfield changes, we update the entire screen.
 *      - Each sprite (the paddles and ball) gets two rectangles:
 *          - One for its new position
 *          - One for its old position
 *
 *    None of these rectangles are ever merged.
 *
 *-----------------------------------------------------------------------------
 */

static void
PongDrawScreen()
{
   PongDrawPlayfield();

   if (pong.playfieldDirty) {
      Rect r = {0, 0, gSVGA.width, gSVGA.height};
      BackMarkDirty(r);
      pong.playfieldDirty = FALSE;
   }

   /* Draw all sprites at the current positions */
   BackFill(pong.paddles[0]);
   BackMarkDirty(pong.paddles[0].r);
   BackFill(pong.paddles[1]);
   BackMarkDirty(pong.paddles[1].r);
   BackFill(pong.ball);
   BackMarkDirty(pong.ball.r);

   /* Commit this to the front buffer and the host's screen */
   BackUpdate();

   /* Make sure we erase all sprites at the current positions on the next frame */
   BackMarkDirty(pong.paddles[0].r);
   BackMarkDirty(pong.paddles[1].r);
   BackMarkDirty(pong.ball.r);
}


/*
 *-----------------------------------------------------------------------------
 *
 * PongLaunchBall --
 *
 *    Reset the ball position, and give it a random angle.
 *
 *-----------------------------------------------------------------------------
 */

static void
PongLaunchBall()
{
   /* sin() from 0 to PI/2 */
   static const float sineTable[64] = {
      0.000000, 0.024931, 0.049846, 0.074730, 0.099568, 0.124344, 0.149042, 0.173648,
      0.198146, 0.222521, 0.246757, 0.270840, 0.294755, 0.318487, 0.342020, 0.365341,
      0.388435, 0.411287, 0.433884, 0.456211, 0.478254, 0.500000, 0.521435, 0.542546,
      0.563320, 0.583744, 0.603804, 0.623490, 0.642788, 0.661686, 0.680173, 0.698237,
      0.715867, 0.733052, 0.749781, 0.766044, 0.781831, 0.797133, 0.811938, 0.826239,
      0.840026, 0.853291, 0.866025, 0.878222, 0.889872, 0.900969, 0.911506, 0.921476,
      0.930874, 0.939693, 0.947927, 0.955573, 0.962624, 0.969077, 0.974928, 0.980172,
      0.984808, 0.988831, 0.992239, 0.995031, 0.997204, 0.998757, 0.999689, 1.000000,
   };
   int t;
   float sinT, cosT;

   pong.ballPos.x = gSVGA.width / 2;
   pong.ballPos.y = gSVGA.height / 2;

   /* Limit the random angle to avoid those within 45 degrees of vertical */
   t = 32 + (Random32() & 31);

   sinT = sineTable[t];
   cosT = -sineTable[(t + 32) & 63];

   sinT *= pong.ballSpeed;
   cosT *= pong.ballSpeed;

   switch (Random32() & 3) {
   case 0:
      pong.ballVelocity.x = sinT;
      pong.ballVelocity.y = cosT;
      break;
   case 1:
      pong.ballVelocity.x = -sinT;
      pong.ballVelocity.y = cosT;
      break;
   case 2:
      pong.ballVelocity.x = -sinT;
      pong.ballVelocity.y = -cosT;
      break;
   case 3:
      pong.ballVelocity.x = sinT;
      pong.ballVelocity.y = -cosT;
      break;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * PongInit --
 *
 *    Initialize all game variables, including sprite location/size/color.
 *    Requires that SVGA has already been initialized.
 *
 *-----------------------------------------------------------------------------
 */

static void
PongInit()
{
   pong.scores[0] = 0;
   pong.scores[1] = 0;
   pong.playfieldDirty = TRUE;

   pong.paddlePos[0] = pong.paddlePos[1] = gSVGA.height / 2;

   pong.paddles[0].r.x = 10;
   pong.paddles[0].r.w = 16;
   pong.paddles[0].r.h = 64;
   pong.paddles[0].color = PONG_SPRITE_COLOR;

   pong.paddles[1].r.x = gSVGA.width - 16 - 10;
   pong.paddles[1].r.w = 16;
   pong.paddles[1].r.h = 64;
   pong.paddles[1].color = PONG_SPRITE_COLOR;

   pong.ball.r.w = 16;
   pong.ball.r.h = 16;
   pong.ball.color = PONG_SPRITE_COLOR;

   pong.ballSpeed = 400;
   PongLaunchBall();
}


/*
 *-----------------------------------------------------------------------------
 *
 * PongUpdateMotion --
 *
 *    Perform motion updates for the ball and paddles. This includes
 *    bounce/goal detection.
 *
 *-----------------------------------------------------------------------------
 */

static void
PongUpdateMotion(float dt)  // IN
{
   int playableWidth = gSVGA.width - pong.ball.r.w;
   int playableHeight = gSVGA.height - pong.ball.r.h;
   int i;

   pong.ballPos.x += pong.ballVelocity.x * dt;
   pong.ballPos.y += pong.ballVelocity.y * dt;

   for (i = 0; i < 2; i++) {
      int pos = pong.paddlePos[i] + pong.paddleVelocities[i] * dt;
      pong.paddlePos[i] = MIN(gSVGA.height - pong.paddles[i].r.h, MAX(0, pos));
      pong.paddles[i].r.y = (int)pong.paddlePos[i];
   }

   if (pong.ballPos.x >= playableWidth) {
      /* Goal off the right edge */
      pong.scores[0]++;
      pong.playfieldDirty = TRUE;
      PongLaunchBall();
   }

   if (pong.ballPos.x <= 0) {
      /* Goal off the left edge */
      pong.scores[1]++;
      pong.playfieldDirty = TRUE;
      PongLaunchBall();
   }

   if (pong.ballPos.y >= playableHeight) {
      /* Bounce off the bottom edge */
      pong.ballVelocity.y = -pong.ballVelocity.y;
      pong.ballPos.y = playableHeight - (pong.ballPos.y - playableHeight);
   }

   if (pong.ballPos.y <= 0) {
      /* Bounce off the top edge */
      pong.ballVelocity.y = -pong.ballVelocity.y;
      pong.ballPos.y = -pong.ballPos.y;
   }

   pong.ballPos.y = MIN(playableHeight, pong.ballPos.y);
   pong.ballPos.y = MAX(0, pong.ballPos.y);

   pong.ball.r.x = (int)pong.ballPos.x;
   pong.ball.r.y = (int)pong.ballPos.y;

   /*
    * Lame collision detection between ball and paddles. Really we
    * should be testing the ball's entire path over this time step,
    * not just the ball's new position. Using the current
    * implementation, it's possible for the ball to move through a
    * paddle if it's going fast enough or our frame rate is slow
    * enough.
    */
   for (i = 0; i < 2; i++) {
      /*
       * Only bounce off the paddle when we're moving toward it, to
       * prevent the ball from getting stuck inside the paddle
       */
      if ((pong.paddles[i].r.x > gSVGA.width / 2) == (pong.ballVelocity.x > 0) &&
          RectTestIntersection(&pong.ball.r, &pong.paddles[i].r)) {
         /*
          * Boing! The ball bounces back, plus it gets a little spin
          * if the paddle itself was moving at the time.
          */
         pong.ballVelocity.x = -pong.ballVelocity.x;
         pong.ballVelocity.y += pong.paddleVelocities[i];
         pong.ballVelocity.y = MIN(pong.ballVelocity.y, pong.ballSpeed * 2);
         pong.ballVelocity.y = MAX(pong.ballVelocity.y, -pong.ballSpeed * 2);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * PongKeyboardPlayer --
 *
 *    A human player, using the up and down arrows on a keyboard.
 *
 *-----------------------------------------------------------------------------
 */

static void
PongKeyboardPlayer(int playerNum,   // IN
                   float maxSpeed,  // IN
                   float accel)     // IN
{
   float v = pong.paddleVelocities[playerNum];
   Bool up = Keyboard_IsKeyPressed(KEY_UP);
   Bool down = Keyboard_IsKeyPressed(KEY_DOWN);

   if (up && !down) {
      v -= accel;
   } else if (down && !up) {
      v += accel;
   } else {
      v = 0;
   }

   v = MIN(maxSpeed, MAX(-maxSpeed, v));
   pong.paddleVelocities[playerNum] = v;
}


/*
 *-----------------------------------------------------------------------------
 *
 * PongAbsMousePlayer --
 *
 *    A human player, controlled with the Y axis of the absolute mouse.
 *
 *-----------------------------------------------------------------------------
 */

static void
PongAbsMousePlayer(int playerNum)   // IN
{
   int currentY = pong.paddles[playerNum].r.y;
   int newY = currentY;
   VMMousePacket p;
   Bool mouseMoved = FALSE;

   while (VMBackdoor_MouseGetPacket(&p)) {
      newY = (p.y * gSVGA.height / 0xFFFF) - pong.paddles[playerNum].r.h / 2;
      newY = MAX(0, newY);
      newY = MIN(gSVGA.height - pong.paddles[playerNum].r.h, newY);
      mouseMoved = TRUE;
   }

   if (newY != currentY && mouseMoved) {
      pong.paddleVelocities[playerNum] = (newY - currentY) * (float)PONG_FRAME_RATE;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * PongComputerPlayer --
 *
 *    Simple computer player. Always moves its paddle toward the ball.
 *
 *-----------------------------------------------------------------------------
 */

static void
PongComputerPlayer(int playerNum,   // IN
                   float maxSpeed)  // IN
{
   int paddleCenter = pong.paddles[playerNum].r.y + pong.paddles[playerNum].r.h / 2;
   int ballCenter = pong.ball.r.y + pong.ball.r.h / 2;
   int distance = ballCenter - paddleCenter;

   pong.paddleVelocities[playerNum] = distance / (float)gSVGA.height * maxSpeed;
}


/*
 *-----------------------------------------------------------------------------
 *
 * main --
 *
 *    Initialization and main loop.
 *
 *-----------------------------------------------------------------------------
 */

void
main(void)
{
   Intr_Init();
   SVGA_Init();
   SVGA_SetMode(800, 600, 32);
   back.buffer = (uint32*) (gSVGA.fbMem + gSVGA.width * gSVGA.height * sizeof(uint32));

   Keyboard_Init();
   VMBackdoor_MouseInit(TRUE);
   PongInit();

   Timer_InitPIT(PIT_HZ / PONG_FRAME_RATE);
   Intr_SetMask(0, TRUE);

   while (1) {
      PongKeyboardPlayer(0, 1000, 50);
      PongAbsMousePlayer(0);
      PongComputerPlayer(1, 2000);

      PongUpdateMotion(1.0 / PONG_FRAME_RATE);
      PongDrawScreen();

      Intr_Halt();
   }
}
