/*
 * This is a test which demonstrates many features of the Screen
 * Object rendering model:
 *
 *   - GMR-to-screen blits from multiple GMRFBs onto multiple screens
 *   - SVGA3D-to-screen blits, with scaling, onto multiple screens
 *   - screen-to-GMR blits
 *   - Video Overlay with GMRs and Screen Objects
 *
 * For a complete test of Screen Object, several other tests are also
 * necessary:
 *
 *   - Per-screen cursors (screen-multimon test)
 *   - Screen creation, destruction, and redefinition (screen-multimon)
 *   - Checkpointing
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
#include "datafile.h"

/*
 * 3D Rendering Definitions
 */

typedef struct {
   float position[3];
   uint32 color;
} MyVertex;

uint32 vertexSid, indexSid;

const int surfWidth = 1024;
const int surfHeight = 512;

SVGA3dSurfaceImageId colorImage;
SVGA3dSurfaceImageId depthImage;

static const MyVertex vertexData[] = {
   { {-1, -1, -1}, 0xFFFFFFFF },
   { {-1, -1,  1}, 0xFFFFFF00 },
   { {-1,  1, -1}, 0xFFFF00FF },
   { {-1,  1,  1}, 0xFFFF0000 },
   { { 1, -1, -1}, 0xFF00FFFF },
   { { 1, -1,  1}, 0xFF00FF00 },
   { { 1,  1, -1}, 0xFF0000FF },
   { { 1,  1,  1}, 0xFF000000 },
};

#define QUAD(a,b,c,d) a, b, d, d, c, a
static const uint16 indexData[] = {
   QUAD(0,1,2,3), // -X
   QUAD(4,5,6,7), // +X
   QUAD(0,1,4,5), // -Y
   QUAD(2,3,6,7), // +Y
   QUAD(0,2,4,6), // -Z
   QUAD(1,3,5,7), // +Z
};
#undef QUAD

const uint32 numTriangles = arraysize(indexData) / 3;


/*
 * initScreens --
 *
 *    Set up our Screen Objects, and label them.
 */

void
initScreens(void)
{
   int i;

   /*
    * Define two screens:
    *
    *   +-------+
    *   |0      |
    *   |       |
    *   +--+----+-+
    *      |1     |
    *      +------+
    *
    * The screen 0 is 799x405, the screen 1 is 600x200.
    * Neither screen is rooted at 0,0.
    */

   SVGAScreenObject screens[] = {
      {
         .structSize = sizeof(SVGAScreenObject),
         .id = 0,
         .flags = SVGA_SCREEN_HAS_ROOT | SVGA_SCREEN_IS_PRIMARY,
         .size = { 799, 405 },
         .root = { -1234, 5678 },
      },
      {
         .structSize = sizeof(SVGAScreenObject),
         .id = 1,
         .flags = SVGA_SCREEN_HAS_ROOT,
         .size = { 600, 200 },
         .root = { -1234 + 302, 5678 + 405 },
      },
   };

   for (i = 0; i < arraysize(screens); i++) {
      SVGAScreenObject *screen = &screens[i];

      Screen_Create(screen);

      ScreenDraw_SetScreen(screen->id, screen->size.width, screen->size.height);
      Console_Clear();
      Console_MoveTo(5, screen->size.height - 20);
      Console_Format("Screen #%d\n", screen->id);
      ScreenDraw_Border(0, 0, screen->size.width, screen->size.height, 0xFF0000, 1);
   }
}


/*
 * initOverlays --
 *
 *    Set up a video overlay using a non-default screen ID and a
 *    system memory GMR. These are both new features included in
 *    the Screen Object extension.
 */

