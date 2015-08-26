/****************************************************************
 *
 * potential_input_f0.c: Routines for reading a potential table
 *
 ****************************************************************
 *
 * Copyright 2002-2015
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

#include "functions.h"
#include "memory.h"
#include "potential_input.h"
#include "utils.h"

#if !defined(APOT)

void read_pot_table0(char const* potential_filename, FILE* pfile) {}

#else

typedef struct
{
  char const* filename;
  FILE* pfile;
  fpos_t startpos;
} apot_state;

void read_chemical_potentials(apot_state* pstate);
void read_elstat_table(apot_state* pstate);
void read_reaxff_potentials(apot_state* pstate);
void read_global_parameters(apot_state* pstate);
void read_analytic_potentials(apot_state* pstate);

void init_calc_table0();

/****************************************************************
 *
 *  read potential in analytic format:
 *  	for more information an how to specify an analytic potential
 *  	please check the documentation
 *
 *  parameters:
 *  	pot_table_t * ... pointer to the potential table
 *  	apot_table_t * ... pointer to the analytic potential table
 *  	char * ... name of the potential file (for error messages)
 *  	FILE * ... open file handle of the potential file
 *
 ****************************************************************/

void read_pot_table0(char const* potential_filename, FILE* pfile)
{
  fpos_t filepos;
  char buffer[255];

  apot_state state;
  apot_table_t* apt = &g_pot.apot_table;
  pot_table_t* pt = &g_pot.opt_pot;

  state.filename = potential_filename;
  state.pfile = pfile;

  /* save starting position */
  fgetpos(pfile, &state.startpos);

  /* initialize the function table for analytic potentials */
  apot_init();
  
  read_chemical_potentials(&state);

  read_elstat_table(&state);

  read_reaxff_potentials(&state);

  read_global_parameters(&state);

  read_analytic_potentials(&state);

#if defined(COULOMB)
  apt->total_ne_par = apt->total_par;
#endif /* COULOMB */

  /* if we have global parameters, are they actually used ? */
  if (g_pot.have_globals)
  {
    int j = 0;
    for (int i = 0; i < apt->globals; i++)
      j += apt->n_glob[i];
    if (j == 0)
    {
      g_pot.have_globals = 0;
      printf("You defined global parameters but did not use them.\n");
      printf("Disabling global parameters.\n\n");
    }
  }

  /* assign the potential functions to the function pointers */
  if (apot_assign_functions(apt) == -1)
    error(1, "Could not assign the function pointers.\n");
#if defined(PAIR)
  if (g_param.enable_cp)
  {
    g_pot.cp_start =
        apt->total_par - apt->globals + g_param.ntypes * (g_param.ntypes + 1);
    apt->total_par += (g_param.ntypes + g_param.compnodes -
                       apt->invar_par[apt->number][g_param.ntypes]);
  }
#endif /* PAIR */

#if defined(COULOMB)
  apt->total_par += g_param.ntypes;
#endif /* COULOMB */

#if defined(DIPOLE)
  apt->total_par += g_param.ntypes * (g_param.ntypes + 2);
#endif /* DIPOLE */

  /* initialize function table and write indirect index */
  for (int i = 0; i < apt->number; i++)
  {
    pt->begin[i] = apt->begin[i];
    pt->end[i] = apt->end[i];
    pt->step[i] = 0;
    pt->invstep[i] = 0;
    if (i == 0)
      pt->first[i] = 2;
    else
      pt->first[i] = pt->last[i - 1] + 3;
    pt->last[i] = pt->first[i] + apt->n_par[i] - 1;
  }
  pt->len = pt->first[apt->number - 1] + apt->n_par[apt->number - 1];
  if (g_pot.have_globals)
    pt->len += apt->globals;

#if defined(PAIR)
  if (g_param.enable_cp)
  {
    pt->len += (g_param.ntypes + g_param.compnodes);
  }
#endif /* PAIR */

#if defined(COULOMB)
  pt->len += 2 * g_param.ntypes - 1;
#endif /* COULOMB */
#if defined(DIPOLE)
  pt->len += g_param.ntypes * (g_param.ntypes + 2);
#endif /* DIPOLE */

// Modification by AI 20.08.2015
  pt->table = (double *)malloc(pt->len * sizeof(double)); 

  g_pot.calc_list = (double *)malloc(pt->len * sizeof(double));

  pt->idx = (int *)malloc(pt->len * sizeof(int));

  apt->idxpot = (int *)malloc(apt->total_par * sizeof(int));

  apt->idxparam = (int *)malloc(apt->total_par * sizeof(int));
// End of modification

  if ((NULL == pt->table) || (NULL == pt->idx) || (apt->idxpot == NULL) || (apt->idxparam == NULL))
    error(1, "Cannot allocate memory for potential table.\n");

  for (int i = 0; i < pt->len; i++) {
    pt->table[i] = 0.0;
    g_pot.calc_list[i] = 0.0;
    pt->idx[i] = 0;
  }

  /* this is the indirect index */
  int k = 0;
  int l = 0;
  double* val = pt->table;
  double* list = g_pot.calc_list;

  for (int i = 0; i < apt->number; i++)
  { /* loop over potentials */
    val += 2;
    list += 2;
    l += 2;
    for (int j = 0; j < apt->n_par[i]; j++)
    { /* loop over parameters */
      *val = apt->values[i][j];
      *list = apt->values[i][j];
      val++;
      list++;
      if (!g_pot.invar_pot[i] && !apt->invar_par[i][j])
      {
        pt->idx[k] = l++;
        apt->idxpot[k] = i;
        apt->idxparam[k++] = j;
      }
      else
        l++;
    }
    if (!g_pot.invar_pot[i])
      pt->idxlen += apt->n_par[i] - apt->invar_par[i][apt->n_par[i]];
    apt->total_par -= apt->invar_par[i][apt->n_par[i]];
  }

#if defined(PAIR)
  if (g_param.enable_cp)
  {
    init_chemical_potential(g_param.ntypes);
    int i = apt->number;
    for (int j = 0; j < (g_param.ntypes + g_param.compnodes); j++)
    {
      *val = apt->values[i][j];
      val++;
      if (!apt->invar_par[i][j])
      {
        pt->idx[k] = l++;
        apt->idxpot[k] = i;
        apt->idxparam[k++] = j;
      }
    }
    pt->idxlen += (g_param.ntypes + g_param.compnodes -
                   apt->invar_par[apt->number][g_param.ntypes]);
    g_pot.global_idx += (g_param.ntypes + g_param.compnodes -
                         apt->invar_par[apt->number][g_param.ntypes]);
  }
#endif /* PAIR */

#if defined(COULOMB)
  int i = apt->number;
  for (int j = 0; j < (g_param.ntypes - 1); j++)
  {
    *val = apt->values[i][j];
    val++;
    if (!apt->invar_par[i][j])
    {
      pt->idx[k] = l++;
      apt->idxpot[k] = i;
      apt->idxparam[k++] = j;
    }
    else
    {
      l++;
      apt->total_par -= apt->invar_par[i][j];
      pt->idxlen -= apt->invar_par[i][j];
    }
  }
  i = apt->number + 1;
  *val = apt->values[i][0];
  val++;
  if (!apt->invar_par[i][0])
  {
    pt->idx[k] = l++;
    apt->idxpot[k] = i;
    apt->idxparam[k++] = 0;
  }
  else
  {
    l++;
    apt->total_par -= apt->invar_par[i][0];
    pt->idxlen -= apt->invar_par[i][0];
  }
  pt->idxlen += g_param.ntypes;
#endif /* COULOMB */

#if defined(DIPOLE)
  i = apt->number + 2;
  for (int j = 0; j < (g_param.ntypes); j++)
  {
    *val = apt->values[i][j];
    val++;
    if (!apt->invar_par[i][j])
    {
      pt->idx[k] = l++;
      apt->idxpot[k] = i;
      apt->idxparam[k++] = j;
    }
    else
    {
      l++;
      apt->total_par -= apt->invar_par[i][j];
      pt->idxlen -= apt->invar_par[i][j];
    }
  }
  for (i = apt->number + 3; i < apt->number + 5; i++)
  {
    for (int j = 0; j < (g_param.ntypes * (g_param.ntypes + 1) / 2); j++)
    {
      *val = apt->values[i][j];
      val++;
      if (!apt->invar_par[i][j]) {
	pt->idx[k] = l++;
	apt->idxpot[k] = i;
	apt->idxparam[k++] = j;
      } else {
	l++;
	apt->total_par -= apt->invar_par[i][j];
	pt->idxlen -= apt->invar_par[i][j];
      }
    }
  }
  pt->idxlen += g_param.ntypes;
  pt->idxlen += (2 * ncols);
#endif /* DIPOLE */

  if (g_pot.have_globals)
  {
    int i = g_pot.global_pot;
    for (int j = 0; j < apt->globals; j++)
    {
      *val = apt->values[i][j];
      *list = apt->values[i][j];
      val++;
      list++;
      if (!apt->invar_par[i][j])
      {
        pt->idx[k] = l++;
        apt->idxpot[k] = i;
        apt->idxparam[k++] = j;
      }
      else
        l++;
    }
    pt->idxlen += apt->globals - apt->invar_par[i][apt->globals];
    apt->total_par -= apt->invar_par[i][apt->globals];
  }
  g_pot.global_idx += pt->last[apt->number - 1] + 1;

#if defined(NOPUNISH)
  if (g_param.opt)
    warning("Gauge degrees of freedom are NOT fixed!\n");
#endif /* NOPUNISH */

  check_apot_functions();

  init_calc_table0();

  return;
}

