/*
 * Test clipping for Present and Surface-to-Screen blits.
 *
 * This example requires SVGA Screen Object and SVGA3D support.
 */

#include "svga.h"
#include "svga3d.h"
#include "svga3dutil.h"
#include "matrix.h"
#include "math.h"
#include "gmr.h"
#include "screen.h"
#include "intr.h"
#include "screendraw.h"
#include "console_vga.h"

/*
 * 3D Rendering Definitions
 */

typedef struct {
   float position[3];
   uint32 color;
} MyVertex;

typedef struct {
   int            numRects;
   SVGASignedRect rects[2048];
} ClipBuffer;

uint32 vertexSid, indexSid;
ClipBuffer circles[2];

const int surfWidth = 224;
const int surfHeight = 168;

SVGA3dSurfaceImageId colorImage;
SVGA3dSurfaceImageId depthImage;

static const MyVertex vertexData[] = {
   { {-1, -1, -1}, 0xffffff },
   { {-1, -1,  1}, 0xffff00 },
   { {-1,  1, -1}, 0xff00ff },
   { {-1,  1,  1}, 0xff0000 },
   { { 1, -1, -1}, 0x00ffff },
   { { 1, -1,  1}, 0x00ff00 },
   { { 1,  1, -1}, 0x0000ff },
   { { 1,  1,  1}, 0x000000 },
};

static const uint16 indexData[] = {
   0, 1, 1, 3, 3, 2, 2, 0,   // -X
   4, 5, 5, 7, 7, 6, 6, 4,   // +X
   0, 4,
   1, 5,
   2, 6,
   3, 7,
};

const uint32 numLines = arraysize(indexData) / 2;


/*
 * initScreens --
 *
 *    Set up our Screen Objects, and label them.
 */

static void
initScreens(void)
{
   static SVGAScreenObject screen = {
      .structSize = sizeof(SVGAScreenObject),
      .id = 0,
      .flags = SVGA_SCREEN_HAS_ROOT | SVGA_SCREEN_IS_PRIMARY,
      .size = { 1024, 768 },
      .root = { 1000, 2000 },
   };

   Screen_Create(&screen);

   ScreenDraw_SetScreen(screen.id, screen.size.width, screen.size.height);
   Console_Clear();

   Console_Format("Surface-to-Screen Blit Clipping Test\n");
   ScreenDraw_Border(0, 0, screen.size.width, screen.size.height, 0xFF0000, 1);

   Console_MoveTo(20, 45);
   Console_Format("Stair-step clipping (small tiles)");

   Console_MoveTo(20, 245);
   Console_Format("Top/bottom halves swapped");

   Console_MoveTo(20, 445);
   Console_Format("Scaled bottom half, with hole");

   Console_MoveTo(350, 65);
   Console_Format("Zoomed to 1.5x full screen, two circular clip regions");

   Console_MoveTo(5, 660);
   Console_Format("Stair-step, clipped against screen edges");
}


/*
 * presentWithClipBuf --
 *
 *    Present our surface to the screen, with clipping data from a ClipBuffer.
 *
 *    The supplied ClipBuffer is always in screen coordinates. We
 *    convert them into dest-relative coordinates for the
 *    surface-to-screen blit.
 */

static void
presentWithClipBuf(ClipBuffer *buf, int dstL, int dstT, int dstR, int dstB)
{
   SVGASignedRect srcRect = { 0, 0, surfWidth, surfHeight };
   SVGASignedRect dstRect = { dstL, dstT, dstR, dstB };
   SVGASignedRect *clip;
   int i;

   SVGA3D_BeginBlitSurfaceToScreen(&colorImage, &srcRect, 0,
                                   &dstRect, &clip, buf->numRects);

   for (i = 0; i < buf->numRects; i++) {
      clip->left = buf->rects[i].left - dstL;
      clip->top = buf->rects[i].top - dstT;
      clip->right = buf->rects[i].right - dstL;
      clip->bottom = buf->rects[i].bottom - dstT;
      clip++;
   }

   SVGA_FIFOCommitAll();
}


