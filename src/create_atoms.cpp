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

#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "create_atoms.h"
#include "atom.h"
#include "atom_vec.h"
#include "atom_vec_hybrid.h"
#include "comm.h"
#include "domain.h"
#include "lattice.h"
#include "region.h"
#include "error.h"

using namespace LAMMPS_NS;

#define MAXATOMS 0x7FFFFFFF
#define BIG      1.0e30
#define EPSILON  1.0e-6

/* ---------------------------------------------------------------------- */

CreateAtoms::CreateAtoms(LAMMPS *lmp) : Pointers(lmp) {}

/* ---------------------------------------------------------------------- */

void CreateAtoms::command(int narg, char **arg)
{
  if (domain->box_exist == 0) 
    error->all("Create_atoms command before simulation box is defined");
  if (domain->lattice == NULL)
    error->all("Cannot create atoms with undefined lattice");

  // parse arguments

  int nbasis = domain->lattice->nbasis;
  int *basistype = new int[nbasis];

  if (narg < 1) error->all("Illegal create_atoms command");
  int itype = atoi(arg[0]);
  if (itype <= 0 || itype > atom->ntypes) 
    error->all("Invalid atom type in create_atoms command");
  for (int i = 0; i < nbasis; i++) basistype[i] = itype;

  int regionflag = -1;
  int nhybrid = 0;

  int iarg = 1;
  while (iarg < narg) {
    if (strcmp(arg[iarg],"region") == 0) {
      if (iarg+2 > narg) error->all("Illegal create_atoms command");
      int iregion;
      for (iregion = 0; iregion < domain->nregion; iregion++)
	if (strcmp(arg[iarg+1],domain->regions[iregion]->id) == 0) break;
      if (iregion == domain->nregion)
	error->all("Create_atoms region ID does not exist");
      regionflag = iregion;
      iarg += 2;
    } else if (strcmp(arg[iarg],"basis") == 0) {
      if (iarg+3 > narg) error->all("Illegal create_atoms command");
      int ibasis = atoi(arg[iarg+1]);
      itype = atoi(arg[iarg+2]);
      if (ibasis <= 0 || ibasis > nbasis || 
	  itype <= 0 || itype > atom->ntypes) 
	error->all("Illegal create_atoms command");
      basistype[ibasis-1] = itype;
      iarg += 3;
    } else if (strcmp(arg[iarg],"hybrid") == 0) {
      if (iarg+3 > narg) error->all("Illegal create_atoms command");
      AtomVecHybrid *avec_hybrid = (AtomVecHybrid *) atom->avec;
      int ihybrid;
      for (ihybrid = 0; ihybrid < avec_hybrid->nstyles; ihybrid++)
	if (strcmp(avec_hybrid->keywords[ihybrid],arg[iarg+1]) == 0) break;
      if (ihybrid == avec_hybrid->nstyles)
	error->all("Create atoms hybrid sub-style does not exist");
      nhybrid = ihybrid;
      iarg += 3;
    } else error->all("Illegal create_atoms command");
  }

  // convert 8 corners of my sub-box from box coords to lattice coords
  // min to max = bounding box around the pts in lattice space

  int triclinic = domain->triclinic;
  double *bboxlo,*bboxhi;

  if (triclinic == 0) {
    bboxlo = domain->sublo;
    bboxhi = domain->subhi;
  } else {
    bboxlo = domain->sublo_bound;
    bboxhi = domain->subhi_bound;
  }

  double xmin,ymin,zmin,xmax,ymax,zmax;
  xmin = ymin = zmin = BIG;
  xmax = ymax = zmax = -BIG;

  domain->lattice->bbox(1,bboxlo[0],bboxlo[1],bboxlo[2],
			xmin,ymin,zmin,xmax,ymax,zmax);
  domain->lattice->bbox(1,bboxhi[0],bboxlo[1],bboxlo[2],
			xmin,ymin,zmin,xmax,ymax,zmax);
  domain->lattice->bbox(1,bboxlo[0],bboxhi[1],bboxlo[2],
			xmin,ymin,zmin,xmax,ymax,zmax);
  domain->lattice->bbox(1,bboxhi[0],bboxhi[1],bboxlo[2],
			xmin,ymin,zmin,xmax,ymax,zmax);
  domain->lattice->bbox(1,bboxlo[0],bboxlo[1],bboxhi[2],
			xmin,ymin,zmin,xmax,ymax,zmax);
  domain->lattice->bbox(1,bboxhi[0],bboxlo[1],bboxhi[2],
			xmin,ymin,zmin,xmax,ymax,zmax);
  domain->lattice->bbox(1,bboxlo[0],bboxhi[1],bboxhi[2],
			xmin,ymin,zmin,xmax,ymax,zmax);
  domain->lattice->bbox(1,bboxhi[0],bboxhi[1],bboxhi[2],
			xmin,ymin,zmin,xmax,ymax,zmax);

  // ilo:ihi,jlo:jhi,klo:khi = loop bounds for lattice overlap of my subbox
  // overlap = any part of a unit cell (face,edge,pt) in common with my subbox
  // in lattice space, subbox is a tilted box
  // but bbox of subbox is aligned with lattice axes
  // so ilo:khi unit cells should completely tile bounding box
  // decrement lo values if min < 0, since static_cast(-1.5) = -1

  int ilo,ihi,jlo,jhi,klo,khi;
  ilo = static_cast<int> (xmin);
  jlo = static_cast<int> (ymin);
  klo = static_cast<int> (zmin);
  ihi = static_cast<int> (xmax);
  jhi = static_cast<int> (ymax);
  khi = static_cast<int> (zmax);

  if (xmin < 0.0) ilo--;
  if (ymin < 0.0) jlo--;
  if (zmin < 0.0) klo--;

  // set bounds for my proc
  // if periodic and I am lo/hi proc, adjust bounds by EPSILON
  // on lower boundary, allows triclinic atoms just outside box to be added
  // on upper boundary, prevents atoms with lower images from being added

  double sublo[3],subhi[3];

  if (triclinic == 0) {
    sublo[0] = domain->sublo[0]; subhi[0] = domain->subhi[0];
    sublo[1] = domain->sublo[1]; subhi[1] = domain->subhi[1];
    sublo[2] = domain->sublo[2]; subhi[2] = domain->subhi[2];
  } else {
    sublo[0] = domain->sublo_lamda[0]; subhi[0] = domain->subhi_lamda[0];
    sublo[1] = domain->sublo_lamda[1]; subhi[1] = domain->subhi_lamda[1];
    sublo[2] = domain->sublo_lamda[2]; subhi[2] = domain->subhi_lamda[2];
  }

  if (domain->xperiodic) {
    if (triclinic && comm->myloc[0] == 0) sublo[0] -= EPSILON;
    if (comm->myloc[0] == comm->procgrid[0]-1) subhi[0] -= EPSILON;
  }
  if (domain->yperiodic) {
    if (triclinic && comm->myloc[1] == 0) sublo[1] -= EPSILON;
    if (comm->myloc[1] == comm->procgrid[1]-1) subhi[1] -= EPSILON;
  }
  if (domain->zperiodic) {
    if (triclinic && comm->myloc[2] == 0) sublo[2] -= EPSILON;
    if (comm->myloc[2] == comm->procgrid[2]-1) subhi[2] -= EPSILON;
  }

  // iterate on 3d periodic lattice using loop bounds
  // invoke add_atom for nbasis atoms in each unit cell
  // converts lattice coords to box coords
  // add atom if it meets all criteria 

  double natoms_previous = atom->natoms;
  int nlocal_previous = atom->nlocal;

  double **basis = domain->lattice->basis;
  double x[3],lamda[3];
  double *coord;

  int i,j,k,m;
  for (k = klo; k <= khi; k++)
    for (j = jlo; j <= jhi; j++)
      for (i = ilo; i <= ihi; i++)
	for (m = 0; m < nbasis; m++) {

	  x[0] = i + basis[m][0];
	  x[1] = j + basis[m][1];
	  x[2] = k + basis[m][2];

	  // convert from lattice coords to box coords

	  domain->lattice->lattice2box(x[0],x[1],x[2]);

	  // if a region was specified, test if atom is in it

	  if (regionflag >= 0)
	    if (!domain->regions[regionflag]->match(x[0],x[1],x[2])) continue;

	  // test if atom is in my subbox
	  
	  if (triclinic) {
	    domain->x2lamda(x,lamda);
	    coord = lamda;
	  } else coord = x;

	  if (coord[0] < sublo[0] || coord[0] >= subhi[0] || 
	      coord[1] < sublo[1] || coord[1] >= subhi[1] || 
	      coord[2] < sublo[2] || coord[2] >= subhi[2]) continue;

	  // add the atom to my list of atoms

	  atom->avec->create_atom(basistype[m],x,nhybrid);
	}

  // clean up

  delete [] basistype;

  // new total # of atoms

  double rlocal = atom->nlocal;
  MPI_Allreduce(&rlocal,&atom->natoms,1,MPI_DOUBLE,MPI_SUM,world);

  // print status

  if (comm->me == 0) {
    if (screen)
      fprintf(screen,"Created %.15g atoms\n",atom->natoms-natoms_previous);
    if (logfile)
      fprintf(logfile,"Created %.15g atoms\n",atom->natoms-natoms_previous);
  }

  // reset simulation now that more atoms are defined
  // add tags for newly created atoms if possible
  // if global map exists, reset it
  // if a molecular system, set nspecial to 0 for new atoms

  if (atom->natoms > MAXATOMS) atom->tag_enable = 0;
  if (atom->natoms <= MAXATOMS) atom->tag_extend();

  if (atom->map_style) {
    atom->map_init();
    atom->map_set();
  }
  if (atom->molecular) {
    int **nspecial = atom->nspecial;
    for (i = nlocal_previous; i < atom->nlocal; i++) {
      nspecial[i][0] = 0;
      nspecial[i][1] = 0;
      nspecial[i][2] = 0;
    }
  }
}
