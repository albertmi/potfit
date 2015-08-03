/****************************************************************
 *
 * utils.c: potfit utilities (vectors and memory management)
 *
 ****************************************************************
 *
 * Copyright 2002-2014
 *	Institute for Theoretical and Applied Physics
 *	University of Stuttgart, D-70550 Stuttgart, Germany
 *	http://potfit.sourceforge.net/
 *
 ****************************************************************
 *
 *   This file is part of potfit.
 *
 *   potfit is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   potfit is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with potfit; if not, see <http://www.gnu.org/licenses/>.
 *
 *****************************************************************/

#include "potfit.h"

#include <stdint.h>

/* 32-bit */
#if UINTPTR_MAX == 0xffffffff
#ifndef ACML
#include <mkl_vml.h>
#endif /* ACML */
#define _32BIT

/* 64-bit */
#elif UINTPTR_MAX == 0xffffffffffffffff
#ifndef ACML
#include <mkl_vml.h>
#elif defined ACML4
#include <acml_mv.h>
#elif defined ACML5
#include <amdlibm.h>
#endif /* ACML */

/* wtf */
#else
#error Unknown integer size

#endif /* UINTPTR_MAX */

#include "utils.h"

// int* vect_int(long dim)
// {
//   int* vect, i;
//   vect = (int*)malloc((size_t)(dim * sizeof(int)));
//   if (vect == NULL)
//     error(1, "Error in integer vector allocation");
//   for (i = 0; i < dim; i++)
//     vect[i] = 0;
//
//   return vect;
// }
//
// double* vect_double(long dim)
// {
//   double* vect;
//   int i;
//   vect = (double*)malloc((size_t)(dim * sizeof(double)));
//   if (vect == NULL)
//     error(1, "Error in double vector allocation");
//   for (i = 0; i < dim; i++)
//     vect[i] = 0.0;
//
//   return vect;
// }

double** mat_double(long rowdim, long coldim)
{
  long i;
  double** matrix;

  /* matrix: array of array of pointers */
  /* matrix: pointer to rows */
  matrix = (double**)malloc((size_t)rowdim * sizeof(double*));
  if (matrix == NULL)
    error(1, "Error in double matrix row allocation");

  /* matrix[0]: pointer to elements */
  matrix[0] = (double*)malloc((size_t)rowdim * coldim * sizeof(double));
  if (matrix[0] == NULL)
    error(1, "Error in double matrix element allocation");

  for (i = 1; i < rowdim; i++)
    matrix[i] = matrix[i - 1] + coldim;

  int j, k;
  for (j = 0; j < rowdim; j++)
    for (k = 0; k < coldim; k++)
      matrix[j][k] = 0.0;

  return matrix;
}

void free_vect_double(double* vect) { free(vect); }

void free_vect_int(int* vect) { free(vect); }

void free_mat_double(double** matrix)
{
  free(matrix[0]);
  free(matrix);
}

/* vector product */
vector vec_prod(vector u, vector v)
{
  vector w;

  w.x = u.y * v.z - u.z * v.y;
  w.y = u.z * v.x - u.x * v.z;
  w.z = u.x * v.y - u.y * v.x;

  return w;
}

/****************************************************************
 *
 *  higher powers in one and more dimensions
 *
 ****************************************************************/

void power_1(double* result, double* x, double* y)
{
#ifdef _32BIT
  *result = pow(*x, *y);
#else
#ifndef ACML
  vdPow(1, x, y, result);
#elif defined ACML4
  *result = fastpow(*x, *y);
#elif defined ACML5
  *result = pow(*x, *y);
#endif /* ACML */
#endif /* _32BIT */
}

void power_m(int dim, double* result, double* x, double* y)
{
#ifdef _32BIT
  int i = 0;
  for (i = 0; i < dim; i++)
    result[i] = pow(x[i], y[i]);
#else
#ifndef ACML
  vdPow(dim, x, y, result);
#elif defined ACML4
  int i;
  for (i = 0; i < dim; i++)
    *(result + i) = fastpow(*(x + i), *(y + i));
#elif defined ACML5
  int i;
  for (i = 0; i < dim; i++)
    *(result + i) = pow(*(x + i), *(y + i));
#endif /* ACML */
#endif /* _32BIT */
}

#ifdef _32BIT
#undef _32BIT
#endif /* _32BIT */