void
initOverlays(void)
{
   /*
    * A video test card, in UYVY format.
    *
    * It's a 720x576 pixel 4:3 aspect test card designed by Barney
    * Wol. (http://www.barney-wol.net/testpatterns)
    *
    * Decompress it into a new system memory GMR.
    */
   DECLARE_DATAFILE(testCardFile, wols4x3_yuv_z);

   const uint32 gmrId = 1;
   const int videoWidth = 720;
   const int videoHeight = 576;
   const int videoBytes = videoWidth * videoHeight * 2;
   const int videoPages = (videoBytes + PAGE_MASK) / PAGE_SIZE;

   PPN videoFirstPage = GMR_DefineContiguous(gmrId, videoPages);
   DataFile_Decompress(testCardFile, PPN_POINTER(videoFirstPage), videoBytes);

   /*
    * Display the video using Screen 1's coordinate system, but
    * actually have it overlap the right edge of the line segment that
    * screens 0 and 1 share.
    */

   SVGAOverlayUnit overlay = {
      .enabled = TRUE,
      .format = VMWARE_FOURCC_UYVY,
      .flags = SVGA_VIDEO_FLAG_COLORKEY,
      .colorKey = 0x000000,
      .width = videoWidth,
      .height = videoHeight,
      .srcWidth = videoWidth,
      .srcHeight = videoHeight,
      .pitches[0] = videoWidth * 2,

      .dataGMRId = gmrId,
      .dataOffset = 0,

      .dstX = 220,
      .dstY = -100,
      .dstWidth = 320,
      .dstHeight = 240,
      .dstScreenId = 1,
   };

   SVGA_VideoSetAllRegs(0, &overlay, SVGA_VIDEO_DST_SCREEN_ID);

   /*
    * XXX: Flush twice, to work around a bug in legacy backends.
    *      New SWB backends don't have this problem, but it's harmless.
    */
   SVGA_VideoFlush(0);
   SVGA_VideoFlush(0);

   /*
    * Draw a border around the video
    */
   ScreenDraw_SetScreen(1, 0, 0);
   ScreenDraw_Border(overlay.dstX - 1, overlay.dstY - 1,
                     overlay.dstX + overlay.dstWidth + 1,
                     overlay.dstY + overlay.dstHeight + 1,
                     0xFFFF00, 1);

   /*
    * Some text to explain the video
    */
   Console_MoveTo(overlay.dstX, overlay.dstY + overlay.dstHeight + 5);
   Console_Format("Video overlay on Screen 1, sysmem GMR");
}


/*
 * setup3D --
 *
 *    Allocate 3D resources that are used by draw3D() and cubeLoop().
 */

void
setup3D(void)
{
   /*
    * Set up some 3D resources: A color buffer, depth buffer, vertex
    * buffer, and index buffer. Load the VB and IB with data for a
    * cube with unique vertex colors.
    */

   colorImage.sid = SVGA3DUtil_DefineSurface2D(surfWidth, surfHeight, SVGA3D_X8R8G8B8);
   depthImage.sid = SVGA3DUtil_DefineSurface2D(surfWidth, surfHeight, SVGA3D_Z_D16);

   SVGA3D_DefineContext(CID);

   vertexSid = SVGA3DUtil_DefineStaticBuffer(vertexData, sizeof vertexData);
   indexSid = SVGA3DUtil_DefineStaticBuffer(indexData, sizeof indexData);
}


/*
 * drawCube --
 *
 *    Draw a cube with the specified viewport and background color.
 *    Every time we draw a cube, it rotates a little.
 */

void
drawCube(SVGA3dRect *viewport, uint32 bgColor)
{
   static float angle = 0.5f;
   SVGA3dRect *rect;
   Matrix perspectiveMat;
   SVGA3dTextureState *ts;
   SVGA3dRenderState *rs;

   SVGA3D_SetRenderTarget(CID, SVGA3D_RT_COLOR0, &colorImage);
   SVGA3D_SetRenderTarget(CID, SVGA3D_RT_DEPTH, &depthImage);

   SVGA3D_SetViewport(CID, viewport);
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
    * Clear unused areas of the surface to white
    */
   SVGA3D_BeginClear(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                     0xFFFFFFFF, 1.0f, 0, &rect, 1);
   rect->x = 0;
   rect->y = 0;
   rect->w = surfWidth;
   rect->w = surfHeight;
   SVGA_FIFOCommitAll();

   /*
    * Draw the background color
    */
   SVGA3D_BeginClear(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                     bgColor | 0x42000000, 1.0f, 0, &rect, 1);
   *rect = *viewport;
   SVGA_FIFOCommitAll();

   SVGA3dVertexDecl *decls;
   SVGA3dPrimitiveRange *ranges;
   Matrix view;

   Matrix_Copy(view, gIdentityMatrix);
   Matrix_Scale(view, 0.5, 0.5, 0.5, 1.0);
   Matrix_RotateX(view, 30.0 * M_PI / 180.0);
   Matrix_RotateY(view, angle);
   Matrix_Translate(view, 0, 0, 2.5);

   angle += 0.01;

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

      ranges[0].primType = SVGA3D_PRIMITIVE_TRIANGLELIST;
      ranges[0].primitiveCount = numTriangles;
      ranges[0].indexArray.surfaceId = indexSid;
      ranges[0].indexArray.stride = sizeof(uint16);
      ranges[0].indexWidth = sizeof(uint16);
   }
   SVGA_FIFOCommitAll();
}


