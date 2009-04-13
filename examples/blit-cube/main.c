/*
 * SVGA3D example: Spinning cube, with various blit operations.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga3dutil.h"
#include "svga3dtext.h"
#include "matrix.h"
#include "math.h"

typedef struct {
   float position[3];
   float texcoord[2];
   float color[3];
} MyVertex;

static const MyVertex vertexData[] = {
   { {-1, -1, -1}, { 0, 0 }, {0.5, 0.5, 0.5} },  /* -X */
   { {-1, -1,  1}, { 0, 1 }, {1.0, 1.0, 1.0} },
   { {-1,  1, -1}, { 1, 0 }, {0.5, 0.5, 0.5} },
   { {-1,  1,  1}, { 1, 1 }, {1.0, 1.0, 1.0} },

   { { 1, -1, -1}, { 0, 0 }, {0.5, 0.5, 0.5} },  /* +X */
   { { 1, -1,  1}, { 0, 1 }, {1.0, 1.0, 1.0} },
   { { 1,  1, -1}, { 1, 0 }, {0.5, 0.5, 0.5} },
   { { 1,  1,  1}, { 1, 1 }, {1.0, 1.0, 1.0} },

   { {-1, -1, -1}, { 0, 0 }, {0.5, 0.5, 0.5} },  /* -Y */
   { {-1, -1,  1}, { 0, 1 }, {1.0, 1.0, 1.0} },
   { { 1, -1, -1}, { 1, 0 }, {0.5, 0.5, 0.5} },
   { { 1, -1,  1}, { 1, 1 }, {1.0, 1.0, 1.0} },

   { {-1,  1, -1}, { 0, 0 }, {0.5, 0.5, 0.5} },  /* +Y */
   { {-1,  1,  1}, { 0, 1 }, {1.0, 1.0, 1.0} },
   { { 1,  1, -1}, { 1, 0 }, {0.5, 0.5, 0.5} },
   { { 1,  1,  1}, { 1, 1 }, {1.0, 1.0, 1.0} },

   { {-1, -1, -1}, { 0, 0 }, {0.5, 0.5, 0.5} },  /* -Z */
   { {-1,  1, -1}, { 0, 1 }, {1.0, 1.0, 1.0} },
   { { 1, -1, -1}, { 1, 0 }, {0.5, 0.5, 0.5} },
   { { 1,  1, -1}, { 1, 1 }, {1.0, 1.0, 1.0} },

   { {-1, -1,  1}, { 0, 0 }, {0.5, 0.5, 0.5} },  /* +Z */
   { {-1,  1,  1}, { 0, 1 }, {1.0, 1.0, 1.0} },
   { { 1, -1,  1}, { 1, 0 }, {0.5, 0.5, 0.5} },
   { { 1,  1,  1}, { 1, 1 }, {1.0, 1.0, 1.0} },
};

#define QUAD(a,b,c,d) a, b, d, d, c, a

static const uint16 indexData[] = {
   QUAD(0,  1,  2,  3),  // -X
   QUAD(4,  5,  6,  7),  // +X
   QUAD(8,  9,  10, 11), // -Y
   QUAD(12, 13, 14, 15), // +Y
   QUAD(16, 17, 18, 19), // -Z
   QUAD(20, 21, 22, 23), // +Z
};

#undef QUAD

const uint32 numTriangles = sizeof indexData / sizeof indexData[0] / 3;
uint32 vertexSid, indexSid, textureSid;
Matrix perspectiveMat;
FPSCounterState gFPS;
VMMousePacket lastMouseState;

/*
 * render --
 *
 *   Set up render state, and draw our cube scene from static index
 *   and vertex buffers.
 *
 *   This render state only needs to be set each frame because
 *   SVGA3DText_Draw() changes it.
 */

