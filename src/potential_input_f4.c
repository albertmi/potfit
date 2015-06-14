/****************************************************************
 *
 * potential_input.c: Routines for reading a potential table
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
 ****************************************************************/

#include "potfit.h"

#include "potential_input.h"
#include "utils.h"

void init_calc_table4();

/****************************************************************
 *
 *  read potential in fourth format:
 *
 *  Sampling points are NON-equidistant.
 *
 *  Header:  one line for each function with
 *           npoints
 *
 *  Table: Sampling points, function values
 *            r f(r)
 *         functions separated by blank lines
 *
 ****************************************************************/

void read_pot_table4(char const* potential_filename, FILE* pfile, potential_state* pstate)
{
//   int   i, k, l, j;
// #if defined(EAM) || defined(ADP)
//   int   den_count;
//   int   emb_count;
// #endif /* EAM || ADP */
  int   nvals[pstate->num_pots];
//   double *val, *ord;

  pot_table_t* pt = &g_pot.opt_pot;

  /* read the info block of the function table */
  for (int i = 0; i < pstate->num_pots; i++)
  {
    if (1 > fscanf(pfile, "%d", &nvals[i]))
      error(1, "Premature end of potential file %s", pstate->filename);
    pt->step[i] = 0.0;
    pt->invstep[i] = 0.0;
    if (i == 0)
      pt->first[i] = 2;
    else
      pt->first[i] = pt->last[i - 1] + 3;
    pt->last[i] = pt->first[i] + nvals[i] - 1;
    pt->len = pt->first[i] + nvals[i];
  }
  /* allocate the function table */
  pt->table = (double *)malloc(pt->len * sizeof(double));
  pt->xcoord = (double *)malloc(pt->len * sizeof(double));
  pt->d2tab = (double *)malloc(pt->len * sizeof(double));
  pt->idx = (int *)malloc(pt->len * sizeof(int));

  if ((NULL == pt->table) || (NULL == pt->xcoord) || (NULL == pt->idx)
    || (NULL == pt->d2tab))
    error(1, "Cannot allocate memory for potential table");

  for (int i = 0; i < pt->len; i++) {
    pt->table[i] = 0.0;
    pt->xcoord[i] = 0.0;
    pt->d2tab[i] = 0.0;
    pt->idx[i] = 0;
  }

  /* input loop */
  double* val = pt->table;
  double* ord = pt->xcoord;
  int k = 0;
  int l = 0;

  /* read pair potentials */
  for (int i = 0; i < g_calc.paircol; i++)
  {
    if (pstate->have_gradient)
    {
      if (2 > fscanf(pfile, "%lf %lf\n", val, val + 1))
        error(1, "Premature end of potential file %s", pstate->filename);
    } else {
      *val = 1e30;
      *(val + 1) = 0.0;
    }
    val += 2;
    ord += 2;
    if ((!g_pot.invar_pot[i]) && (g_pot.gradient[i] >> 1))
      pt->idx[k++] = l++;
    else
      l++;
    if ((!g_pot.invar_pot[i]) && (g_pot.gradient[i] % 2))
      pt->idx[k++] = l++;
    else
      l++;
    /* read values */
    for (int j = 0; j < nvals[i]; j++)
    {
      if (2 > fscanf(pfile, "%lf %lf\n", ord, val))
        error(1, "Premature end of potential file %s", pstate->filename);
      else {
	val++;
	ord++;
      }
      if ((j > 0) && (*(ord - 1) <= *(ord - 2)))
	error(1, "Abscissa not monotonous in potential %d.", i);
      if ((!g_pot.invar_pot[i]) && (j < nvals[i] - 1))
	pt->idx[k++] = l++;
      else
	l++;
    }
    pt->begin[i] = pt->xcoord[pt->first[i]];
    pt->end[i] = pt->xcoord[pt->last[i]];
    /* pt->step is average step length.. */
    pt->step[i] = (pt->end[i] - pt->begin[i]) / ((double)nvals[i] - 1);
    pt->invstep[i] = 1.0 / pt->step[i];

  }
#if defined EAM || defined ADP
#ifndef TBEAM			/* EAM ADP MEAM */
  den_count = ntypes;
  emb_count = ntypes;
#else /* TBEAM */
  if (ntypes == 1) {
    den_count = ntypes + 1;
  } else {
    den_count = ntypes * (ntypes + 1) / 2;
  }
  emb_count = 2 * ntypes;
#endif /* END EAM or TBEAM */
  /* read EAM transfer function rho(r) */
  for (i = paircol; i < paircol + den_count; i++) {
    if (have_grad) {
      if (2 > fscanf(infile, "%lf %lf\n", val, val + 1))
        error(1, "Premature end of potential file %s", pstate->filename);
    } else {
      *val = 1e30;
      *(val + 1) = 0.0;
    }
    val += 2;
    ord += 2;
    if ((!invar_pot[i]) && (gradient[i] >> 1))
      pt->idx[k++] = l++;
    else
      l++;
    if ((!invar_pot[i]) && (gradient[i] % 2))
      pt->idx[k++] = l++;
    else
      l++;
    /* read values */
    for (j = 0; j < nvals[i]; j++) {
      if (2 > fscanf(infile, "%lf %lf\n", ord, val))
	error(1, "Premature end of potential file %s", filename);
      else {
	ord++;
	val++;
      }
      if ((j > 0) && (*(ord - 1) <= *(ord - 2)))
	error(1, "Abscissa not monotonous in potential %d.", i);
      if ((!invar_pot[i]) && (j < nvals[i] - 1))
	pt->idx[k++] = l++;
      else
	l++;
    }
    pt->begin[i] = pt->xcoord[pt->first[i]];
    pt->end[i] = pt->xcoord[pt->last[i]];
    /* pt->step is average step length.. */
    pt->step[i] = (pt->end[i] - pt->begin[i]) / ((double)nvals[i] - 1);
    pt->invstep[i] = 1.0 / pt->step[i];
  }

  /* read EAM embedding function F(n) */
  for (i = paircol + den_count; i < paircol + den_count + emb_count; i++) {
    if (have_grad) {
      if (2 > fscanf(infile, "%lf %lf\n", val, val + 1))
	error(1, "Premature end of potential file %s", filename);
    } else {
      *val = 1e30;
      *(val + 1) = 1.e30;
    }
    val += 2;
    ord += 2;
    if ((!invar_pot[i]) && (gradient[i] >> 1))
      pt->idx[k++] = l++;
    else
      l++;
    if ((!invar_pot[i]) && (gradient[i] % 2))
      pt->idx[k++] = l++;
    else
      l++;
    /* read values */
    for (j = 0; j < nvals[i]; j++) {
      if (1 > fscanf(infile, "%lf %lf\n", ord, val))
	error(1, "Premature end of potential file %s", filename);
      else {
	ord++;
	val++;
      }
      if ((j > 0) && (*(ord - 1) <= *(ord - 2)))
	error(1, "Abscissa not monotonous in potential %d.", i);
      if (!invar_pot[i])
	pt->idx[k++] = l++;
      else
	l++;
    }
    pt->begin[i] = pt->xcoord[pt->first[i]];
    pt->end[i] = pt->xcoord[pt->last[i]];
    /* pt->step is average step length.. */
    pt->step[i] = (pt->end[i] - pt->begin[i]) / ((double)nvals[i] - 1);
    pt->invstep[i] = 1.0 / pt->step[i];
  }
#endif /* EAM || ADP */

#ifdef ADP
  /* read ADP dipole function u(r) */
  for (i = paircol + 2 * ntypes; i < 2 * (paircol + ntypes); i++) {
    if (have_grad) {
      if (2 > fscanf(infile, "%lf %lf\n", val, val + 1))
	error(1, "Premature end of potential file %s", filename);
    } else {
      *val = 1e30;
      *(val + 1) = 0.0;
    }
    val += 2;
    ord += 2;
    if ((!invar_pot[i]) && (gradient[i] >> 1))
      pt->idx[k++] = l++;
    else
      l++;
    if ((!invar_pot[i]) && (gradient[i] % 2))
      pt->idx[k++] = l++;
    else
      l++;
    /* read values */
    for (j = 0; j < nvals[i]; j++) {
      if (2 > fscanf(infile, "%lf %lf\n", ord, val))
	error(1, "Premature end of potential file %s", filename);
      else {
	ord++;
	val++;
      }
      if ((j > 0) && (*(ord - 1) <= *(ord - 2)))
	error(1, "Abscissa not monotonous in potential %d.", i);
      if ((!invar_pot[i]) && (j < nvals[i] - 1))
	pt->idx[k++] = l++;
      else
	l++;
    }
    pt->begin[i] = pt->xcoord[pt->first[i]];
    pt->end[i] = pt->xcoord[pt->last[i]];
    /* pt->step is average step length.. */
    pt->step[i] = (pt->end[i] - pt->begin[i]) / ((double)nvals[i] - 1);
    pt->invstep[i] = 1.0 / pt->step[i];
  }

  /* read adp quadrupole function w(r) */
  for (i = 2 * (paircol + ntypes); i < 3 * paircol + 2 * ntypes; i++) {
    if (have_grad) {
      if (2 > fscanf(infile, "%lf %lf\n", val, val + 1))
	error(1, "Premature end of potential file %s", filename);
    } else {
      *val = 1e30;
      *(val + 1) = 1.e30;
    }
    val += 2;
    ord += 2;
    if ((!invar_pot[i]) && (gradient[i] >> 1))
      pt->idx[k++] = l++;
    else
      l++;
    if ((!invar_pot[i]) && (gradient[i] % 2))
      pt->idx[k++] = l++;
    else
      l++;
    /* read values */
    for (j = 0; j < nvals[i]; j++) {
      if (1 > fscanf(infile, "%lf %lf\n", ord, val))
	error(1, "Premature end of potential file %s", filename);
      else {
	ord++;
	val++;
      }
      if ((j > 0) && (*(ord - 1) <= *(ord - 2)))
	error(1, "Abscissa not monotonous in potential %d.", i);
      if (!invar_pot[i])
	pt->idx[k++] = l++;
      else
	l++;
    }
    pt->begin[i] = pt->xcoord[pt->first[i]];
    pt->end[i] = pt->xcoord[pt->last[i]];
    /* pt->step is average step length.. */
    pt->step[i] = (pt->end[i] - pt->begin[i]) / ((double)nvals[i] - 1);
    pt->invstep[i] = 1.0 / pt->step[i];
  }
#endif /* ADP */

  pt->idxlen = k;
  init_calc_table4();

  return;
}

