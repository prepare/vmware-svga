/*
 * SVGA3D example: Spinning cube, with static vertex/index buffers.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga3dutil.h"
#include "svga3dtext.h"
#include "matrix.h"
#include "math.h"
#include "keyboard.h"
#include "apm.h"

typedef struct {
   float position[3];
   uint32 color;
} MyVertex;

static const MyVertex vertexData[] = {
   { {-1, -1, -1}, 0xFFFFFF },
   { {-1, -1,  1}, 0xFFFF00 },
   { {-1,  1, -1}, 0xFF00FF },
   { {-1,  1,  1}, 0xFF0000 },
   { { 1, -1, -1}, 0x00FFFF },
   { { 1, -1,  1}, 0x00FF00 },
   { { 1,  1, -1}, 0x0000FF },
   { { 1,  1,  1}, 0x000000 },
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

const uint32 numTriangles = sizeof indexData / sizeof indexData[0] / 3;
uint32 vertexSid, indexSid;
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

   Matrix_Translate(view, 0, 0, 3);

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
 * main --
 *
 *    Our example's entry point, invoked directly by the bootloader.
 */

int
main(void)
{
   SVGA3DUtil_InitFullscreen(CID, 800, 600);
   SVGA3DText_Init();
   Keyboard_Init();
   APM_Init();

   vertexSid = SVGA3DUtil_DefineStaticBuffer(vertexData, sizeof vertexData);
   indexSid = SVGA3DUtil_DefineStaticBuffer(indexData, sizeof indexData);

   Matrix_Perspective(perspectiveMat, 45.0f,
                      gSVGA.width / (float)gSVGA.height, 0.1f, 100.0f);

   while (!Keyboard_IsKeyPressed(KEY_ESCAPE)) {

      if (SVGA3DUtil_UpdateFPSCounter(&gFPS)) {
         Console_Clear();
         Console_Format("VMware SVGA3D Example:\n"
                        "Spinning cube with static vertex and index buffer.\n"
                        "Drag with left mouse button to rotate.\n"
                        "Press ESC to exit.\n"
                        "\n%s",
                        gFPS.text);
         SVGA3DText_Update();
         VMBackdoor_VGAScreenshot();
      }

      while (VMBackdoor_MouseGetPacket(&lastMouseState));

      SVGA3DUtil_ClearFullscreen(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                                 0x113366, 1.0f, 0);
      render();
      SVGA3DText_Draw();
      SVGA3DUtil_PresentFullscreen();
   }

   APM_SetPowerState(POWER_OFF);
   return 0;
}