/****************************************************************
 *
 *  read_chemical_potentials:
 *
 ****************************************************************/

void read_chemical_potentials(apot_state* pstate)
{
#if defined(PAIR)
  apot_table_t* apt = &g_pot.apot_table;

  char buffer[255];
  char name[255];

  fpos_t filepos;

  if (g_param.enable_cp)
  {
    /* search for cp */
    do
    {
      fgetpos(pstate->pfile, &filepos);
      fscanf(pstate->pfile, "%s", buffer);
    } while (strncmp(buffer, "cp", 2) != 0 && !feof(pstate->pfile));

    /* and save the position */
    fsetpos(pstate->pfile, &filepos);

    /* shortcut for apt->number */
    int i = apt->number;

    /* allocate memory for global parameters */
    apt->names = (char**)Realloc(apt->names, (i + 1) * sizeof(char*));
    apt->names[i] = (char*)Malloc(20 * sizeof(char));
    strcpy(apt->names[i], "chemical potentials");

    apt->invar_par = (int**)Realloc(apt->invar_par, (i + 1) * sizeof(int*));
    apt->invar_par[i] = (int*)Malloc((g_param.ntypes + 1) * sizeof(int));

    apt->param_name = (char***)Realloc(apt->param_name, (i + 1) * sizeof(char**));
    apt->param_name[i] = (char**)Malloc(g_param.ntypes * sizeof(char*));

    /* loop over all atom types */
    for (int j = 0; j < g_param.ntypes; j++)
    {
      /* allocate memory for parameter name */
      apt->param_name[i][j] = (char*)Malloc(30 * sizeof(char));

      /* read one line */
      if (4 > fscanf(pstate->pfile, "%s %lf %lf %lf", buffer, &apt->chempot[j],
                     &apt->pmin[i][j], &apt->pmax[i][j]))
        error(1, "Could not read chemical potential for %d. atomtype.", j);

      /* split cp and _# */
      char* token = strchr(buffer, '_');

      if (token != NULL)
      {
        strncpy(name, buffer, strlen(buffer) - strlen(token));
        name[strlen(buffer) - strlen(token)] = '\0';
      }

      if (strcmp("cp", name) != 0)
      {
        fprintf(stderr, "Found \"%s\" instead of \"cp\"\n", name);
        error(1, "No chemical potentials found in %s.\n", pstate->filename);
      }

      /* check for invariance and proper value (respect boundaries) */
      apt->invar_par[i][j] = 0.0;
      /* parameter will not be optimized if min==max */
      if (apt->pmin[i][j] == apt->pmax[i][j])
      {
        apt->invar_par[i][j] = 1;
        apt->invar_par[i][g_param.ntypes]++;
        /* swap min and max if max<min */
      }
      else if (apt->pmin[i][j] > apt->pmax[i][j])
      {
        double temp = apt->pmin[i][j];
        apt->pmin[i][j] = apt->pmax[i][j];
        apt->pmax[i][j] = temp;
        /* reset value if >max or <min */
      }
      else if ((apt->values[i][j] < apt->pmin[i][j]) ||
               (apt->values[i][j] > apt->pmax[i][j]))
      {
        /* Only print warning if we are optimizing */
        if (g_param.opt)
        {
          if (apt->values[i][j] < apt->pmin[i][j])
            apt->values[i][j] = apt->pmin[i][j];
          if (apt->values[i][j] > apt->pmax[i][j])
            apt->values[i][j] = apt->pmax[i][j];
          warning("Starting value for chemical potential #%d is ", j + 1);
          warning("outside of specified adjustment range.\n");
          warning("Resetting it to %f.\n", j + 1, apt->values[i][j]);
          if (apt->values[i][j] == 0)
            warning("New value is 0 ! Please be careful about this.\n");
        }
      }
      strcpy(apt->param_name[i][j], buffer);
    }
    printf(" - Enabled %d chemical potential(s)\n", g_param.ntypes);

/* disable composition nodes for now */
#if defined(CN)
    /* read composition nodes */
    if (2 > fscanf(pstate->pfile, "%s %d", buffer, &compnodes))
    {
      if (strcmp("type", buffer) == 0)
        compnodes = -1;
      else
        error(1,
              "Could not read number of composition nodes from potential "
              "file.\n");
    }
    if (strcmp(buffer, "cn") != 0 && g_param.ntypes > 1 && g_param.compnodes != -1)
      error(1, "No composition nodes found in %s.\nUse \"cn 0\" for none.\n",
            pstate->filename);
    if (g_param.ntypes == 1)
    {
      compnodes = 0;
    }
    if (compnodes != -1)
    {
      apt->values[apt->number] =
          (double*)Realloc(apt->values[apt->number],
                           (g_param.ntypes + g_param.compnodes) * sizeof(double));
      apt->pmin[apt->number] = (double*)Realloc(
          apt->pmin[apt->number], (g_param.ntypes + g_param.compnodes) * sizeof(double));
      apt->pmax[apt->number] = (double*)Realloc(
          apt->pmax[apt->number], (g_param.ntypes + g_param.compnodes) * sizeof(double));
      apt->chempot = apt->values[apt->number];
      compnodelist =
          (double*)Malloc((g_param.ntypes + g_param.compnodes) * sizeof(double));

      for (j = 0; j < compnodes; j++)
      {
        if (4 > fscanf(pstate->pfile, "%lf %lf %lf %lf", &compnodelist[j],
                       &apt->chempot[ntypes + j], &apt->pmin[apt->number][ntypes + j],
                       &apt->pmax[apt->number][ntypes + j]))
          error(1, "Could not read composition node %d\n", j + 1);
        if (apt->pmin[apt->number][ntypes + j] > apt->chempot[ntypes + j] ||
            apt->pmax[apt->number][ntypes + j] < apt->chempot[ntypes + j])
          error(1, "composition node %d is out of bounds.\n", j + 1);
      }

      /* check compnodes for valid values */
      if (g_param.ntypes == 2)
      {
        for (j = 0; j < compnodes; j++)
          if (compnodelist[j] > 1 || compnodelist[j] < 0)
            error(1, "Composition node %d is %f but should be inside [0,1].\n", j + 1,
                  compnodelist[j]);
      }
    }
    if (compnodes != -1)
      printf("Enabled chemical potentials with %d extra composition node(s).\n",
             compnodes);
    if (compnodes == -1)
      compnodes = 0;
#endif  // CN
  }
#endif  // PAIR
}