/*
 * prepareCircle --
 *
 *    Prepare a ClipBuffer with a circular clip region, and draw an
 *    outline around the region.
 */

static void
prepareCircle(ClipBuffer *buf, int centerX, int centerY, int radius)
{
   int r, i;

   i = 0;
   for (r = -radius; r <= radius; r++) {
      int chordRadius = __builtin_sqrtf(radius * radius - r * r) + 0.5f;
      SVGASignedRect *rect = &buf->rects[i++];

      rect->left = centerX - chordRadius;
      rect->top = centerY - r;
      rect->right = centerX + chordRadius;
      rect->bottom = centerY - r + 1;

      ScreenDraw_Rectangle(rect->left - 1, rect->top - 1,
                           rect->right + 1, rect->bottom + 1,
                           0xffffff);
   }

   buf->numRects = i;
}


/*
 * presentStairStep --
 *
 *    Use a non-scaled Present to draw many small square tiles, and
 *    clip the edge to a stair-step pattern. This tests performance
 *    for large numbers of clip rectangles, and it will make any edge
 *    artifacts very noticeable.
 *
 * This is a set of copyrects where all of the sources line up,
 * so it is also expressable as a clip rectangle.
 */

static void
presentStairStep(int xOffset, int yOffset)
{
   const int gridSize = 16;
   const int numRects = (gridSize + 1) * gridSize / 2;
   const int squareWidth = surfWidth / gridSize;
   const int squareHeight = surfHeight / gridSize;
   int x, y, i;
   SVGA3dCopyRect *cr;

   SVGA3D_BeginPresent(colorImage.sid, &cr, numRects);
   i = 0;
   for (x = 0; x < gridSize; x++) {
      for (y = 0; y < gridSize; y++) {
         if (x + y < gridSize) {

            cr[i].srcx = x * squareWidth;
            cr[i].srcy = y * squareHeight;
            cr[i].x = cr[i].srcx + xOffset;
            cr[i].y = cr[i].srcy + yOffset;
            cr[i].w = squareWidth;
            cr[i].h = squareHeight;
            i++;
         }
      }
   }
   if (i != numRects) {
      SVGA_Panic("Incorrect numRects in present()");
   }
   SVGA_FIFOCommitAll();
}


/*
 * present --
 *
 *    Copy our rendered cube to the screen. This is where we test clipping.
 */

static void
present(void)
{
   /*
    * Main stair-step unscaled present test.
    */
   presentStairStep(1020, 2065);

   /*
    * Another non-scaled present, this time using the copyrects in a
    * way which is not also expressable as a clip rectangle. In this
    * case, we're using one Present to split the image in half (top
    * and bottom) and reverse the two halves.
    */
   {
      SVGA3dCopyRect *cr;

      SVGA3D_BeginPresent(colorImage.sid, &cr, 2);

      cr[0].srcx = 0;
      cr[0].srcy = surfHeight / 2;
      cr[0].x = 1020;
      cr[0].y = 2265;
      cr[0].w = surfWidth;
      cr[0].h = surfHeight / 2;

      cr[1].srcx = 0;
      cr[1].srcy = 0;
      cr[1].x = 1020;
      cr[1].y = 2265 + surfHeight / 2;
      cr[1].w = surfWidth;
      cr[1].h = surfHeight / 2;

      SVGA_FIFOCommitAll();
   }

   /*
    * A fairly normal scaled blit. This one is only slightly scaled, unlike the
    * large one below- so it may be easier to see a different class of bugs.
    * For clipping, we remove a hole from the center of the image.
    *
    * We also test source clipping by displaying the bottom half.
    */
   {
      SVGASignedRect *clip;

      SVGASignedRect srcRect = { 0, surfHeight/2, surfWidth, surfHeight };
      SVGASignedRect dstRect = { 20, 465, 325, 655 };

      SVGA3D_BeginBlitSurfaceToScreen(&colorImage, &srcRect, 0,
                                      &dstRect, &clip, 4);

      // Top
      clip[0].left = 0;
      clip[0].top = 0;
      clip[0].right = 445;
      clip[0].bottom = 75;

      // Bottom
      clip[1].left = 0;
      clip[1].top = 115;
      clip[1].right = 445;
      clip[1].bottom = 330;

      // Left
      clip[2].left = 0;
      clip[2].top = 75;
      clip[2].right = 63;
      clip[2].bottom = 115;

      // Right
      clip[3].left = 242;
      clip[3].top = 75;
      clip[3].right = 305;
      clip[3].bottom = 115;

      SVGA_FIFOCommitAll();
   }

   /*
    * Stair-step, clipped against the bottom and left sides of the screen.
    */
   presentStairStep(1000 - surfHeight/2, 2000 + 768 - surfHeight/2);

   /*
    * Scaled circles. We scale these asymmetrically, to about 1.5x the
    * size of the screen.
    */
   {
      int i;

      for (i = 0; i < arraysize(circles); i++) {
         presentWithClipBuf(&circles[i], -500, -300, 1300, 1000);
      }
   }
}


