/**********************************************************
 * Copyright 2008-2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

/*
 * svga3dtext.c --
 *
 *      Emulated text console, built on the SVGA3D protocol.  This
 *      module allows the use of the VGA Console in 3D mode. We
 *      convert the ROM BIOS font into a texture, and sample character
 *      data from the text-mode framebuffer. Text attributes are
 *      ignored.
 *
 *      This is used as a debug/diagnostic facility in the example
 *      programs, plus it's a simple but relatively efficient example
 *      of rendering using dynamic vertex buffer data.
 *
 *      XXX: Now that there can be multiple Console backends, this no
 *      longer needs to be tied to VGA. We should just implement this
 *      as a normal Console backend.
 */

#include "svga3dutil.h"
#include "svga3dtext.h"
#include "matrix.h"
#include "console_vga.h"

typedef uint16 IndexType;

typedef struct {
   uint16 position[2];
   float texCoord[2];
   uint32 color;
} VertexType;

#define  MAX_NUM_CHARACTERS  (VGA_TEXT_WIDTH * VGA_TEXT_HEIGHT)
#define  MAX_VERTICES        (MAX_NUM_CHARACTERS * 4)
#define  MAX_INDICES         (MAX_NUM_CHARACTERS * 6)
#define  INDEX_BUF_SIZE      (MAX_INDICES * sizeof(IndexType))
#define  VERTEX_BUF_SIZE     (MAX_VERTICES * sizeof(VertexType))
#define  FONT_CHARACTERS     256
#define  FONT_CHAR_WIDTH     9
#define  FONT_CHAR_HEIGHT    9
#define  FONT_GRID_WIDTH     25
#define  FONT_WIDTH          256
#define  FONT_HEIGHT         64

static struct {
   uint32       fontSid;
   uint32       ibSid;

   uint32       vbSid;
   SVGAGuestPtr vbGuestPtr;
   VertexType  *vbBuffer;
   uint32       vbFence;
   uint32       numTriangles;

   Matrix          view;
} self;


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DTextUnpackROMFont --
 *
 *      Unpack the ROM BIOS font into a square texture, 16 characters
 *      by 8 characters, in 8-bit alpha format.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Reads from BIOS memory.
 *
 *----------------------------------------------------------------------
 */