void
render(void)
{
   SVGA3dTextureState *ts;
   SVGA3dRenderState *rs;
   SVGA3dVertexDecl *decls;
   SVGA3dPrimitiveRange *ranges;
   static Matrix view;

   Matrix_Copy(view, gIdentityMatrix);
   Matrix_Scale(view, 0.5, 0.5, 0.5, 1.0);

   if (lastMouseState.buttons & VMMOUSE_LEFT_BUTTON) {
      Matrix_RotateX(view, lastMouseState.y *  0.0001);
      Matrix_RotateY(view, lastMouseState.x * -0.0001);
   } else {
      Matrix_RotateX(view, 30.0 * M_PI / 180.0);
      Matrix_RotateY(view, gFPS.frame * 0.01f);
   }

   Matrix_Translate(view, 0, 0, 2);

   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_VIEW, view);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_WORLD, gIdentityMatrix);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_PROJECTION, perspectiveMat);

   SVGA3D_BeginSetRenderState(CID, &rs, 4);
   {
      rs[0].state     = SVGA3D_RS_BLENDENABLE;
      rs[0].uintValue = FALSE;

      rs[1].state     = SVGA3D_RS_ZENABLE;
      rs[1].uintValue = TRUE;

      rs[2].state     = SVGA3D_RS_ZWRITEENABLE;
      rs[2].uintValue = TRUE;

      rs[3].state     = SVGA3D_RS_ZFUNC;
      rs[3].uintValue = SVGA3D_CMP_LESS;
   }
   SVGA_FIFOCommitAll();

   SVGA3D_BeginSetTextureState(CID, &ts, 10);
   {
      ts[0].stage = 0;
      ts[0].name  = SVGA3D_TS_BIND_TEXTURE;
      ts[0].value = textureSid;

      ts[1].stage = 0;
      ts[1].name  = SVGA3D_TS_COLOROP;
      ts[1].value = SVGA3D_TC_MODULATE;

      ts[2].stage = 0;
      ts[2].name  = SVGA3D_TS_COLORARG1;
      ts[2].value = SVGA3D_TA_TEXTURE;

      ts[3].stage = 0;
      ts[3].name  = SVGA3D_TS_COLORARG2;
      ts[3].value = SVGA3D_TA_DIFFUSE;

      ts[4].stage = 0;
      ts[4].name  = SVGA3D_TS_ALPHAOP;
      ts[4].value = SVGA3D_TC_SELECTARG1;

      ts[5].stage = 0;
      ts[5].name  = SVGA3D_TS_ALPHAARG1;
      ts[5].value = SVGA3D_TA_DIFFUSE;

      ts[6].stage = 0;
      ts[6].name  = SVGA3D_TS_MINFILTER;
      ts[6].value = SVGA3D_TEX_FILTER_LINEAR;

      ts[7].stage = 0;
      ts[7].name  = SVGA3D_TS_MAGFILTER;
      ts[7].value = SVGA3D_TEX_FILTER_LINEAR;

      ts[8].stage = 0;
      ts[8].name  = SVGA3D_TS_ADDRESSU;
      ts[8].value = SVGA3D_TEX_ADDRESS_WRAP;

      ts[9].stage = 0;
      ts[9].name  = SVGA3D_TS_ADDRESSV;
      ts[9].value = SVGA3D_TEX_ADDRESS_WRAP;
   }
   SVGA_FIFOCommitAll();

   SVGA3D_BeginDrawPrimitives(CID, &decls, 3, &ranges, 1);
   {
      decls[0].identity.type = SVGA3D_DECLTYPE_FLOAT3;
      decls[0].identity.usage = SVGA3D_DECLUSAGE_POSITION;
      decls[0].array.surfaceId = vertexSid;
      decls[0].array.stride = sizeof(MyVertex);
      decls[0].array.offset = offsetof(MyVertex, position);

      decls[1].identity.type = SVGA3D_DECLTYPE_FLOAT2;
      decls[1].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
      decls[1].array.surfaceId = vertexSid;
      decls[1].array.stride = sizeof(MyVertex);
      decls[1].array.offset = offsetof(MyVertex, texcoord);

      decls[2].identity.type = SVGA3D_DECLTYPE_FLOAT3;
      decls[2].identity.usage = SVGA3D_DECLUSAGE_COLOR;
      decls[2].array.surfaceId = vertexSid;
      decls[2].array.stride = sizeof(MyVertex);
      decls[2].array.offset = offsetof(MyVertex, color);

      ranges[0].primType = SVGA3D_PRIMITIVE_TRIANGLELIST;
      ranges[0].primitiveCount = numTriangles;
      ranges[0].indexArray.surfaceId = indexSid;
      ranges[0].indexArray.stride = sizeof(uint16);
      ranges[0].indexWidth = sizeof(uint16);
   }
   SVGA_FIFOCommitAll();
}


/*
 * defineCheckerboard --
 *
 *    Create a new checkerboard texture of the specified size.
 */

uint32
defineCheckerboard(uint32 width, uint32 height)
{
   uint32 *buffer;
   int i, j;
   SVGAGuestPtr gPtr;
   uint32 size = width * height * sizeof *buffer;

   uint32 sid = SVGA3DUtil_DefineSurface2D(width, height, SVGA3D_A8R8G8B8);

   buffer = SVGA3DUtil_AllocDMABuffer(size, &gPtr);

   for (j = 0; j < height; j++) {
      for (i = 0; i < width; i++) {
         *buffer = (i + j) & 1 ? 0xFFFFFFFF : 0x00000000;
         buffer++;
      }
   }

   SVGA3DUtil_SurfaceDMA2D(sid, &gPtr, SVGA3D_WRITE_HOST_VRAM, width, height);

   return sid;
}


