/*
 * Test SVGA3D_RS_OUTPUTGAMMA.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga3dutil.h"
#include "matrix.h"
#include "math.h"

typedef uint32 DWORD;
#include "cube_vs.h"
#include "cube_ps.h"

#define MY_VSHADER_ID       0
#define MY_PSHADER_ID       0

#define CONST_MAT_VIEW      0
#define CONST_MAT_PROJ      4

typedef struct {
   float position[3];
   uint32 color;
} MyVertex;

/*
 * Cubes are drawn with a gradient from black to white.
 */
#define COLOR1 0x000000
#define COLOR2 0xFFFFFF

/*
 * This defines the grid spacing, as well as the total number of cubes we draw.
 */
#define GRID_STEP    2
#define GAMMA_STEP   0.1f
#define GRID_X_COUNT 5
#define GRID_Y_COUNT 5

static const MyVertex vertexData[] = {
   { {-1, -1, -1}, COLOR1 },
   { {-1, -1,  1}, COLOR1 },
   { {-1,  1, -1}, COLOR1 },
   { {-1,  1,  1}, COLOR1 },
   { { 1, -1, -1}, COLOR2 },
   { { 1, -1,  1}, COLOR2 },
   { { 1,  1, -1}, COLOR2 },
   { { 1,  1,  1}, COLOR2 },
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
VMMousePacket lastMouseState;

/*
 * render --
 *
 *   Set up common render state and matrices, then enter a loop
 *   drawing many cubes with individual draw commands.
 */

void
render(void)
{
   SVGA3dRenderState *rs;
   SVGA3dVertexDecl *decls;
   SVGA3dPrimitiveRange *ranges;
   static Matrix view, instance;
   static uint32 frame = 0;
   float x, y;

   Matrix_Copy(view, gIdentityMatrix);
   Matrix_Scale(view, 0.5, 0.5, 0.5, 1.0);
   Matrix_RotateY(view, 30.0 * M_PI / 180.0);
   Matrix_RotateX(view, ++frame * 0.001f);
   Matrix_Translate(view, 0, 0, 15);

   for (y = 0; y < GRID_Y_COUNT; y++) {
      for (x = 0; x < GRID_X_COUNT; x++) {
         float gamma = (x + y * GRID_X_COUNT) * GAMMA_STEP;

         Matrix_Copy(instance, view);
         Matrix_Translate(instance,
                          (x - GRID_X_COUNT / 2.0f + 0.5f) * GRID_STEP,
                          -(y - GRID_Y_COUNT / 2.0f + 0.5f) * GRID_STEP,
                          0);

         SVGA3DUtil_SetShaderConstMatrix(CID, CONST_MAT_VIEW,
                                         SVGA3D_SHADERTYPE_VS, instance);

         SVGA3D_BeginSetRenderState(CID, &rs, 4);
         {
            rs[0].state      = SVGA3D_RS_OUTPUTGAMMA;
            rs[0].floatValue = gamma;
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
   }
}


/*
 * main --
 *
 *    Our example's entry point, invoked directly by the bootloader.
 */

int
main(void)
{
   SVGA3dRenderState *rs;

   SVGA3DUtil_InitFullscreen(CID, 800, 600);

   vertexSid = SVGA3DUtil_DefineStaticBuffer(vertexData, sizeof vertexData);
   indexSid = SVGA3DUtil_DefineStaticBuffer(indexData, sizeof indexData);

   SVGA3D_DefineShader(CID, MY_VSHADER_ID, SVGA3D_SHADERTYPE_VS,
                        g_vs20_MyVertexShader, sizeof g_vs20_MyVertexShader);
   SVGA3D_DefineShader(CID, MY_PSHADER_ID, SVGA3D_SHADERTYPE_PS,
                       g_ps20_MyPixelShader, sizeof g_ps20_MyPixelShader);

   Matrix_Perspective(perspectiveMat, 45.0f,
                      gSVGA.width / (float)gSVGA.height, 10.0f, 100.0f);


   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_WORLD, gIdentityMatrix);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_PROJECTION, perspectiveMat);

   SVGA3D_SetShader(CID, SVGA3D_SHADERTYPE_VS, MY_VSHADER_ID);
   SVGA3D_SetShader(CID, SVGA3D_SHADERTYPE_PS, MY_PSHADER_ID);

   SVGA3DUtil_SetShaderConstMatrix(CID, CONST_MAT_PROJ,
                                   SVGA3D_SHADERTYPE_VS, perspectiveMat);

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

      rs[4].state     = SVGA3D_RS_CULLMODE;
      rs[4].uintValue = SVGA3D_FACE_FRONT;
   }
   SVGA_FIFOCommitAll();

   while (1) {
      SVGA3DUtil_ClearFullscreen(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                                 0x000000, 1.0f, 0);
      render();
      SVGA3DUtil_PresentFullscreen();
   }

   return 0;
}
