/****************************************************************
 *
 * potfit.c: Contains main potfit program
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

#include <time.h>

#include "potfit.h"

#include "config.h"
#include "errors.h"
#include "forces.h"
#include "functions.h"
#include "mpi_utils.h"
#include "optimize.h"
#include "params.h"
#include "potential_input.h"
#include "potential_output.h"
#include "random.h"
#include "utils.h"

void read_input_files(int argc, char** argv);
void allocate_global_variables();
void start_mpi_worker(double* force);
void free_global_variables();
void init_interaction_name(const char* name);

potfit_calculation g_calc;
potfit_configurations g_config;
potfit_filenames g_files;
potfit_mpi_config g_mpi;
potfit_parameters g_param;
potfit_potentials g_pot;
potfit_memory g_memory;
potfit_unknown g_todo;

/****************************************************************
 *
 *  main potfit routine
 *
 ****************************************************************/

int main(int argc, char** argv)
{
  allocate_global_variables();

#if defined(MPI)
  if (init_mpi(&argc, &argv) != MPI_SUCCESS) return EXIT_FAILURE;
#else
  printf("This is %s compiled on %s, %s.\n\n", POTFIT_VERSION, __DATE__, __TIME__);
#endif  // MPI

  read_input_files(argc, argv);

#if defined(MPI)
  /* let the others know what's going on */
  broadcast_params_mpi();
#else
  /* Identify subset of atoms/volumes belonging to individual process with
   * complete set of atoms/volumes */
  g_config.conf_atoms = g_config.atoms;
  g_config.conf_vol = g_config.volume;
  g_config.conf_uf = g_config.useforce;
#ifdef STRESS
  g_config.conf_us = g_config.usestress;
#endif /* STRESS */
#endif /* MPI */

  g_calc.ndim = g_pot.opt_pot.idxlen;
  g_calc.ndimtot = g_pot.opt_pot.len;
  g_todo.idx = g_pot.opt_pot.idx;

  /* main force vector, all forces, energies, stresses, ... will be stored here
   */
  double* force = (double*)malloc(g_calc.mdim * sizeof(double));

  if (force == NULL) error(1, "Could not allocate memory for main force vector.");

  memset(force, 0, g_calc.mdim * sizeof(double));

  reg_for_free(force, "force vector");

  /* starting positions for the force vector */
  set_force_vector_pointers();

#if defined(APOT)
#if defined(MPI)
  MPI_Bcast(g_pot.opt_pot.table, g_calc.ndimtot, MPI_DOUBLE, 0, MPI_COMM_WORLD);
#endif /* MPI */
  update_calc_table(g_pot.opt_pot.table, g_pot.calc_pot.table, 1);
#endif /* APOT */

  if (g_mpi.myid > 0) {
    start_mpi_worker(force);
  } else {
#if defined(MPI)
    if (g_mpi.num_cpus > g_config.nconf) {
      warning("You are using more CPUs than you have configurations!\n");
      warning("While this will not do any harm, you are wasting %d CPUs.\n",
              g_mpi.num_cpus - g_config.nconf);
    }
#endif /* MPI */

    time_t start_time;
    time_t end_time;

    time(&start_time);

    if (g_param.opt && g_calc.ndim > 0) {
      run_optimization();
    } else if (g_calc.ndim == 0) {
      printf(
          "\nOptimization disabled due to 0 free parameters. Calculating "
          "errors.\n");
    } else {
      printf("\nOptimization disabled. Calculating errors.\n\n");
    }

    time(&end_time);

#if defined(APOT)
    double tot = g_calc_forces(g_pot.opt_pot.table, force, 0);
#else
    double tot = g_calc_forces(g_pot.calc_pot.table, force, 0);
#endif /* APOT */

    write_pot_table_potfit(g_files.endpot);

    printf("\nPotential in format %d written to file \t%s\n", g_pot.format,
           g_files.endpot);

    if (g_param.writeimd == 1) write_pot_table_imd(g_files.imdpot);

    // TODO
    //     if (g_param.plot == 1)
    //       write_plotpot_pair(&g_pot.calc_pot, g_files.plotfile);

    if (g_param.write_lammps == 1) write_pot_table_lammps();

/* will not work with MPI */
#if defined(PDIST) && !defined(MPI)
    write_pairdist(&g_pot.opt_pot, g_files.distfile);
#endif /* PDIST && !MPI */

    /* write the error files for forces, energies, stresses, ... */
    write_errors(force, tot);

    /* calculate total runtime */
    if (g_param.opt && g_mpi.myid == 0 && g_calc.ndim > 0) {
      printf("\nRuntime: %d hours, %d minutes and %d seconds.\n",
             (int)difftime(end_time, start_time) / 3600,
             ((int)difftime(end_time, start_time) % 3600) / 60,
             (int)difftime(end_time, start_time) % 60);
      printf("%d force calculations, each took %f seconds\n", g_calc.fcalls,
             (double)difftime(end_time, start_time) / g_calc.fcalls);
    }

#ifdef MPI
    g_calc_forces(NULL, NULL, 1); /* go wake up other threads */
#endif                            /* MPI */
  }                               /* myid == 0 */

/* do some cleanups before exiting */
#ifdef MPI
  /* kill MPI */
  shutdown_mpi();
#endif /* MPI */

  free(g_memory.u_address);
  free_all_pointers();

  free_global_variables();

  return 0;
}

/****************************************************************
 *
 *  read_input_files -- process all input files
 *
 ****************************************************************/