/*
 * draw3D --
 *
 *    Set up some 3D resources, and draw a simple cube scene to
 *    several places on the frontbuffer. Each cube is drawn using a
 *    distinct background color.
 */

void
draw3D(void)
{
   /*
    * Scenes to render
    */
   const int dstWidth = 160;
   const int dstHeight = 120;

   struct {
      uint32 screenId;
      SVGA3dRect viewport;
      int x, y;
      uint32 bgColor;
      const char *label;
   } scenes[] = {
      /*
       * These scenes all use different but overlapping parts of the
       * render target, so it's quite obvious if the wrong image is
       * being displayed- either you'll see part of the frame
       * overwritten by a different frame, or the image will be
       * misaligned.
       */
      { 0, { 123, 65,  160, 120 }, 320, 20 + 145 * 0, 0x800000, "Red" },    // Non-scaled
      { 0, { 150, 82,  320, 240 }, 320, 20 + 145 * 1, 0x008000, "Green" },  // Scaled
      { 0, { 85,  32,  400, 300 }, 320, 20 + 145 * 2, 0x000080, "Blue" },   // Scaled
      { 0, { 160, 40,  160, 120 }, 320, 20 + 145 * 3, 0xae3aff, "Purple" }, // Non-scaled
   };

   int i;
   for (i = 0; i < arraysize(scenes); i++) {
      drawCube(&scenes[i].viewport, scenes[i].bgColor);

      SVGASignedRect srcRect = { scenes[i].viewport.x,
                                 scenes[i].viewport.y,
                                 scenes[i].viewport.x + scenes[i].viewport.w,
                                 scenes[i].viewport.y + scenes[i].viewport.h };
      SVGASignedRect dstRect = { scenes[i].x,
                                 scenes[i].y,
                                 scenes[i].x + dstWidth,
                                 scenes[i].y + dstHeight };

      /*
       * Draw some text underneath the Present. If you can see this
       * text, something is wrong.
       */
      ScreenDraw_SetScreen(scenes[i].screenId, 0, 0);
      Console_MoveTo(dstRect.left + 10, (dstRect.top + dstRect.bottom) / 2);
      Console_WriteString("XXX: 2D!");

      /*
       * Present our scene to the front buffer using the new surface-to-screen blit.
       */
      SVGA3D_BlitSurfaceToScreen(&colorImage, &srcRect,
                                 scenes[i].screenId, &dstRect);

      /*
       * Draw on the frontbuffer to label this scene, and draw
       * a border around it.
       */
      ScreenDraw_SetScreen(scenes[i].screenId, 0, 0);
      Console_MoveTo(dstRect.left, dstRect.top);
      Console_WriteString(scenes[i].label);
      ScreenDraw_Border(dstRect.left - 1, dstRect.top - 1,
                        dstRect.right + 1, dstRect.bottom + 1,
                        0xFFFF00, 1);
   }
}


/*
 * cubeLoop --
 *
 *    Draw a spinning cube in an infinite loop, using the same
 *    backbuffer and other resources that we used on the earlier
 *    cubes.
 */

void
cubeLoop(void)
{
   /* Draw a label */
   ScreenDraw_SetScreen(1, 0, 0);
   Console_MoveTo(230, -265);
   Console_WriteString("Spinning, Orange:");

   while (1) {
      SVGA3dRect viewport = { 0, 0, 160, 120 };
      SVGASignedRect srcRect = { 0, 0, 160, 120 };
      SVGASignedRect dstRect = { 230, -245, 230+160, -245+120 };

      drawCube(&viewport, 0xd9a54a);
      SVGA3D_BlitSurfaceToScreen(&colorImage, &srcRect, 1, &dstRect);
   }
}


/*
 * complementBytes --
 *
 *    For a range of bytes, do an in-place complement. Every bit is inverted.
 */

void
complementBytes(uint8 *ptr, uint32 count)
{
   while (count) {
      *ptr ^= 0xFF;
      ptr++;
      count--;
   }
}


