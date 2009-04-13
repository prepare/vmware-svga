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
 * matrix.h --
 *
 *      Simple operations for 4x4 row-major float matrices.
 */

#ifndef __MATRIX_H__
#define __MATRIX_H__

#include "types.h"

typedef float Matrix[16];

extern const Matrix gIdentityMatrix;

void Matrix_Copy(Matrix self, const Matrix other);
void Matrix_Scale(Matrix self, float x, float y, float z, float w);
void Matrix_Translate(Matrix self, float x, float y, float z);
void Matrix_Multiply(Matrix self, const Matrix other);
void Matrix_RotateX(Matrix self, float rad);
void Matrix_RotateY(Matrix self, float rad);
void Matrix_RotateZ(Matrix self, float rad);
void Matrix_Perspective(Matrix self, float fovY, float aspect, float zNear, float zFar);

#endif /* __MATRIX_H_ */