/****************************************************************
 *
 *  read_elstat_table:
 *      bla bla
 *
 ****************************************************************/

void read_elstat_table(apot_state* pstate)
{
#if defined(COULOMB)
  char buffer[255];
  fpos_t filepos;

  fsetpos(pstate->pfile, &pstate->startpos);
  /* skip to electrostatic section */
  do
  {
    fgetpos(pstate->pfile, &filepos);
    fscanf(pstate->pfile, "%s", buffer);
  } while (strcmp(buffer, "elstat") != 0 && !feof(pstate->pfile));

  /* check for elstat keyword */
  if (strcmp("elstat", buffer) != 0)
  {
    error(1, "No elstat option found in %s.\n", pstate->filename);
  }

  /* read electrostatic parameters */
  fscanf(pstate->pfile, " %s", buffer);
  if (strcmp("ratio", buffer) != 0)
  {
    error(1, "Could not read ratio");
  }

  apot_table_t* apt = &g_pot.apot_table;

  for (int i = 0; i < g_param.ntypes; i++)
  {
    if (1 > fscanf(pstate->pfile, "%lf", &apt->ratio[i]))
    {
      error(1, "Could not read ratio for atomtype #%d\n", i);
    }
  }
  for (int i = 0; i < g_param.ntypes - 1; i++)
  {
    apt->param_name[apt->number][i] = (char*)Malloc(30 * sizeof(char));
    if (4 > fscanf(pstate->pfile, "%s %lf %lf %lf", apt->param_name[apt->number][i],
                   &apt->charge[i], &apt->pmin[apt->number][i],
                   &apt->pmax[apt->number][i]))
    {
      error(1, "Could not read charge for atomtype #%d\n", i);
    }
    apt->invar_par[apt->number][i] = 0;
    if (apt->pmin[apt->number][i] == apt->pmax[apt->number][i])
    {
      apt->invar_par[apt->number][i]++;
    }
  }
  apt->param_name[apt->number + 1][0] = (char*)Malloc(30 * sizeof(char));
  if (4 > fscanf(pstate->pfile, "%s %lf %lf %lf", apt->param_name[apt->number + 1][0],
                 &apt->dp_kappa[0], &apt->pmin[apt->number + 1][0],
                 &apt->pmax[apt->number + 1][0]))
  {
    error(1, "Could not read kappa");
  }
  apt->invar_par[apt->number + 1][0] = 0;
  if (apt->pmin[apt->number + 1][0] == apt->pmax[apt->number + 1][0])
  {
    apt->invar_par[apt->number + 1][0]++;
  }
  apt->sw_kappa = apt->invar_par[apt->number + 1][0];
#if !defined(DIPOLE)
  printf(" - Read elstat table\n");
#endif /* !DIPOLE */
#endif /* COULOMB */

#if defined(DIPOLE)
  int ncols = g_param.ntypes * (g_param.ntypes + 1) / 2;

  for (int i = 0; i < g_param.ntypes; i++)
  {
    apt->param_name[apt->number + 2][i] = (char*)Malloc(30 * sizeof(char));
    if (4 > fscanf(pstate->pfile, "%s %lf %lf %lf", apt->param_name[apt->number + 2][i],
                   &apt->dp_alpha[i], &apt->pmin[apt->number + 2][i],
                   &apt->pmax[apt->number + 2][i]))
    {
      error(1, "Could not read polarisability for atomtype #%d\n", i);
    }
    apt->invar_par[apt->number + 2][i] = 0;
    if (apt->pmin[apt->number + 2][i] == apt->pmax[apt->number + 2][i])
    {
      apt->invar_par[apt->number + 2][i]++;
    }
  }
  for (int i = 0; i < ncols; i++)
  {
    apt->param_name[apt->number + 3][i] = (char*)Malloc(30 * sizeof(char));
    if (4 > fscanf(pstate->pfile, "%s %lf %lf %lf", apt->param_name[apt->number + 3][i],
                   &apt->dp_b[i], &apt->pmin[apt->number + 3][i],
                   &apt->pmax[apt->number + 3][i]))
    {
      error(1, "Could not read parameter dp_b for potential #%d\n", i);
    }
    apt->invar_par[apt->number + 3][i] = 0;
    if (apt->pmin[apt->number + 3][i] == apt->pmax[apt->number + 3][i])
    {
      apt->invar_par[apt->number + 3][i]++;
    }
  }
  for (int i = 0; i < ncols; i++)
  {
    apt->param_name[apt->number + 4][i] = (char*)Malloc(30 * sizeof(char));
    if (4 > fscanf(pstate->pfile, "%s %lf %lf %lf", apt->param_name[apt->number + 4][i],
                   &apt->dp_c[i], &apt->pmin[apt->number + 4][i],
                   &apt->pmax[apt->number + 4][i]))
    {
      error(1, "Could not read parameter dp_c for potential #%d\n", i);
    }
    apt->invar_par[apt->number + 4][i] = 0;
    if (apt->pmin[apt->number + 4][i] == apt->pmax[apt->number + 4][i])
    {
      apt->invar_par[apt->number + 4][i]++;
    }
  }

  printf(" - Read elstat table\n");
#endif /* DIPOLE */
}