/*
 * main --
 *
 *    Our example's entry point, invoked directly by the bootloader.
 */

int
main(void)
{
   uint32 texSize = 256;
   uint32 checkerSid;

   SVGA3DUtil_InitFullscreen(CID, 1024, 768);
   SVGA3DText_Init();

   vertexSid = SVGA3DUtil_DefineStaticBuffer(vertexData, sizeof vertexData);
   indexSid = SVGA3DUtil_DefineStaticBuffer(indexData, sizeof indexData);

   textureSid = SVGA3DUtil_DefineSurface2D(texSize, texSize, SVGA3D_A8R8G8B8);
   checkerSid = defineCheckerboard(texSize, texSize);

   Matrix_Perspective(perspectiveMat, 45.0f,
                      gSVGA.width / (float)gSVGA.height, 0.1f, 100.0f);

   while (1) {
      if (SVGA3DUtil_UpdateFPSCounter(&gFPS)) {
         Console_Clear();
         Console_Format(
            "VMware SVGA3D Example:\n"
            "Spinning cube blitter test: \n"
            "  - SurfaceStretchBlt from back buffer to cube texture\n"
            "  - SurfaceCopy from cube texture to back buffer\n"
            "  - Checkerboard pattern in bottom left\n"
            "\n"
            "Verify performance and correctness with all blitter implementations.\n"
            "\n"
            "%s",
            gFPS.text);

         SVGA3DText_Update();
         VMBackdoor_VGAScreenshot();
      }

      while (VMBackdoor_MouseGetPacket(&lastMouseState));

      SVGA3DUtil_ClearFullscreen(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                                 0x6666dd, 1.0f, 0);
      render();
      SVGA3DText_Draw();

      /* Surface copy from cube texture to the lower-right corner of the back buffer */
      {
         SVGA3dSurfaceImageId src = { textureSid };
         SVGA3dCopyBox *boxes;

         SVGA3D_BeginSurfaceCopy(&src, &gFullscreen.colorImage, &boxes, 1);
         boxes[0].w = texSize;
         boxes[0].h = texSize;
         boxes[0].d = 1;
         boxes[0].x = gFullscreen.screen.w - texSize;
         boxes[0].y = gFullscreen.screen.h - texSize;

         SVGA_FIFOCommitAll();
      }

      /*
       * We're displaying the checkerboard texture in the lower-left
       * corner of the back buffer.  This tests for subpixel alignment
       * errors within the blitter.
       *
       * Draw the top half with a regular blit, bottom half with a
       * stretch blit. You should see a contiguous checkerboard.
       */
      {
         SVGA3dSurfaceImageId src = { checkerSid };
         SVGA3dCopyBox *boxes;
         SVGA3dBox boxSrc = { 0 };
         SVGA3dBox boxDest = { 0 };

         SVGA3D_BeginSurfaceCopy(&src, &gFullscreen.colorImage, &boxes, 1);
         boxes[0].w = texSize;
         boxes[0].h = texSize/2;
         boxes[0].d = 1;
         boxes[0].y = gFullscreen.screen.h - texSize;
         SVGA_FIFOCommitAll();

         boxSrc.w = texSize;
         boxSrc.y = texSize/2;
         boxSrc.h = texSize/2;
         boxSrc.d = 1;

         boxDest.w = texSize;
         boxDest.y = gFullscreen.screen.h - texSize/2;
         boxDest.h = texSize/2;
         boxDest.d = 1;

         SVGA3D_SurfaceStretchBlt(&src, &gFullscreen.colorImage, &boxSrc, &boxDest,
                                  SVGA3D_STRETCH_BLT_LINEAR);
      }

      SVGA3DUtil_PresentFullscreen();

      /* Stretch blit from back buffer to cube */
      {
         SVGA3dSurfaceImageId dest = { textureSid };
         SVGA3dBox boxSrc = { 0 };
         SVGA3dBox boxDest = { 0 };

         boxSrc.w = gFullscreen.screen.w;
         boxSrc.h = gFullscreen.screen.h;
         boxSrc.d = 1;

         boxDest.w = texSize;
         boxDest.h = texSize;
         boxDest.d = 1;

         SVGA3D_SurfaceStretchBlt(&gFullscreen.colorImage, &dest, &boxSrc, &boxDest,
                                  SVGA3D_STRETCH_BLT_LINEAR);
      }
   }

   return 0;
}
