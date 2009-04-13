/*
 * SVGA3D example: Simple Shaders.
 *
 * This is a simple example to demonstrate the programmable pixel
 * and vertex pipelines. A vertex shader animates a rippling surface,
 * and a pixel shader generates a procedural checkerboard pattern.
 *
 * For simplicity, this example generates shader bytecode at
 * compile-time using the Microsoft HLSL compiler.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga3dutil.h"
#include "svga3dtext.h"
#include "matrix.h"
#include "math.h"

typedef uint32 DWORD;
#include "simple_vs.h"
#include "simple_ps.h"

/*
 * Small integers to identify our shaders.
 */
#define MY_VSHADER_ID       0
#define MY_PSHADER_ID       0

/*
 * Shader constants. These must match the constant registers in the
 * bytecode we send the device, so in this example the constants are
 * actually assigned by the Microsoft HLSL compiler.
 */
#define CONST_MAT_WORLDVIEWPROJ   0
#define CONST_TIMESTEP            4

/*
 * Macros for the simple mesh we generate as input for the vertex
 * shader.  It's a static grid in the XY plane.
 */
#define MESH_WIDTH      256
#define MESH_HEIGHT     256
#define MESH_NUM_VERTICES   (MESH_WIDTH * MESH_HEIGHT)
#define MESH_NUM_QUADS      ((MESH_WIDTH-1) * (MESH_HEIGHT-1))
#define MESH_NUM_TRIANGLES  (MESH_NUM_QUADS * 2)
#define MESH_NUM_INDICES    (MESH_NUM_TRIANGLES * 3)
#define MESH_ELEMENT(x, y)  (MESH_WIDTH * (y) + (x))

typedef struct {
   float position[3];
} MyVertex;

typedef uint16 IndexType;
uint32 vertexSid, indexSid;
FPSCounterState gFPS;


/*
 * render --
 *
 *    Set up render state that we load once per frame (because
 *    SVGA3DText clobbered it) and render the scene.
 */

void
render(void)
{
   SVGA3dVertexDecl *decls;
   SVGA3dPrimitiveRange *ranges;
   SVGA3dRenderState *rs;

   float shaderTimestep[4] = { gFPS.frame * 0.01 };

   SVGA3D_SetShaderConst(CID, CONST_TIMESTEP, SVGA3D_SHADERTYPE_VS,
                         SVGA3D_CONST_TYPE_FLOAT, shaderTimestep);

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

   SVGA3D_SetShader(CID, SVGA3D_SHADERTYPE_VS, MY_VSHADER_ID);
   SVGA3D_SetShader(CID, SVGA3D_SHADERTYPE_PS, MY_PSHADER_ID);

   SVGA3D_BeginDrawPrimitives(CID, &decls, 1, &ranges, 1);
   {
      decls[0].identity.type = SVGA3D_DECLTYPE_FLOAT3;
      decls[0].identity.usage = SVGA3D_DECLUSAGE_POSITION;
      decls[0].array.surfaceId = vertexSid;
      decls[0].array.stride = sizeof(MyVertex);
      decls[0].array.offset = offsetof(MyVertex, position);

      ranges[0].primType = SVGA3D_PRIMITIVE_TRIANGLELIST;
      ranges[0].primitiveCount = MESH_NUM_TRIANGLES;
      ranges[0].indexArray.surfaceId = indexSid;
      ranges[0].indexArray.stride = sizeof(IndexType);
      ranges[0].indexWidth = sizeof(IndexType);
   }
   SVGA_FIFOCommitAll();

   SVGA3D_SetShader(CID, SVGA3D_SHADERTYPE_VS, SVGA3D_INVALID_ID);
   SVGA3D_SetShader(CID, SVGA3D_SHADERTYPE_PS, SVGA3D_INVALID_ID);
}


/*
 * createIndexBuffer --
 *
 *    Create a static index buffer that renders our vertices as a 2D
 *    mesh. For simplicity, we use a triangle list rather than a
 *    triangle strip.
 */