/****************************************************************
 *
 *  read_global_parameters:
 *      bla bla
 *
 ****************************************************************/

void read_global_parameters(apot_state* pstate)
{
  apot_table_t* apt = &g_pot.apot_table;

  char buffer[255];

  fpos_t filepos;

  pot_table_t* pt = &g_pot.opt_pot;

  /* skip to global section */
  fsetpos(pstate->pfile, &pstate->startpos);
  do
  {
    fgetpos(pstate->pfile, &filepos);
    fscanf(pstate->pfile, "%s", buffer);
  } while (strcmp(buffer, "global") != 0 && !feof(pstate->pfile));
  fsetpos(pstate->pfile, &filepos);

  /* check for global keyword */
  if (strcmp(buffer, "global") == 0)
  {
    if (2 > fscanf(pstate->pfile, "%s %d", buffer, &apt->globals))
      error(1, "Premature end of potential file %s", pstate->filename);
    g_pot.have_globals = 1;
    apt->total_par += apt->globals;

    int i = apt->number + g_param.enable_cp;
    int j = apt->globals;
    g_pot.global_pot = i;

    /* allocate memory for global parameters */
    apt->names = (char**)Realloc(apt->names, (g_pot.global_pot + 1) * sizeof(char*));
    apt->names[g_pot.global_pot] = (char*)Malloc(20 * sizeof(char));
    strcpy(apt->names[g_pot.global_pot], "global parameters");

    apt->n_glob = (int*)Malloc(apt->globals * sizeof(int));

    apt->global_idx = (int***)Malloc(apt->globals * sizeof(int**));

    apt->values =
        (double**)Realloc(apt->values, (g_pot.global_pot + 1) * sizeof(double*));
    apt->values[g_pot.global_pot] = (double*)Malloc(j * sizeof(double));

    apt->invar_par =
        (int**)Realloc(apt->invar_par, (g_pot.global_pot + 1) * sizeof(int*));
    apt->invar_par[g_pot.global_pot] = (int*)Malloc((j + 1) * sizeof(int));

    apt->pmin = (double**)Realloc(apt->pmin, (g_pot.global_pot + 1) * sizeof(double*));
    apt->pmin[g_pot.global_pot] = (double*)Malloc(j * sizeof(double));

    apt->pmax = (double**)Realloc(apt->pmax, (g_pot.global_pot + 1) * sizeof(double*));
    apt->pmax[g_pot.global_pot] = (double*)Malloc(j * sizeof(double));

    apt->param_name =
        (char***)Realloc(apt->param_name, (g_pot.global_pot + 1) * sizeof(char**));
    apt->param_name[g_pot.global_pot] = (char**)Malloc(j * sizeof(char*));

    pt->first = (int*)Realloc(pt->first, (g_pot.global_pot + 1) * sizeof(int));

    /* read the global parameters */
    for (j = 0; j < apt->globals; j++)
    {
      apt->param_name[g_pot.global_pot][j] = (char*)Malloc(30 * sizeof(char));

      strcpy(apt->param_name[g_pot.global_pot][j], "\0");
      int ret_val =
          fscanf(pstate->pfile, "%s %lf %lf %lf", apt->param_name[g_pot.global_pot][j],
                 &apt->values[g_pot.global_pot][j], &apt->pmin[g_pot.global_pot][j],
                 &apt->pmax[g_pot.global_pot][j]);
      if (4 > ret_val)
        if (strcmp(apt->param_name[g_pot.global_pot][j], "type") == 0)
        {
          error(0, "Not enough global parameters!\n");
          error(1, "You specified %d parameter(s), but needed are %d.\nAborting", j,
                apt->globals);
        }

      /* check for duplicate names */
      for (int k = j - 1; k >= 0; k--)
      {
        if (strcmp(apt->param_name[g_pot.global_pot][j],
                   apt->param_name[g_pot.global_pot][k]) == 0)
        {
          error(0, "\nFound duplicate global parameter name!\n");
          error(1, "Parameter #%d (%s) is the same as #%d (%s)\n", j + 1,
                apt->param_name[g_pot.global_pot][j], k + 1,
                apt->param_name[g_pot.global_pot][k]);
        }
      }

      apt->n_glob[j] = 0;

      /* check for invariance and proper value (respect boundaries) */
      /* parameter will not be optimized if min==max */
      apt->invar_par[i][j] = 0;

      if (apt->pmin[i][j] == apt->pmax[i][j])
      {
        apt->invar_par[i][j] = 1;
        apt->invar_par[i][apt->globals]++;
      }
      else if (apt->pmin[i][j] > apt->pmax[i][j])
      {
        double temp = apt->pmin[i][j];
        apt->pmin[i][j] = apt->pmax[i][j];
        apt->pmax[i][j] = temp;
      }
      else if ((apt->values[i][j] < apt->pmin[i][j]) ||
               (apt->values[i][j] > apt->pmax[i][j]))
      {
        /* Only print warning if we are optimizing */
        if (g_param.opt)
        {
          if (apt->values[i][j] < apt->pmin[i][j])
            apt->values[i][j] = apt->pmin[i][j];
          if (apt->values[i][j] > apt->pmax[i][j])
            apt->values[i][j] = apt->pmax[i][j];
          warning("Starting value for global parameter #%d is ", j + 1);
          warning("outside of specified adjustment range.\n");
          warning("Resetting it to %f.\n", j + 1, apt->values[i][j]);
          if (apt->values[i][j] == 0)
            warning("New value is 0 ! Please be careful about this.\n");
        }
      }
    }

    printf(" - Read %d global parameter(s)\n", apt->globals);
  }
}