static void
SVGA3DTextUnpackROMFont(uint8 *buffer)
{
   uint8 *romFont = (uint8*) 0xFFA6E;
   uint8 fontChar = 0;
   int gridX = 0, gridY = 0;
   int charY;

   memset(buffer, 0, FONT_WIDTH * FONT_HEIGHT);

   while (fontChar < 128) {
      for (charY = 0; charY < 8; charY++) {
         uint8 fontByte = *(romFont++);
         uint8 mask = 0x80;
         uint8 *bufferLine = &buffer[FONT_WIDTH * (gridY * FONT_CHAR_HEIGHT + charY)
                                     + gridX * FONT_CHAR_WIDTH];
         while (mask) {
            if (fontByte & mask) {
               *bufferLine = 0xFF;
               }
               bufferLine++;
               mask >>= 1;
            }
         }
      fontChar++;
      gridX++;
      if (gridX == FONT_GRID_WIDTH) {
         gridX = 0;
         gridY++;
      }
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DText_Init --
 *
 *      Initialize the SVGA3DText module. This populates the font
 *      texture, and sets up the vertex buffer and index buffer
 *      surfaces. It implicitly calls SVGA3DText_Update() for the
 *      first time.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Allocates buffers, begins DMA.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DText_Init(void)
{
   int i;
   void *fontBuffer;
   IndexType *indexBuffer;
   SVGAGuestPtr fontGuestPtr;
   SVGAGuestPtr ibGuestPtr;

   /*
    * XXX: We should stop depending on console_vga, and get our own framebuffer.
    */
   ConsoleVGA_Init();

   /*
    * Populate the font texture with our ROM BIOS font.
    */

   fontBuffer = SVGA3DUtil_AllocDMABuffer(FONT_WIDTH * FONT_HEIGHT, &fontGuestPtr);
   SVGA3DTextUnpackROMFont(fontBuffer);

   self.fontSid = SVGA3DUtil_DefineSurface2D(FONT_WIDTH, FONT_HEIGHT, SVGA3D_ALPHA8);
   SVGA3DUtil_SurfaceDMA2D(self.fontSid, &fontGuestPtr, SVGA3D_WRITE_HOST_VRAM,
                           FONT_WIDTH, FONT_HEIGHT);

   /*
    * Populate the index buffer with a static pattern for drawing quads.
    */

   indexBuffer = SVGA3DUtil_AllocDMABuffer(INDEX_BUF_SIZE, &ibGuestPtr);

   for (i = 0; i < MAX_NUM_CHARACTERS; i++) {
      /* First triangle */
      indexBuffer[i * 6 + 0] = i * 4 + 0;
      indexBuffer[i * 6 + 1] = i * 4 + 1;
      indexBuffer[i * 6 + 2] = i * 4 + 2;

      /* Second triangle */
      indexBuffer[i * 6 + 3] = i * 4 + 2;
      indexBuffer[i * 6 + 4] = i * 4 + 3;
      indexBuffer[i * 6 + 5] = i * 4 + 0;
   }

   self.ibSid = SVGA3DUtil_DefineSurface2D(INDEX_BUF_SIZE, 1, SVGA3D_BUFFER);
   SVGA3DUtil_SurfaceDMA2D(self.ibSid, &ibGuestPtr, SVGA3D_WRITE_HOST_VRAM,
                           INDEX_BUF_SIZE, 1);

   /*
    * Set up an empty vertex buffer. We'll fill it later.
    */

   self.vbBuffer = SVGA3DUtil_AllocDMABuffer(VERTEX_BUF_SIZE, &self.vbGuestPtr);
   self.vbSid = SVGA3DUtil_DefineSurface2D(VERTEX_BUF_SIZE, 1, SVGA3D_BUFFER);

   /*
    * Set up the view matrix.
    */
   {
      const float border = 0.05;
      const float width = (2.0 - border*2) / VGA_TEXT_WIDTH;
      const float height = (2.0 - border*2) / VGA_TEXT_HEIGHT;

      Matrix_Copy(self.view, gIdentityMatrix);
      Matrix_Scale(self.view, width, -height, 1, 1);
      Matrix_Translate(self.view, -1 + border, 1 - border, 0);
   }

   SVGA3DText_Update();
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DText_Update --
 *
 *      Build and upload a vertex buffer for rendering text from the
 *      current contents of the VGA framebuffer.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May SyncToFence. Reads VGA memory.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DText_Update(void)
{
   uint8 *textFb = (uint8*) 0xB8000;
   int x, y;
   VertexType *vertex = self.vbBuffer;

   /* Wait for any previous DMA operations to finish. */
   SVGA_SyncToFence(self.vbFence);

   self.numTriangles = 0;

   for (y = 0; y < VGA_TEXT_HEIGHT; y++) {
      for (x = 0; x < VGA_TEXT_WIDTH; x++) {
         uint8 textChar = *textFb;
         textFb += 2;

         if (textChar > 0 && textChar < 0x80 && textChar != ' ') {
            int charX = textChar % FONT_GRID_WIDTH;
            int charY = textChar / FONT_GRID_WIDTH;
            const float xHalfTexel = 0.5 / (float)FONT_WIDTH;
            const float yHalfTexel = 0.5 / (float)FONT_HEIGHT;
            const float charWidth = FONT_CHAR_WIDTH / (float)FONT_WIDTH;
            const float charHeight = FONT_CHAR_WIDTH / (float)FONT_HEIGHT;

            vertex[0].position[0] = x;
            vertex[0].position[1] = y;
            vertex[0].texCoord[0] = charX * charWidth - xHalfTexel;
            vertex[0].texCoord[1] = charY * charHeight - yHalfTexel;
            vertex[0].color       = 0xFFFFFFFF;

            vertex[2].position[0] = x+1;
            vertex[2].position[1] = y+1;
            vertex[2].texCoord[0] = (charX+1) * charWidth - xHalfTexel;
            vertex[2].texCoord[1] = (charY+1) * charHeight - yHalfTexel;
            vertex[2].color       = 0xFFFFFFFF;

            vertex[1].position[0] = vertex[2].position[0];
            vertex[1].position[1] = vertex[0].position[1];
            vertex[1].texCoord[0] = vertex[2].texCoord[0];
            vertex[1].texCoord[1] = vertex[0].texCoord[1];
            vertex[1].color       = 0xFFFFFFFF;

            vertex[3].position[0] = vertex[0].position[0];
            vertex[3].position[1] = vertex[2].position[1];
            vertex[3].texCoord[0] = vertex[0].texCoord[0];
            vertex[3].texCoord[1] = vertex[2].texCoord[1];
            vertex[3].color       = 0xFFFFFFFF;

            self.numTriangles += 2;
            vertex += 4;
         }
      }
   }

   SVGA3DUtil_SurfaceDMA2D(self.vbSid, &self.vbGuestPtr, SVGA3D_WRITE_HOST_VRAM,
                           (uint8*)vertex - (uint8*)self.vbBuffer, 1);
   self.vbFence = SVGA_InsertFence();
}


/*
 *----------------------------------------------------------------------
 *
 * SVGA3DText_Draw --
 *
 *      Draw a screen full of text, using the current vertex buffer contents.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies a lot of render state.
 *
 *----------------------------------------------------------------------
 */

void
SVGA3DText_Draw(void)
{
   SVGA3dVertexDecl *decls;
   SVGA3dPrimitiveRange *ranges;
   SVGA3dTextureState *ts;
   SVGA3dRenderState *rs;

   static const SVGA3dMaterial mat = {
      .diffuse = { 1, 1, 1, 1 },
   };

   SVGA3D_SetMaterial(CID, SVGA3D_FACE_FRONT_BACK, &mat);

   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_VIEW, self.view);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_WORLD, gIdentityMatrix);
   SVGA3D_SetTransform(CID, SVGA3D_TRANSFORM_PROJECTION, gIdentityMatrix);

   SVGA3D_BeginSetRenderState(CID, &rs, 8);
   {
      rs[0].state     = SVGA3D_RS_ZENABLE;
      rs[0].uintValue = FALSE;

      rs[1].state     = SVGA3D_RS_ZWRITEENABLE;
      rs[1].uintValue = FALSE;

      rs[2].state     = SVGA3D_RS_BLENDENABLE;
      rs[2].uintValue = TRUE;

      rs[3].state     = SVGA3D_RS_SRCBLEND;
      rs[3].uintValue = SVGA3D_BLENDOP_SRCALPHA;

      rs[4].state     = SVGA3D_RS_DSTBLEND;
      rs[4].uintValue = SVGA3D_BLENDOP_INVSRCALPHA;

      rs[5].state     = SVGA3D_RS_BLENDEQUATION;
      rs[5].uintValue = SVGA3D_BLENDEQ_ADD;

      rs[6].state     = SVGA3D_RS_LIGHTINGENABLE;
      rs[6].uintValue = FALSE;

      rs[7].state     = SVGA3D_RS_CULLMODE;
      rs[7].uintValue = SVGA3D_FACE_NONE;
   }
   SVGA_FIFOCommitAll();

   SVGA3D_BeginSetTextureState(CID, &ts, 9);
   {
      ts[0].stage = 0;
      ts[0].name  = SVGA3D_TS_BIND_TEXTURE;
      ts[0].value = self.fontSid;

      ts[1].stage = 0;
      ts[1].name  = SVGA3D_TS_COLOROP;
      ts[1].value = SVGA3D_TC_SELECTARG1;

      ts[2].stage = 0;
      ts[2].name  = SVGA3D_TS_COLORARG1;
      ts[2].value = SVGA3D_TA_DIFFUSE;

      ts[3].stage = 0;
      ts[3].name  = SVGA3D_TS_ALPHAOP;
      ts[3].value = SVGA3D_TC_SELECTARG1;

      ts[4].stage = 0;
      ts[4].name  = SVGA3D_TS_ALPHAARG1;
      ts[4].value = SVGA3D_TA_TEXTURE;

      ts[5].stage = 0;
      ts[5].name  = SVGA3D_TS_MINFILTER;
      ts[5].value = SVGA3D_TEX_FILTER_LINEAR;

      ts[6].stage = 0;
      ts[6].name  = SVGA3D_TS_MAGFILTER;
      ts[6].value = SVGA3D_TEX_FILTER_LINEAR;

      ts[7].stage = 0;
      ts[7].name  = SVGA3D_TS_ADDRESSU;
      ts[7].value = SVGA3D_TEX_ADDRESS_WRAP;

      ts[8].stage = 0;
      ts[8].name  = SVGA3D_TS_ADDRESSV;
      ts[8].value = SVGA3D_TEX_ADDRESS_WRAP;
   }
   SVGA_FIFOCommitAll();

   SVGA3D_BeginDrawPrimitives(CID, &decls, 3, &ranges, 1);
   {
      decls[0].identity.type = SVGA3D_DECLTYPE_SHORT2;
      decls[0].identity.usage = SVGA3D_DECLUSAGE_POSITION;
      decls[0].array.surfaceId = self.vbSid;
      decls[0].array.stride = sizeof(VertexType);
      decls[0].array.offset = offsetof(VertexType, position);

      decls[1].identity.type = SVGA3D_DECLTYPE_FLOAT2;
      decls[1].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
      decls[1].array.surfaceId = self.vbSid;
      decls[1].array.stride = sizeof(VertexType);
      decls[1].array.offset = offsetof(VertexType, texCoord);

      decls[2].identity.type = SVGA3D_DECLTYPE_D3DCOLOR;
      decls[2].identity.usage = SVGA3D_DECLUSAGE_COLOR;
      decls[2].array.surfaceId = self.vbSid;
      decls[2].array.stride = sizeof(VertexType);
      decls[2].array.offset = offsetof(VertexType, color);

      ranges[0].primType = SVGA3D_PRIMITIVE_TRIANGLELIST;
      ranges[0].primitiveCount = self.numTriangles;
      ranges[0].indexArray.surfaceId = self.ibSid;
      ranges[0].indexArray.stride = sizeof(IndexType);
      ranges[0].indexWidth = sizeof(IndexType);
   }
   SVGA_FIFOCommitAll();
   SVGA_RingDoorbell();
}
