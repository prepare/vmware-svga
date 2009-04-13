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
 * matrix.c --
 *
 *      Simple operations for 4x4 row-major float matrices.
 */

#include "matrix.h"
#include "math.h"

/* Shortcut for accessing elements */
#define EL(col, row)  (((row)<<2) + (col))

const Matrix gIdentityMatrix = {
   1.0f, 0.0f, 0.0f, 0.0f,
   0.0f, 1.0f, 0.0f, 0.0f,
   0.0f, 0.0f, 1.0f, 0.0f,
   0.0f, 0.0f, 0.0f, 1.0f,
};


/*
 *----------------------------------------------------------------------
 *
 * Matrix_Copy --
 *
 *      Copy from 'other' to 'self'.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Matrix_Copy(Matrix self,         // OUT
            const Matrix other)  // IN
{
   memcpy(self, other, sizeof(Matrix));
}


/*
 *----------------------------------------------------------------------
 *
 * Matrix_Perspective --
 *
 *      Load a generic perspective matrix, equivalent to gluPerspective().
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Matrix_Perspective(Matrix self,   // OUT
                   float fovY,    // IN
                   float aspect,  // IN
                   float zNear,   // IN
                   float zFar)    // IN
{

   float f = 1.0 / tanf(fovY * (M_PI / 180) / 2);
   float q = zFar / (zFar - zNear);

   memset(self, 0, sizeof self);

   self[EL(0,0)] = f / aspect;
   self[EL(1,1)] = f;
   self[EL(2,2)] = q;
   self[EL(2,3)] = -q * zNear;
   self[EL(3,2)] = 1;
}


/*
 *----------------------------------------------------------------------
 *
 * Matrix_Scale --
 *
 *      Scale a matrix by the provided 4-vector.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Matrix_Scale(Matrix self,  // IN/OUT
             float x,      // IN
             float y,      // IN
             float z,      // IN
             float w)      // IN
{
   self[EL(0, 0)] *= x;
   self[EL(0, 1)] *= x;
   self[EL(0, 2)] *= x;
   self[EL(0, 3)] *= x;

   self[EL(1, 0)] *= y;
   self[EL(1, 1)] *= y;
   self[EL(1, 2)] *= y;
   self[EL(1, 3)] *= y;

   self[EL(2, 0)] *= z;
   self[EL(2, 1)] *= z;
   self[EL(2, 2)] *= z;
   self[EL(2, 3)] *= z;

   self[EL(3, 0)] *= w;
   self[EL(3, 1)] *= w;
   self[EL(3, 2)] *= w;
   self[EL(3, 3)] *= w;
}


/*
 *----------------------------------------------------------------------
 *
 * Matrix_Translate --
 *
 *      Add a translation to a homogeneous matrix.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Matrix_Translate(Matrix self,  // IN/OUT
                 float x,      // IN
                 float y,      // IN
                 float z)      // IN
{
   self[EL(0, 3)] += x;
   self[EL(1, 3)] += y;
   self[EL(2, 3)] += z;
}


/*
 *----------------------------------------------------------------------
 *
 * Matrix_Multiply --
 *
 *      4x4 matrix multiply.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Matrix_Multiply(Matrix self,         // IN/OUT
                const Matrix other)  // IN
{
   Matrix result;
   int i;

#define A(x,y) self[EL(x,y)]
#define B(x,y) other[EL(x,y)]
#define C(x,y) result[EL(x,y)]

   for (i = 0; i < 4; i++) {
      C(0,i) = A(0,i)*B(0,0) + A(1,i)*B(0,1) + A(2,i)*B(0,2) + A(3,i)*B(0,3);
      C(1,i) = A(0,i)*B(1,0) + A(1,i)*B(1,1) + A(2,i)*B(1,2) + A(3,i)*B(1,3);
      C(2,i) = A(0,i)*B(2,0) + A(1,i)*B(2,1) + A(2,i)*B(2,2) + A(3,i)*B(2,3);
      C(3,i) = A(0,i)*B(3,0) + A(1,i)*B(3,1) + A(2,i)*B(3,2) + A(3,i)*B(3,3);
   }

#undef A
#undef B
#undef C

   memcpy(self, result, sizeof result);
}


/*
 *----------------------------------------------------------------------
 *
 * Matrix_RotateX --
 *
 *      Rotate a matrix about the X axis.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Matrix_RotateX(Matrix self,  // IN/OUT
               float rad)    // IN
{
   Matrix rotation = {
      1.0f, 0.0f,       0.0f,      0.0f,
      0.0f, cosf(rad),  sinf(rad), 0.0f,
      0.0f, -sinf(rad), cosf(rad), 0.0f,
      0.0f, 0.0f,       0.0f,      1.0f,
   };

   Matrix_Multiply(self, rotation);
}


/*
 *----------------------------------------------------------------------
 *
 * Matrix_RotateY --
 *
 *      Rotate a matrix about the Y axis.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Matrix_RotateY(Matrix self,  // IN/OUT
               float rad)    // IN
{
   Matrix rotation = {
      cosf(rad), 0.0f, -sinf(rad), 0.0f,
      0.0f,      1.0f, 0.0f,       0.0f,
      sinf(rad), 0.0f, cosf(rad),  0.0f,
      0.0f,      0.0f, 0.0f,       1.0f,
   };

   Matrix_Multiply(self, rotation);
}


/*
 *----------------------------------------------------------------------
 *
 * Matrix_RotateZ --
 *
 *      Rotate a matrix about the Z axis.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Matrix_RotateZ(Matrix self,  // IN/OUT
               float rad)    // IN
{
   Matrix rotation = {
      cosf(rad),  sinf(rad), 0.0f, 0.0f,
      -sinf(rad), cosf(rad), 0.0f, 0.0f,
      0.0f,       0.0f,      1.0f, 0.0f,
      0.0f,       0.0f,      0.0f, 1.0f,
   };

   Matrix_Multiply(self, rotation);
}
