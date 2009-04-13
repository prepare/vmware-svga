/*
 * SVGA3D example: Bunnies.
 *
 * This example loads the famous Stanford Bunny model, and draws many
 * copies of it. This demonstrates large models and fixed-function
 * lighting.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga3dutil.h"
#include "svga3dtext.h"
#include "matrix.h"
#include "math.h"

DECLARE_DATAFILE(ibFile, bunny_ib_z);
DECLARE_DATAFILE(vbFile, bunny_vb_z);

uint32 vertexSid, indexSid;
uint32 ibSize, vbSize;
Matrix perspectiveMat;
FPSCounterState gFPS;


/*
 * setupFrame --
 *
 *    Set up render state that we load once per frame (because
 *    SVGA3DText clobbered it) and perform matrix calculations that we
 *    only need once per frame.
 */

void
setupFrame(void)
{
   static Matrix world;
   SVGA3dTextureState *ts;
   SVGA3dRenderState *rs;

   static const SVGA3dLightData light = {
      .type = SVGA3D_LIGHTTYPE_POINT,
      .inWorldSpace = TRUE,
      .diffuse = { 10.0f, 10.0f, 10.0f, 1.0f },
      .ambient = { 0.05f, 0.05f, 0.1f, 1.0f },
      .position = { -5.0f, 5.0f, 0.0f, 1.0f },
      .attenuation0 = 1.0f,
      .attenuation1 = 0.0f,
      .attenuation2 = 0.0f,
   };

   static const SVGA3dMaterial mat = {
      .diffuse = { 1.0f, 0.9f, 0.9f, 1.0f },
      .ambient = { 1.0f, 1.0f, 1.0f, 1.0f },
   };

   Matrix_Copy(world, gIdentityMatrix);
   Matrix_Scale(world, 10, 10, 10, 1);
   Matrix_RotateY(world, gFPS.frame * 0.001f);

   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_WORLD, world);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_PROJECTION, perspectiveMat);

   SVGA3D_SetMaterial(CID, SVGA3D_FACE_FRONT_BACK, &mat);
   SVGA3D_SetLightData(CID, 0, &light);
   SVGA3D_SetLightEnabled(CID, 0, TRUE);

   SVGA3D_BeginSetRenderState(CID, &rs, 8);
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
      rs[4].uintValue = TRUE;

      rs[5].state     = SVGA3D_RS_VERTEXMATERIALENABLE;
      rs[5].uintValue = FALSE;

      rs[6].state     = SVGA3D_RS_CULLMODE;
      rs[6].uintValue = SVGA3D_FACE_FRONT;

      rs[7].state     = SVGA3D_RS_AMBIENT;
      rs[7].uintValue = 0x00000000;
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
}


/*
 * drawMesh --
 *
 *    Draw our bunny mesh at a particular position.
 */

void
drawMesh(float posX, float posY, float posZ)
{
   SVGA3dVertexDecl *decls;
   SVGA3dPrimitiveRange *ranges;
   static Matrix view;

   Matrix_Copy(view, gIdentityMatrix);
   Matrix_Translate(view, posX, posY, posZ);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_VIEW, view);

   SVGA3D_BeginDrawPrimitives(CID, &decls, 2, &ranges, 1);
   {
      decls[0].identity.type = SVGA3D_DECLTYPE_FLOAT3;
      decls[0].identity.usage = SVGA3D_DECLUSAGE_POSITION;
      decls[0].array.surfaceId = vertexSid;
      decls[0].array.stride = 6 * sizeof(float);

      decls[1].identity.type = SVGA3D_DECLTYPE_FLOAT3;
      decls[1].identity.usage = SVGA3D_DECLUSAGE_NORMAL;
      decls[1].array.surfaceId = vertexSid;
      decls[1].array.stride = 6 * sizeof(float);
      decls[1].array.offset = 3 * sizeof(float);

      ranges[0].primType = SVGA3D_PRIMITIVE_TRIANGLELIST;
      ranges[0].primitiveCount = ibSize / sizeof(uint32) / 3;
      ranges[0].indexArray.surfaceId = indexSid;
      ranges[0].indexArray.stride = sizeof(uint32);
      ranges[0].indexWidth = sizeof(uint32);
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

   vertexSid = SVGA3DUtil_LoadCompressedBuffer(vbFile, &vbSize);
   indexSid = SVGA3DUtil_LoadCompressedBuffer(ibFile, &ibSize);

   Matrix_Perspective(perspectiveMat, 45.0f,
                      gSVGA.width / (float)gSVGA.height, 0.1f, 100.0f);

   while (1) {
      int i;

      if (SVGA3DUtil_UpdateFPSCounter(&gFPS)) {
         Console_Clear();
         Console_Format("VMware SVGA3D Example:\n"
                        "Bunnies: Drawing 4 copies of the Stanford Bunny,"
                        " at 65K triangles each.\n\n%s",
                        gFPS.text);
         SVGA3DText_Update();
      }

      SVGA3DUtil_ClearFullscreen(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                                 0x113366, 1.0f, 0);

      setupFrame();

      for (i = 0; i < 4; i++) {
         drawMesh(0.8 - i * 1.0f, -1, 3 + i * 1.0f);
      }

      SVGA3DText_Draw();
      SVGA3DUtil_PresentFullscreen();
   }

   return 0;
}