/*
 * doBlits --
 *
 *    Use screen-to-GMR and GMR-to-screen blits in order to copy from
 *    one part of the screen to another. To prove that the guest can
 *    access the GMR data, we invert part of the image.
 */

void
doBlits()
{
   struct {
      SVGAGMRImageFormat format;
      int screenId;
      int srcx, srcy;
      int dstx, dsty;
      int labelx, labely;
      const char *label;
   } blits[] = {
      /*
       * Copy the red image, using 24-bit color, and label the bottom
       * half (inverse) as Cyan.
       */
      {
         {{{32, 24}}}, 0,
         319, 19,  // src
         134, 19,  // dest
         1, 103,   // label
         "Cyan",
      },

      /*
       * Copy the green image using 15-bit color, and label the bottom as pink.
       */
      {
         {{{16, 15}}}, 0,
         319, 164,  // src
         134, 164,  // dest
         1, 103,    // label
         "Pink",
      },

      /*
       * Try to copy the blue image. The screen-to-GMR blit will fail, since
       * we don't support screen-to-GMR blits that aren't contained within one
       * screen. So we'll end up drawing gray (the color we clear the buffer with)
       * and a ligher gray on the bottom where we invert.
       */
      {
         {{{32, 24}}}, 0,
         319, 309,  // src
         134, 309,  // dest
         1, 1,      // label
         "Gray, no cube",
      },

      /*
       * Now try to copy part of the video overlay from Screen 2.
       * We should get the text and border, but no video.
       */
      {
         {{{16, 16}}}, 1,
         200, 70,    // src
         230, -400,  // dest
         0, 0,       // label
         "Frame & Text, No Video",
      },
   };

   const int width = 162;
   const int height = 122;
   const SVGASignedPoint gmrOrigin = { 123, 4 };

   const int maxBytesPerLine = (width + gmrOrigin.x) * 4;
   const int numBytes = maxBytesPerLine * (height + gmrOrigin.y);
   const int offset = 12345;
   const int numPages = (offset + numBytes + PAGE_MASK) / PAGE_SIZE;

   const int gmrId = 2;
   PPN firstPage = GMR_DefineContiguous(gmrId, numPages);

   int i;
   for (i = 0; i < arraysize(blits); i++) {

      /* Wait for any previous DMA to finish */
      SVGA_SyncToFence(SVGA_InsertFence());

      /* Clear the buffer to 0x42 (will appear dark grey) */
      memset(PPN_POINTER(firstPage), 0x42, numPages * PAGE_SIZE);

      SVGAGuestPtr gPtr = { gmrId, offset };
      uint32 bytesPerLine = blits[i].format.bitsPerPixel * width / 8;
      Screen_DefineGMRFB(gPtr, bytesPerLine, blits[i].format);

      SVGASignedPoint srcPoint = { blits[i].srcx, blits[i].srcy };
      SVGASignedPoint dstPoint = { blits[i].dstx, blits[i].dsty };
      SVGASignedRect srcRect = { srcPoint.x, srcPoint.y,
                                 srcPoint.x + width, srcPoint.y + height };
      SVGASignedRect dstRect = { dstPoint.x, dstPoint.y,
                                 dstPoint.x + width, dstPoint.y + height };

      Screen_BlitToGMRFB(&gmrOrigin, &srcRect, blits[i].screenId);

      /* Wait for the DMA to finish */
      SVGA_SyncToFence(SVGA_InsertFence());

      /* Invert the colors in the bottom half of the image */
      uint8 *pixels = (offset +
                       bytesPerLine * (gmrOrigin.y + height/2) +
                       blits[i].format.bitsPerPixel * gmrOrigin.x / 8 +
                       (uint8*)PPN_POINTER(firstPage));
      complementBytes(pixels, bytesPerLine * height/2);

      /* Copy it back */
      Screen_BlitFromGMRFB(&gmrOrigin, &dstRect, blits[i].screenId);

      /* Draw a label for the bottom half */
      ScreenDraw_SetScreen(blits[i].screenId, 0, 0);
      Console_MoveTo(dstRect.left + blits[i].labelx, dstRect.top + blits[i].labely);
      Console_WriteString(blits[i].label);
   }
}


/*
 * main --
 *
 *    Initialization, main loop.
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
   SVGA3D_Init();
   Screen_Init();
   ScreenDraw_Init(0);

   initScreens();
   setup3D();
   draw3D();
   initOverlays();
   doBlits();
   cubeLoop();

   return 0;
}