void read_input_files(int argc, char** argv)
{
  // only root process reads input files
  if (g_mpi.myid == 0) {
    read_parameters(argc, argv);

    read_pot_table(g_files.startpot);

    read_config(g_files.config);

    printf("Global energy weight: %f\n", g_param.eweight);
#if defined(STRESS)
    printf("Global stress weight: %f\n", g_param.sweight);
#endif /* STRESS */

    /* initialize additional force variables and parameters */
    init_forces(0);

    g_todo.init_done = 1;

    init_rng(g_param.rng_seed);
  }
}

/****************************************************************
 *
 *  allocate_global_variables -- initialize global variables and allocate memory
 *
 ****************************************************************/

void allocate_global_variables()
{
  memset(&g_calc, 0, sizeof(g_calc));
  memset(&g_config, 0, sizeof(g_config));
  memset(&g_files, 0, sizeof(g_files));

  g_mpi.myid = 0;
  g_mpi.num_cpus = 1;
  g_mpi.firstatom = 0;
  g_mpi.firstconf = 0;
  g_mpi.myatoms = 0;
  g_mpi.myconf = 0;
#if defined(MPI)
  g_mpi.atom_dist = NULL;
  g_mpi.atom_len = NULL;
  g_mpi.conf_dist = NULL;
  g_mpi.conf_len = NULL;
#endif

  memset(&g_param, 0, sizeof(g_param));

  g_param.global_cell_scale = 1.0;

  g_pot.gradient = NULL;
  g_pot.invar_pot = NULL;
  g_pot.format = -1;
  g_pot.have_invar = 0;
  memset(&g_pot.calc_pot, 0, sizeof(g_pot.calc_pot));
  memset(&g_pot.opt_pot, 0, sizeof(g_pot.opt_pot));
#if defined(APOT)
  g_pot.smooth_pot = NULL;
  g_pot.cp_start = 0;
  g_pot.global_idx = 0;
  g_pot.global_pot = 0;
  g_pot.have_globals = 0;
  g_pot.calc_list = NULL;
  g_pot.compnodelist = NULL;
  memset(&g_pot.apot_table, 0, sizeof(g_pot.apot_table));
#endif  // APOT

  g_memory.pointer_names = NULL;
  g_memory.num_pointers = 0;
  g_memory.pointers = NULL;
  g_memory.u_address = NULL;

#if defined(PAIR)
  init_interaction_name("PAIR");
#elif defined(EAM) && !defined(COULOMB)
#if !defined(TBEAM)
  init_interaction_name("EAM");
#else
  init_interaction_name("TBEAM");
#endif /* TBEAM */
#elif defined(ADP)
  init_interaction_name("ADP");
#elif defined(COULOMB) && !defined(EAM)
  init_interaction_name("ELSTAT");
#elif defined(COULOMB) && defined(EAM)
  init_interaction_name("EAM_ELSTAT");
#elif defined(MEAM)
  init_interaction_name("MEAM");
#elif defined(STIWEB)
  init_interaction_name("STIWEB");
#elif defined(TERSOFF)
#if defined(TERSOFFMOD)
  init_interaction_name("TERSOFFMOD");
#else
  init_interaction_name("TERSOFF");
#endif /* TERSOFFMOD */
#endif /* interaction type */
}

/****************************************************************
 *
 *  start_mpi_worker
 *
 ****************************************************************/

void start_mpi_worker(double* force)
{
  /* Select correct spline interpolation and other functions */
  /* Root process has done this earlier */

  init_forces(1);

#if defined(APOT)
  g_calc_forces(g_pot.opt_pot.table, force, 0);
#else
  g_calc_forces(g_pot.calc_pot.table, force, 0);
#endif  // APOT
}

/****************************************************************
 *
 *  free_global_variables -- de-allocate memory of global variables
 *
 ****************************************************************/

void free_global_variables()
{
  if (g_files.config != NULL) free(g_files.config);
  if (g_files.distfile != NULL) free(g_files.distfile);
  if (g_files.endpot != NULL) free(g_files.endpot);
  if (g_files.flagfile != NULL) free(g_files.flagfile);
  if (g_files.imdpot != NULL) free(g_files.imdpot);
  if (g_files.maxchfile != NULL) free(g_files.maxchfile);
  if (g_files.output_prefix != NULL) free(g_files.output_prefix);
  if (g_files.output_lammps != NULL) free(g_files.output_lammps);
  if (g_files.plotfile != NULL) free(g_files.plotfile);
  if (g_files.plotpointfile != NULL) free(g_files.plotpointfile);
  if (g_files.startpot != NULL) free(g_files.startpot);
  if (g_files.tempfile != NULL) free(g_files.tempfile);

  free(g_todo.interaction_name);
}

/****************************************************************
 *
 *  init_interaction_name
 *
 ****************************************************************/

void init_interaction_name (const char *name)
{
  int len = strlen(name);
  g_todo.interaction_name = malloc((len + 1) * sizeof(char));
  strncpy(g_todo.interaction_name, name, len);
  g_todo.interaction_name[len] = '\0';
}

/****************************************************************
 *
 *  error -- complain and abort
 *
 ****************************************************************/

void error(int done, const char* msg, ...)
{
  va_list ap;

  fflush(stderr);
  fprintf(stderr, "[ERROR] ");

  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);

  fflush(stderr);

  if (done == 1) {
#if defined(MPI)
    /* go wake up other threads */
    g_calc_forces(NULL, NULL, 1);
    fprintf(stderr, "\n");
    shutdown_mpi();
#endif /* MPI */
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
  }
}

/****************************************************************
 *
 *  warning -- just complain, don't abort
 *
 ****************************************************************/

void warning(const char* msg, ...)
{
  va_list ap;

  fflush(stdout);
  fprintf(stderr, "[WARNING] ");

  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);

  fflush(stderr);
}