void read_reaxff_potentials(apot_state* pstate)
{
// Edited by Kubo 20120523 ===================================//
// Addition --------------------------------------------------//
#ifdef LMP
  apot_table_t* apt = &g_pot.apot_table;

  char buffer[255];

  fpos_t filepos;

  fsetpos(pstate->pfile, &pstate->startpos);
  /* skip to reaxff section */
  do {
    fgetpos(pstate->pfile, &filepos);
    fscanf(pstate->pfile, "%s", buffer);
  } while (strcmp(buffer, "reaxff") != 0 && !feof(pstate->pfile));

  /* check for reaxff keyword */
  if(strcmp("reaxff",buffer)!=0){error(1, "No reaxff option found in %s.\n", pstate->filename);}

  int ib,i;
  int comb;
  int sr0;
  char paramtag[30];
  /* read ReaxFF parameters */
// 0-body
  sr0 = apt->rf_sr0;
//  comb = 1;
  comb = apt->rf_comb0;
  for(ib=0;ib<sr0;ib++){ // vpar
    for(i=0;i<comb;i++){
      apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
      strcpy(apt->param_name[apt->number+ib][i],"");
      if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
            &apt->vpar[ib][i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
        error(1, "Could not read parameter for atomtype #%d\n", i);
        error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
      }
//      printf(" %d, %s\n",ib,apt->param_name[apt->number+ib][i]); // For DEBUG
      apt->invar_par[apt->number+ib][i] = 0;
      if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
        apt->invar_par[apt->number+ib][i]++;
      }
      ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
    }
  }
  ib = sr0;