/****************************************************************
 *
 *  init_calc_table: Initialize table used for calculation.
 *
 *  *  Header:  one line for each function with
 *           rbegin rstart npoints
 *
 *  Table: Center, width, amplitude of Gaussians,
 *         functions separated by blank lines
 *
 ****************************************************************/

void init_calc_table4()
{
  g_pot.calc_pot.len = g_pot.opt_pot.len;
  g_pot.calc_pot.idxlen = g_pot.opt_pot.idxlen;
  g_pot.calc_pot.ncols = g_pot.opt_pot.ncols;
  g_pot.calc_pot.begin = g_pot.opt_pot.begin;
  g_pot.calc_pot.end = g_pot.opt_pot.end;
  g_pot.calc_pot.step = g_pot.opt_pot.step;
  g_pot.calc_pot.invstep = g_pot.opt_pot.invstep;
  g_pot.calc_pot.first = g_pot.opt_pot.first;
  g_pot.calc_pot.last = g_pot.opt_pot.last;
  g_pot.calc_pot.xcoord = g_pot.opt_pot.xcoord;
  g_pot.calc_pot.table = g_pot.opt_pot.table;
  g_pot.calc_pot.d2tab = g_pot.opt_pot.d2tab;
  g_pot.calc_pot.idx = g_pot.opt_pot.idx;
}