/*
 * setup3D --
 *
 *    Allocate 3D resources.
 */

static void
setup3D(void)
{
   colorImage.sid = SVGA3DUtil_DefineSurface2D(surfWidth, surfHeight, SVGA3D_X8R8G8B8);
   depthImage.sid = SVGA3DUtil_DefineSurface2D(surfWidth, surfHeight, SVGA3D_Z_D16);

   SVGA3D_DefineContext(CID);

   vertexSid = SVGA3DUtil_DefineStaticBuffer(vertexData, sizeof vertexData);
   indexSid = SVGA3DUtil_DefineStaticBuffer(indexData, sizeof indexData);
}


/*
 * drawCube --
 *
 *    Draw a spinning wireframe cube.
 */

static void
drawCube(void)
{
   static float angle = 0.5f;
   SVGA3dRect *rect;
   Matrix perspectiveMat;
   SVGA3dTextureState *ts;
   SVGA3dRenderState *rs;
   SVGA3dRect viewport = { 0, 0, surfWidth, surfHeight };

   SVGA3D_SetRenderTarget(CID, SVGA3D_RT_COLOR0, &colorImage);
   SVGA3D_SetRenderTarget(CID, SVGA3D_RT_DEPTH, &depthImage);

   SVGA3D_SetViewport(CID, &viewport);
   SVGA3D_SetZRange(CID, 0.0f, 1.0f);

   SVGA3D_BeginSetRenderState(CID, &rs, 5);
   {
      rs[0].state     = SVGA3D_RS_BLENDENABLE;
      rs[0].uintValue = FALSE;

      rs[1].state     = SVGA3D_RS_ZENABLE;
      rs[1].uintValue = TRUE;

      rs[2].state     = SVGA3D_RS_ZWRITEENABLE;
      rs[2].uintValue = TRUE;

      rs[3].state     = SVGA3D_RS_ZFUNC;
      rs[3].uintValue = SVGA3D_CMP_LESS;

      rs[4].state     = SVGA3D_RS_LIGHTINGENABLE;
      rs[4].uintValue = FALSE;
   }
   SVGA_FIFOCommitAll();

   SVGA3D_BeginSetTextureState(CID, &ts, 4);
   {
      ts[0].stage = 0;
      ts[0].name  = SVGA3D_TS_BIND_TEXTURE;
      ts[0].value = SVGA3D_INVALID_ID;

      ts[1].stage = 0;
      ts[1].name  = SVGA3D_TS_COLOROP;
      ts[1].value = SVGA3D_TC_SELECTARG1;

      ts[2].stage = 0;
      ts[2].name  = SVGA3D_TS_COLORARG1;
      ts[2].value = SVGA3D_TA_DIFFUSE;

      ts[3].stage = 0;
      ts[3].name  = SVGA3D_TS_ALPHAARG1;
      ts[3].value = SVGA3D_TA_DIFFUSE;
   }
   SVGA_FIFOCommitAll();

   /*
    * Draw a red border around the render target, to test edge
    * accuracy in Present.
    */
   SVGA3D_BeginClear(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                     0xFF0000, 1.0f, 0, &rect, 1);
   *rect = viewport;
   SVGA_FIFOCommitAll();

   /*
    * Draw the background color
    */
   SVGA3D_BeginClear(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                     0x336699, 1.0f, 0, &rect, 1);
   rect->x = viewport.x + 1;
   rect->y = viewport.y + 1;
   rect->w = viewport.w - 2;
   rect->h = viewport.h - 2;
   SVGA_FIFOCommitAll();

   SVGA3dVertexDecl *decls;
   SVGA3dPrimitiveRange *ranges;
   Matrix view;

   Matrix_Copy(view, gIdentityMatrix);
   Matrix_Scale(view, 0.5, 0.5, 0.5, 1.0);
   Matrix_RotateX(view, 30.0 * M_PI / 180.0);
   Matrix_RotateY(view, angle);
   Matrix_Translate(view, 0, 0, 2.2);

   angle += 0.02;

   Matrix_Perspective(perspectiveMat, 45.0f, 4.0f / 3.0f, 0.1f, 100.0f);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_WORLD, gIdentityMatrix);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_PROJECTION, perspectiveMat);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_VIEW, view);

   SVGA3D_BeginDrawPrimitives(CID, &decls, 2, &ranges, 1);
   {
      decls[0].identity.type = SVGA3D_DECLTYPE_FLOAT3;
      decls[0].identity.usage = SVGA3D_DECLUSAGE_POSITION;
      decls[0].array.surfaceId = vertexSid;
      decls[0].array.stride = sizeof(MyVertex);
      decls[0].array.offset = offsetof(MyVertex, position);

      decls[1].identity.type = SVGA3D_DECLTYPE_D3DCOLOR;
      decls[1].identity.usage = SVGA3D_DECLUSAGE_COLOR;
      decls[1].array.surfaceId = vertexSid;
      decls[1].array.stride = sizeof(MyVertex);
      decls[1].array.offset = offsetof(MyVertex, color);

      ranges[0].primType = SVGA3D_PRIMITIVE_LINELIST;
      ranges[0].primitiveCount = numLines;
      ranges[0].indexArray.surfaceId = indexSid;
      ranges[0].indexArray.stride = sizeof(uint16);
      ranges[0].indexWidth = sizeof(uint16);
   }
   SVGA_FIFOCommitAll();
}


/*
 * main --
 *
 *    Initialization, main loop.
 */

int
main(void)
{
   static FPSCounterState fps;
   uint32 frameFence = 0;
   uint32 nextFence;

   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);
   SVGA_Init();
   GMR_Init();
   Heap_Reset();
   SVGA_SetMode(0, 0, 32);
   SVGA3D_Init();
   Screen_Init();
   ScreenDraw_Init(0);

   initScreens();
   setup3D();

   /*
    * One big circle, and a smaller one that overlaps the top-right
    * corner.  (This tests positive and negative clipping extremes.)
    */
   prepareCircle(&circles[0], 650, 400, 300);
   prepareCircle(&circles[1], 1000, 50, 250);

   while (1) {
      if (SVGA3DUtil_UpdateFPSCounter(&fps)) {
         Console_MoveTo(900, 730);
         Console_Format("%s    ", fps.text);
      }

      drawCube();

      /*
       * Flow control- one frame in the FIFO at a time.
       */
      nextFence = SVGA_InsertFence();
      SVGA_SyncToFence(frameFence);
      frameFence = nextFence;

      present();
   }

   return 0;
}

