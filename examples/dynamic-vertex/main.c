/*
 * SVGA3D example: Dynamic vertex buffers.
 *
 * This example shows how to efficiently stream vertex data to the
 * GPU, using multiple DMA buffers but a single vertex buffer. We
 * allocate DMA buffers from a pool every time we want to draw a new
 * dynamic mesh, then we asynchronously recycle those buffers after
 * the DMA transfer has completed.
 *
 * Copyright (C) 2008-2009 VMware, Inc. Licensed under the MIT
 * License, please see the README.txt. All rights reserved.
 */

#include "svga3dutil.h"
#include "svga3dtext.h"
#include "matrix.h"
#include "math.h"

#define MESH_WIDTH      128
#define MESH_HEIGHT     128

#define MESH_NUM_VERTICES   (MESH_WIDTH * MESH_HEIGHT)
#define MESH_NUM_QUADS      ((MESH_WIDTH-1) * (MESH_HEIGHT-1))
#define MESH_NUM_TRIANGLES  (MESH_NUM_QUADS * 2)
#define MESH_NUM_INDICES    (MESH_NUM_TRIANGLES * 3)

#define MESH_ELEMENT(x, y)  (MESH_WIDTH * (y) + (x))

typedef struct {
   float position[3];
   float color[3];
} MyVertex;

typedef uint16 IndexType;
DMAPool vertexDMA;
uint32 vertexSid, indexSid;
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

   Matrix_Copy(world, gIdentityMatrix);
   Matrix_RotateX(world, -60.0 * PI_OVER_180);
   Matrix_RotateY(world, gFPS.frame * 0.001f);

   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_WORLD, world);
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
}


/*
 * updateVertices --
 *
 *    Calculate new vertices, writing them directly into an available
 *    DMA buffer. Asynchronously begin DMA and recycle the buffer.
 */

void
updateVertices(float red, float green, float blue, float phase, float offset)
{
   DMAPoolBuffer *dma;
   MyVertex *vert;
   int x, y;
   float t = gFPS.frame * 0.01f + phase;

   dma = SVGA3DUtil_DMAPoolGetBuffer(&vertexDMA);
   vert = (MyVertex*) dma->buffer;

   for (y = 0; y < MESH_HEIGHT; y++) {
      for (x = 0; x < MESH_WIDTH; x++) {

         float fx = x * (2.0 / MESH_WIDTH) - 1.0;
         float fy = y * (2.0 / MESH_HEIGHT) - 1.0;
         float fxo = fx + offset;
         float dist = fxo * fxo + fy * fy;
         float z = sinf(dist * 8.0 + t) / (1 + dist * 10.0);

         vert->position[0] = fx;
         vert->position[1] = fy;
         vert->position[2] = z;

         vert->color[0] = red - z;
         vert->color[1] = green - z;
         vert->color[2] = blue - z;

         vert++;
      }
   }

   SVGA3DUtil_SurfaceDMA2D(vertexSid, &dma->ptr, SVGA3D_WRITE_HOST_VRAM,
                           MESH_NUM_VERTICES * sizeof(MyVertex), 1);

   SVGA3DUtil_AsyncCall((AsyncCallFn) SVGA3DUtil_DMAPoolFreeBuffer, dma);
}


/*
 * drawMesh --
 *
 *    Draw our mesh at a particular position. This uses the index and
 *    vertex data which is resident in the host VRAM buffers at the
 *    time the drawing command is executed asynchronously.
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
      decls[0].array.stride = sizeof(MyVertex);
      decls[0].array.offset = offsetof(MyVertex, position);

      decls[1].identity.type = SVGA3D_DECLTYPE_FLOAT3;
      decls[1].identity.usage = SVGA3D_DECLUSAGE_COLOR;
      decls[1].array.surfaceId = vertexSid;
      decls[1].array.stride = sizeof(MyVertex);
      decls[1].array.offset = offsetof(MyVertex, color);

      ranges[0].primType = SVGA3D_PRIMITIVE_TRIANGLELIST;
      ranges[0].primitiveCount = MESH_NUM_TRIANGLES;
      ranges[0].indexArray.surfaceId = indexSid;
      ranges[0].indexArray.stride = sizeof(IndexType);
      ranges[0].indexWidth = sizeof(IndexType);
   }
   SVGA_FIFOCommitAll();
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
 * main --
 *
 *    Our example's entry point, invoked directly by the bootloader.
 */

int
main(void)
{
   SVGA3DUtil_InitFullscreen(CID, 800, 600);
   SVGA3DText_Init();

   vertexSid = SVGA3DUtil_DefineSurface2D(MESH_NUM_VERTICES * sizeof(MyVertex),
                                          1, SVGA3D_BUFFER);
   indexSid = createIndexBuffer();

   SVGA3DUtil_AllocDMAPool(&vertexDMA, MESH_NUM_VERTICES * sizeof(MyVertex), 16);

   Matrix_Perspective(perspectiveMat, 45.0f,
                      gSVGA.width / (float)gSVGA.height, 0.1f, 100.0f);

   while (1) {
      if (SVGA3DUtil_UpdateFPSCounter(&gFPS)) {
         Console_Clear();
         Console_Format("VMware SVGA3D Example:\n"
                        "Dynamic vertex buffers.\n\n%s",
                        gFPS.text);
         SVGA3DText_Update();
      }

      SVGA3DUtil_ClearFullscreen(CID, SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH,
                                 0x113366, 1.0f, 0);

      setupFrame();

      updateVertices(1, 0.5, 0.5, M_PI, 0);
      drawMesh(-1.5, -1, 6);

      updateVertices(0.5, 1.0, 0.5, 0, 0);
      drawMesh(0, 1, 6);

      updateVertices(0.5, 0.5, 1.0, 0, 1.5);
      drawMesh(1.5, -1, 6);

      SVGA3DText_Draw();
      SVGA3DUtil_PresentFullscreen();
   }

   return 0;
}