// 1-body --------------------//
//  comb = ntypes;
  comb = apt->rf_comb1;
  for(i=0;i<comb;i++){ // rat
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rat[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
//    printf(" %d, %s\n",ib,apt->param_name[apt->number+ib][i]); // For DEBUG
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // aval
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->aval[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
//    printf(" %d, %s\n",ib,apt->param_name[apt->number+ib][i]); // For DEBUG
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // amas
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->amas[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
//    printf(" %d, %s\n",ib,apt->param_name[apt->number+ib][i]); // For DEBUG
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
//    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // rvdw
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rvdw[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
//    printf(" %d, %s\n",ib,apt->param_name[apt->number+ib][i]); // For DEBUG
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // eps
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->eps[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
//    printf(" %d, %s\n",ib,apt->param_name[apt->number+ib][i]); // For DEBUG
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // gam
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->gam[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
//    printf(" %d, %s\n",ib,apt->param_name[apt->number+ib][i]); // For DEBUG
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // rapt
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rapt[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // stlp
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->stlp[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // alf
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->alf[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vop
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vop[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // valf
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->valf[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // valp1
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->valp1[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // valp2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->valp2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // chi
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->chi[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // eta
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->eta[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vnphb
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vnphb[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vnq
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vnq[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vlp1
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vlp1[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vincr
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vincr[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // bo131
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->bo131[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // bo132
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->bo132[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // bo133
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->bo133[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // sigqeq
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->sigqeq[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // def
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->def[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vovun
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vovun[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vval1
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vval1[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vrom
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vrom[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vval3
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vval3[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vval4
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vval4[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // rcore2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rcore2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // ecore2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->ecore2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // acore2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->acore2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;

// 2-body --------------------//
//  comb = ntypes*(ntypes+1)/2;
  comb = apt->rf_comb2;
  for(i=0;i<comb;i++){ // de1
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->de1[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    ////reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // de2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->de2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // de3
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->de3[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // psi
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->psi[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // pdo
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->pdo[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // v13cor
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->v13cor[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // popi
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->popi[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vover
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vover[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // psp
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->psp[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // pdp
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->pdp[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // ptp
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->ptp[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // bom
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->bom[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // bop1
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->bop1[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // bop2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->bop2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // ovc
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->ovc[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vuncor
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vuncor[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
// Off-Diagonal --------------------//
  comb = apt->rf_combO;
  for(i=0;i<comb;i++){ // deodmh
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->deodmh[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // rodmh
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rodmh[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // godmh
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->godmh[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // rsig
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rsig[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;

  for(i=0;i<comb;i++){ // rpi
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rpi[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // rpi2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rpi2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
/*
// Hydrogen Bond --------------------//
  for(i=0;i<comb;i++){ // rhb
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rhb[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // dehb
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->dehb[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vhb1
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vhb1[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vhb2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vhb2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
*/

// 3-body --------------------//
//  comb = ntypes*ntypes*(ntypes+1)/2;
  comb = apt->rf_comb3;
  for(i=0;i<comb;i++){ // th0
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->th0[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vka
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vka[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vka3
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vka3[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vka8
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vka8[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vkac
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vkac[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vkap
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vkap[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vval2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vval2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;

// 4-body
//  comb = ntypes*ntypes*(ntypes*ntypes+1)/2;
  comb = apt->rf_comb4;
  for(i=0;i<comb;i++){ // v1
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->v1[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // v2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->v2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // v3
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->v3[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // v4
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->v4[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vconj
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vconj[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // v2bo
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->v2bo[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // v3bo
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->v3bo[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;

// Hydrogen Bond --------------------//
//  comb = ntypes*ntypes*ntypes;
  comb = apt->rf_combH;
  for(i=0;i<comb;i++){ // rhb
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->rhb[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // dehb
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->dehb[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vhb1
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vhb1[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;
  for(i=0;i<comb;i++){ // vhb2
    apt->param_name[apt->number+ib][i] = (char*)malloc(30*sizeof(char));
    if(4>fscanf(pstate->pfile,"%s %lf %lf %lf",apt->param_name[apt->number+ib][i],
          &apt->vhb2[i],&apt->pmin[apt->number+ib][i],&apt->pmax[apt->number+ib][i])){
//      error(1, "Could not read parameter for atomtype #%d\n", i);
      error(1, "Could not read parameter for atomtype #%d (%d)\n", i,ib);
    }
//    printf("%s %f\n",apt->param_name[apt->number+ib][i],apt->vhb2[i]);
    apt->invar_par[apt->number+ib][i] = 0;
    if(apt->pmin[apt->number+ib][i]==apt->pmax[apt->number+ib][i]){
      apt->invar_par[apt->number+ib][i]++;
    }
    //reg_for_free(apt->param_name[apt->number+ib][i],"apt->param_name[%d][%d]",apt->number+ib,i);
  }
  ib++;

#endif /* LMP */
// End of Edition ============================================//
}


/****************************************************************
 *
 *  read_analytic_potentials:
 *      bla bla
 *
 ****************************************************************/

void read_analytic_potentials(apot_state* pstate)
{
  apot_table_t* apt = &g_pot.apot_table;

  char buffer[255];
  char name[255];

  fpos_t filepos;

  /* skip to actual potentials */
  fsetpos(pstate->pfile, &pstate->startpos);
  do
  {
    fgetpos(pstate->pfile, &filepos);
    fscanf(pstate->pfile, "%s", buffer);
  } while (strcmp(buffer, "type") != 0 && !feof(pstate->pfile));
  fsetpos(pstate->pfile, &filepos);

  for (int i = 0; i < apt->number; i++)
  {
    /* scan for "type" keyword */
    do
    {
      fgetpos(pstate->pfile, &filepos);
      fscanf(pstate->pfile, "%s", buffer);
    } while (strcmp(buffer, "type") != 0 && !feof(pstate->pfile));
    fsetpos(pstate->pfile, &filepos);

    /* read type */
    if (2 > fscanf(pstate->pfile, "%s %s", buffer, name))
      error(1, "Premature end of potential file %s", pstate->filename);
    if (strcmp(buffer, "type") != 0)
      error(1, "Unknown keyword in file %s, expected \"type\" but found \"%s\".",
            pstate->filename, buffer);

    /* split name and _sc */
    char* token = strrchr(name, '_');
    if (token != NULL && strcmp(token + 1, "sc") == 0)
    {
      strncpy(buffer, name, strlen(name) - 3);
      buffer[strlen(name) - 3] = '\0';
      strcpy(name, buffer);
      g_pot.smooth_pot[i] = 1;
    }

    /* check if potential is "pohlong" and change it to bjs */
    if (strcmp(name, "pohlong") == 0)
      strcpy(name, "bjs\0");

    if (apot_parameters(name) == -1)
      error(1,
            "Unknown function type in file %s, please define \"%s\" in "
            "functions.c.",
            pstate->filename, name);

    strcpy(apt->names[i], name);
    apt->n_par[i] = apot_parameters(name);

    /* add one parameter for cutoff function if _sc is found */
    if (g_pot.smooth_pot[i] == 1)
      apt->n_par[i]++;
    apt->total_par += apt->n_par[i];

    /* read cutoff */
    if (2 > fscanf(pstate->pfile, "%s %lf", buffer, &apt->end[i]))
      error(1, "Could not read cutoff for potential #%d in file %s\nAborting", i,
            pstate->filename);
    if (strcmp(buffer, "cutoff") != 0)
      error(1,
            "No cutoff found for the %d. potential (%s) after \"type\" in file "
            "%s.\nAborting",
            i + 1, apt->names[i], pstate->filename);

    /* set very small begin, needed for EAM embedding function */
    apt->begin[i] = 0.0001;

    /* allocate memory for this parameter */
    apt->values[i] = (double*)Malloc(apt->n_par[i] * sizeof(double));

    apt->invar_par[i] = (int*)Malloc((apt->n_par[i] + 1) * sizeof(int));

    apt->pmin[i] = (double*)Malloc(apt->n_par[i] * sizeof(double));

    apt->pmax[i] = (double*)Malloc(apt->n_par[i] * sizeof(double));

    apt->param_name[i] = (char**)Malloc(apt->n_par[i] * sizeof(char*));

    char c = 0;

    /* check for comments */
    do
    {
      c = fgetc(pstate->pfile);
    } while (c != 10);

    fgetpos(pstate->pfile, &filepos);

    char* ret = fgets(buffer, 255, pstate->pfile);
    while (buffer[0] == '#')
    {
      fgetpos(pstate->pfile, &filepos);
      ret = fgets(buffer, 255, pstate->pfile);
    }
    fsetpos(pstate->pfile, &filepos);

    /* read parameters */
    apt->invar_par[i][apt->n_par[i]] = 0;

    for (int j = 0; j < apt->n_par[i]; j++)
    {
      apt->param_name[i][j] = (char*)Malloc(30 * sizeof(char));

      strcpy(apt->param_name[i][j], "empty");

      fgetpos(pstate->pfile, &filepos);

      ret = fgets(name, 255, pstate->pfile);

      while (name[0] == '#' && !feof(pstate->pfile) && (j != apt->n_par[i] - 1))
      {
        ret = fgets(name, 255, pstate->pfile);
      }

      if ((j != (apt->n_par[i] - 1)) && (feof(pstate->pfile) || name[0] == '\0'))
      {
        error(0, "Premature end of potential definition or file.\n");
        error(1, "Probably your potential definition is missing some parameters.\n");
      }

      if (feof(pstate->pfile))
        name[0] = '\0';

      buffer[0] = '\0';

      int ret_val = sscanf(name, "%s %lf %lf %lf", buffer, &apt->values[i][j],
                           &apt->pmin[i][j], &apt->pmax[i][j]);

      if (buffer[0] != '\0')
        strncpy(apt->param_name[i][j], buffer, 30);

      /* if last char of name is "!" we have a global parameter */
      if (strrchr(apt->param_name[i][j], '!') != NULL)
      {
        apt->param_name[i][j][strlen(apt->param_name[i][j]) - 1] = '\0';
        int l = -1;
        for (int k = 0; k < apt->globals; k++)
        {
          if (strcmp(apt->param_name[i][j], apt->param_name[g_pot.global_pot][k]) == 0)
            l = k;
        }

        if (-1 == l)
          error(1, "Could not find global parameter %s!\n", apt->param_name[i][j]);

        sprintf(apt->param_name[i][j], "%s!", apt->param_name[i][j]);

        /* write index array for global parameters */
        apt->n_glob[l]++;

        apt->global_idx[l] =
            (int**)Realloc(apt->global_idx[l], apt->n_glob[l] * sizeof(int*));

        apt->global_idx[l][apt->n_glob[l] - 1] = (int*)Malloc(2 * sizeof(int));
        apt->global_idx[l][apt->n_glob[l] - 1][0] = i;
        apt->global_idx[l][apt->n_glob[l] - 1][1] = j;

        apt->values[i][j] = apt->values[g_pot.global_pot][l];
        apt->pmin[i][j] = apt->pmin[g_pot.global_pot][l];
        apt->pmax[i][j] = apt->pmax[g_pot.global_pot][l];
        apt->invar_par[i][j] = 1;
        apt->invar_par[i][apt->n_par[i]]++;
      }
      else
      {
        /* this is no global parameter */
        if (4 > ret_val)
        {
          if (g_pot.smooth_pot[i] && j == apot_parameters(apt->names[i]))
          {
            if (0 == strcmp(apt->param_name[i][j], "type") ||
                0 == strcmp(apt->param_name[i][j], "empty") || feof(pstate->pfile))
            {
              warning(
                  "No cutoff parameter given for potential #%d: adding one "
                  "parameter.\n",
                  i);
              strcpy(apt->param_name[i][j], "h");
              apt->values[i][j] = 1;
              apt->pmin[i][j] = 0.5;
              apt->pmax[i][j] = 2;
              fsetpos(pstate->pfile, &filepos);
            }
          }
          else
          {
            if (strcmp(apt->param_name[i][j], "type") == 0 || ret_val == EOF)
            {
              error(0, "Not enough parameters for potential #%d (%s) in file %s!\n",
                    i + 1, apt->names[i], pstate->filename);
              error(1, "You specified %d parameter(s), but required are %d.\n", j,
                    apt->n_par[i]);
            }
            error(1, "Could not read parameter #%d of potential #%d in file %s", j + 1,
                  i + 1, pstate->filename);
          }
        }

        /* check for invariance and proper value (respect boundaries) */
        /* parameter will not be optimized if min==max */
        apt->invar_par[i][j] = 0;

        if (apt->pmin[i][j] == apt->pmax[i][j])
        {
          apt->invar_par[i][j] = 1;
          apt->invar_par[i][apt->n_par[i]]++;
        }
        else if (apt->pmin[i][j] > apt->pmax[i][j])
        {
          double temp = apt->pmin[i][j];
          apt->pmin[i][j] = apt->pmax[i][j];
          apt->pmax[i][j] = temp;
        }
        else if ((apt->values[i][j] < apt->pmin[i][j]) ||
                 (apt->values[i][j] > apt->pmax[i][j]))
        {
          /* Only print warning if we are optimizing */
          if (g_param.opt)
          {
            if (apt->values[i][j] < apt->pmin[i][j])
              apt->values[i][j] = apt->pmin[i][j];
            if (apt->values[i][j] > apt->pmax[i][j])
              apt->values[i][j] = apt->pmax[i][j];
            warning("Starting value for parameter #%d in potential #%d is ", j + 1,
                    i + 1);
            warning("outside of specified adjustment range.\n");
            warning("Resetting it to %f.\n", apt->values[i][j]);
            if (apt->values[i][j] == 0)
              warning("New value is 0 ! Please be careful about this.\n");
          }
        }
      }
    }
  }
  printf(" - Successfully read %d potential table(s)\n", apt->number);
}

/****************************************************************
 *
 *  init_calc_table0: Initialize table used for calculation.
 *
 ****************************************************************/

void init_calc_table0()
{
  const int size = g_pot.apot_table.number;

  pot_table_t* calc = &g_pot.calc_pot;
  pot_table_t* opt = &g_pot.opt_pot;

  calc->len = size * APOT_STEPS + 2 * opt->ncols + g_param.ntypes + g_param.compnodes;
  calc->idxlen = APOT_STEPS;
  calc->ncols = opt->ncols;
  calc->begin = opt->begin;
  calc->end = opt->end;
  calc->first = (int*)Malloc(size * sizeof(int));
  calc->last = (int*)Malloc(size * sizeof(int));
  calc->step = (double*)Malloc(size * sizeof(double));
  calc->invstep = (double*)Malloc(size * sizeof(double));
  calc->xcoord = (double*)Malloc(calc->len * sizeof(double));
  calc->table = (double*)Malloc(calc->len * sizeof(double));
  printf("Allocated %d bytes for g_pot.calc_pot.table @ %p\n", calc->len, calc->table);
  calc->d2tab = (double*)Malloc(calc->len * sizeof(double));
  calc->idx = (int*)Malloc(calc->len * sizeof(int));

  double f = 0;
  int x = 0;

  /* initialize the g_pot.calc_pot table */
  for (int i = 0; i < size; i++)
  {
    double* val = g_pot.apot_table.values[i];
    double h = g_pot.apot_table.values[i][g_pot.apot_table.n_par[i] - 1];
    calc->table[i * APOT_STEPS + i * 2] = 10e30;
    calc->table[i * APOT_STEPS + i * 2 + 1] = 0;
    calc->first[i] = (x += 2);
    calc->last[i] = (x += APOT_STEPS - 1);
    x++;
    calc->step[i] = (calc->end[i] - calc->begin[i]) / (APOT_STEPS - 1);
    calc->invstep[i] = 1.0 / calc->step[i];

    for (int j = 0; j < APOT_STEPS; j++)
    {
      int index = i * APOT_STEPS + (i + 1) * 2 + j;
      calc->xcoord[index] = calc->begin[i] + j * calc->step[i];

      g_pot.apot_table.fvalue[i](calc->xcoord[index], val, &f);
      calc->table[index] =
          g_pot.smooth_pot[i] ? f * cutoff(calc->xcoord[index], calc->begin[i], h) : f;
      calc->idx[i * APOT_STEPS + j] = index;
    }
  }
}

#endif /* APOT */
