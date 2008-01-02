/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "stdlib.h"
#include "string.h"
#include "compute_stress_atom.h"
#include "atom.h"
#include "comm.h"
#include "force.h"
#include "pair.h"
#include "bond.h"
#include "angle.h"
#include "dihedral.h"
#include "improper.h"
#include "memory.h"
#include "error.h"

using namespace LAMMPS_NS;

#define INVOKED_PERATOM 4

/* ---------------------------------------------------------------------- */

ComputeStressAtom::ComputeStressAtom(LAMMPS *lmp, int narg, char **arg) : 
  Compute(lmp, narg, arg)
{
  if (narg < 3) error->all("Illegal compute stress/atom command");

  peratom_flag = 1;
  size_peratom = 6;
  pressatomflag = 1;
  timeflag = 1;
  comm_reverse = 6;

  if (narg == 3) {
    keflag = 1;
    pairflag = 1;
    bondflag = angleflag = dihedralflag = improperflag = 1;
  } else {
    keflag = 0;
    pairflag = 0;
    bondflag = angleflag = dihedralflag = improperflag = 0;
    int iarg = 3;
    while (iarg < narg) {
      if (strcmp(arg[iarg],"ke") == 0) keflag = 1;
      else if (strcmp(arg[iarg],"pair") == 0) pairflag = 1;
      else if (strcmp(arg[iarg],"bond") == 0) bondflag = 1;
      else if (strcmp(arg[iarg],"angle") == 0) angleflag = 1;
      else if (strcmp(arg[iarg],"dihedral") == 0) dihedralflag = 1;
      else if (strcmp(arg[iarg],"improper") == 0) improperflag = 1;
      else error->all("Illegal compute stress/atom command");
      iarg++;
    }
  }

  nmax = 0;
  stress = NULL;
}

/* ---------------------------------------------------------------------- */

ComputeStressAtom::~ComputeStressAtom()
{
  memory->destroy_2d_double_array(stress);
}

/* ---------------------------------------------------------------------- */

void ComputeStressAtom::compute_peratom()
{
  int i,j;

  invoked |= INVOKED_PERATOM;

  // grow local stress array if necessary

  if (atom->nmax > nmax) {
    memory->destroy_2d_double_array(stress);
    nmax = atom->nmax;
    stress =
      memory->create_2d_double_array(nmax,6,"compute/stress/atom:stress");
    vector_atom = stress;
  }

  // npair includes ghosts if either newton flag is set
  //   b/c some bonds/dihedrals call pair::ev_tally with pairwise info
  // nbond includes ghosts if newton_bond is set
  // ntotal includes ghosts if either newton flag is set

  int nlocal = atom->nlocal;
  int npair = nlocal;
  int nbond = nlocal;
  int ntotal = nlocal;
  if (force->newton) npair += atom->nghost;
  if (force->newton_bond) nbond += atom->nghost;
  if (force->newton) ntotal += atom->nghost;

  // clear local stress array

  for (i = 0; i < ntotal; i++)
    for (j = 0; j < 6; j++)
      stress[i][j] = 0.0;

  // add in per-atom contributions from each force
  
  if (pairflag && force->pair) {
    double **vatom = force->pair->vatom;
    for (i = 0; i < npair; i++)
      for (j = 0; j < 6; j++)
	stress[i][j] += vatom[i][j];
  }

  if (bondflag && force->bond) {
    double **vatom = force->bond->vatom;
    for (i = 0; i < nbond; i++)
      for (j = 0; j < 6; j++)
	stress[i][j] += vatom[i][j];
  }

  if (angleflag && force->angle) {
    double **vatom = force->angle->vatom;
    for (i = 0; i < nbond; i++)
      for (j = 0; j < 6; j++)
	stress[i][j] += vatom[i][j];
  }

  if (dihedralflag && force->dihedral) {
    double **vatom = force->dihedral->vatom;
    for (i = 0; i < nbond; i++)
      for (j = 0; j < 6; j++)
	stress[i][j] += vatom[i][j];
  }

  if (improperflag && force->improper) {
    double **vatom = force->improper->vatom;
    for (i = 0; i < nbond; i++)
      for (j = 0; j < 6; j++)
	stress[i][j] += vatom[i][j];
  }

  // communicate ghost energy between neighbor procs

  if (force->newton) comm->reverse_comm_compute(this);

  // zero virial of atoms not in group
  // only do this after comm since ghost contributions must be included

  int *mask = atom->mask;

  for (i = 0; i < nlocal; i++)
    if (!(mask[i] & groupbit)) {
      stress[i][0] = 0.0;
      stress[i][1] = 0.0;
      stress[i][2] = 0.0;
      stress[i][3] = 0.0;
      stress[i][4] = 0.0;
      stress[i][5] = 0.0;
    }

  // include kinetic energy term for each atom in group
  // mvv2e converts mv^2 to energy

  if (keflag) {
    double **v = atom->v;
    double *mass = atom->mass;
    int *type = atom->type;
    double mvv2e = force->mvv2e;
    double rmass;

    for (i = 0; i < nlocal; i++)
      if (mask[i] & groupbit) {
	rmass = mvv2e * mass[type[i]];
	stress[i][0] += rmass*v[i][0]*v[i][0];
	stress[i][1] += rmass*v[i][1]*v[i][1];
	stress[i][2] += rmass*v[i][2]*v[i][2];
	stress[i][3] += rmass*v[i][0]*v[i][1];
	stress[i][4] += rmass*v[i][0]*v[i][2];
	stress[i][5] += rmass*v[i][1]*v[i][2];
      }
  }

  // convert to pressure units (actually stress/volume = -pressure)

  double nktv2p = -force->nktv2p;
  for (i = 0; i < nlocal; i++)
    if (mask[i] & groupbit) {
      stress[i][0] *= nktv2p;
      stress[i][1] *= nktv2p;
      stress[i][2] *= nktv2p;
      stress[i][3] *= nktv2p;
      stress[i][4] *= nktv2p;
      stress[i][5] *= nktv2p;
    }
}

/* ---------------------------------------------------------------------- */

int ComputeStressAtom::pack_reverse_comm(int n, int first, double *buf)
{
  int i,m,last;

  m = 0;
  last = first + n;
  for (i = first; i < last; i++) {
    buf[m++] = stress[i][0];
    buf[m++] = stress[i][1];
    buf[m++] = stress[i][2];
    buf[m++] = stress[i][3];
    buf[m++] = stress[i][4];
    buf[m++] = stress[i][5];
  }
  return 6;
}

/* ---------------------------------------------------------------------- */

void ComputeStressAtom::unpack_reverse_comm(int n, int *list, double *buf)
{
  int i,j,m;

  m = 0;
  for (i = 0; i < n; i++) {
    j = list[i];
    stress[j][0] += buf[m++];
    stress[j][1] += buf[m++];
    stress[j][2] += buf[m++];
    stress[j][3] += buf[m++];
    stress[j][4] += buf[m++];
    stress[j][5] += buf[m++];
  }
}

/* ----------------------------------------------------------------------
   memory usage of local atom-based array
------------------------------------------------------------------------- */

double ComputeStressAtom::memory_usage()
{
  double bytes = nmax*6 * sizeof(double);
  return bytes;
}