uint32
createIndexBuffer(void)
{
   IndexType *indexBuffer;
   const uint32 bufferSize = MESH_NUM_INDICES * sizeof *indexBuffer;
   SVGAGuestPtr gPtr;
   uint32 sid;
   int x, y;

   sid = SVGA3DUtil_DefineSurface2D(bufferSize, 1, SVGA3D_BUFFER);
   indexBuffer = SVGA3DUtil_AllocDMABuffer(bufferSize, &gPtr);

   for (y = 0; y < (MESH_HEIGHT - 1); y++) {
      for (x = 0; x < (MESH_WIDTH - 1); x++) {

         indexBuffer[0] = MESH_ELEMENT(x,   y  );
         indexBuffer[1] = MESH_ELEMENT(x+1, y  );
         indexBuffer[2] = MESH_ELEMENT(x+1, y+1);

         indexBuffer[3] = MESH_ELEMENT(x+1, y+1);
         indexBuffer[4] = MESH_ELEMENT(x,   y+1);
         indexBuffer[5] = MESH_ELEMENT(x,   y  );

         indexBuffer += 6;
      }
   }

   SVGA3DUtil_SurfaceDMA2D(sid, &gPtr, SVGA3D_WRITE_HOST_VRAM, bufferSize, 1);

   return sid;
}


/*
 * createVertexBuffer --
 *
 *    Create a static vertex buffer that renders a mesh on thee XY
 *    plane. For simplicity, we use a triangle list rather than a
 *    triangle strip.
 */

uint32
createVertexBuffer(void)
{
   MyVertex *vert;
   const uint32 bufferSize = MESH_NUM_VERTICES * sizeof(MyVertex);
   SVGAGuestPtr gPtr;
   uint32 sid;
   int x, y;

   sid = SVGA3DUtil_DefineSurface2D(bufferSize, 1, SVGA3D_BUFFER);
   vert = SVGA3DUtil_AllocDMABuffer(bufferSize, &gPtr);

   for (y = 0; y < MESH_HEIGHT; y++) {
      for (x = 0; x < MESH_WIDTH; x++) {

         vert->position[0] = x * (2.0 / MESH_WIDTH) - 1.0;
         vert->position[1] = y * (2.0 / MESH_HEIGHT) - 1.0;
         vert->position[2] = 0.0f;

         vert++;
      }
   }

   SVGA3DUtil_SurfaceDMA2D(sid, &gPtr, SVGA3D_WRITE_HOST_VRAM, bufferSize, 1);

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
   Matrix worldViewProj, proj;

   SVGA3DUtil_InitFullscreen(CID, 800, 600);
   SVGA3DText_Init();

   vertexSid = createVertexBuffer();
   indexSid = createIndexBuffer();

   SVGA3D_DefineShader(CID, MY_VSHADER_ID, SVGA3D_SHADERTYPE_VS,
                       g_vs20_MyVertexShader, sizeof g_vs20_MyVertexShader);
   SVGA3D_DefineShader(CID, MY_PSHADER_ID, SVGA3D_SHADERTYPE_PS,
                       g_ps20_MyPixelShader, sizeof g_ps20_MyPixelShader);

   /*
    * Compute a single matrix for the world, view, and projection
    * transforms, then upload that to the shader.
    */

   Matrix_Copy(worldViewProj, gIdentityMatrix);
   Matrix_RotateX(worldViewProj, 60.0 * PI_OVER_180);
   Matrix_Translate(worldViewProj, 0, 0, 3);
   Matrix_Perspective(proj, 45.0f, gSVGA.width / (float)gSVGA.height, 0.1f, 100.0f);
   Matrix_Multiply(worldViewProj, proj);

   SVGA3DUtil_SetShaderConstMatrix(CID, CONST_MAT_WORLDVIEWPROJ,
                                   SVGA3D_SHADERTYPE_VS, worldViewProj);

   while (1) {
      if (SVGA3DUtil_UpdateFPSCounter(&gFPS)) {
         Console_Clear();
         Console_Format("VMware SVGA3D Example:\n"
                        "Simple Shaders.\n\n%s",
                        gFPS.text);
         SVGA3DText_Update();
      }

      SVGA3DUtil_ClearFullscreen(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                                 0x113366, 1.0f, 0);
      render();
      SVGA3DText_Draw();
      SVGA3DUtil_PresentFullscreen();
   }

   return 0;
}
