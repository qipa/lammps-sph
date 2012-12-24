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
#include "assert.h"
#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "fix_phase_change.h"
#include "sph_kernel_quintic.h"
#include "sph_energy_equation.h"
#include "neighbor.h"
#include "neigh_list.h"
#include "neigh_request.h"
#include "pair.h"
#include "random_park.h"
#include "atom.h"
#include "atom_vec.h"
#include "force.h"
#include "update.h"
#include "modify.h"
#include "fix.h"
#include "comm.h"
#include "domain.h"
#include "lattice.h"
#include "region.h"
#include "random_park.h"
#include "memory.h"
#include "error.h"

using namespace LAMMPS_NS;
using namespace FixConst;

#define CG_SMALL 1.0e-20

/* ---------------------------------------------------------------------- */

FixPhaseChange::FixPhaseChange(LAMMPS *lmp, int narg, char **arg) :
  Fix(lmp, narg, arg)
{
  // communicate energy change due to phase change
  comm_reverse = 1;
  int nnarg = 14;
  if (narg < nnarg) error->all(FLERR,"Illegal fix phase_change command");

  restart_global = 1;
  time_depend = 1;

  // required args
  int m = 3;
  Tc = atof(arg[m++]);
  Tt = atof(arg[m++]);
  Hwv = atof(arg[m++]);
  dr = atof(arg[m++]);
  to_mass = atof(arg[m++]);
  cutoff = atof(arg[m++]);
  from_type = atoi(arg[m++]);
  to_type = atoi(arg[m++]);
  nfreq = atoi(arg[m++]);
  seed = atoi(arg[m++]);
  if (seed <= 0) error->all(FLERR,"Illegal value for seed");
  change_chance = atof(arg[m++]);
  if (change_chance < 0) error->all(FLERR,"Illegal value for change_chance");
  assert(m==nnarg);

  iregion = -1;
  idregion = NULL;
  maxattempt = 10;
  scaleflag = 1;

  // read options from end of input line

  options(narg-nnarg,&arg[nnarg]);

  // error checks on region and its extent being inside simulation box

  if (iregion == -1) error->all(FLERR,"Must specify a region in fix phase_change");
  if (domain->regions[iregion]->bboxflag == 0)
    error->all(FLERR,"Fix phase_change region does not support a bounding box");
  if (domain->regions[iregion]->dynamic_check())
    error->all(FLERR,"Fix phase_change region cannot be dynamic");

  xlo = domain->regions[iregion]->extent_xlo;
  xhi = domain->regions[iregion]->extent_xhi;
  ylo = domain->regions[iregion]->extent_ylo;
  yhi = domain->regions[iregion]->extent_yhi;
  zlo = domain->regions[iregion]->extent_zlo;
  zhi = domain->regions[iregion]->extent_zhi;

  if (domain->triclinic == 0) {
    if (xlo < domain->boxlo[0] || xhi > domain->boxhi[0] ||
        ylo < domain->boxlo[1] || yhi > domain->boxhi[1] ||
        zlo < domain->boxlo[2] || zhi > domain->boxhi[2])
      error->all(FLERR,"Phase change region extends outside simulation box");
  } else {
    if (xlo < domain->boxlo_bound[0] || xhi > domain->boxhi_bound[0] ||
        ylo < domain->boxlo_bound[1] || yhi > domain->boxhi_bound[1] ||
        zlo < domain->boxlo_bound[2] || zhi > domain->boxhi_bound[2])
      error->all(FLERR,"Phase change region extends outside simulation box");
  }

  // setup scaling

  if (scaleflag && domain->lattice == NULL)
    error->all(FLERR,"Use of fix phase_change with undefined lattice");

  double xscale,yscale,zscale;
  if (scaleflag) {
    xscale = domain->lattice->xlattice;
    yscale = domain->lattice->ylattice;
    zscale = domain->lattice->zlattice;
  }
  else xscale = yscale = zscale = 1.0;

  random = new RanPark(lmp,seed);
  // set up reneighboring

  force_reneighbor = 1;
  next_reneighbor = update->ntimestep + 1;
  nfirst = next_reneighbor;
}

/* ---------------------------------------------------------------------- */

FixPhaseChange::~FixPhaseChange()
{
  delete random;
  delete [] idregion;
}

/* ---------------------------------------------------------------------- */

int FixPhaseChange::setmask()
{
  int mask = 0;
  mask |= PRE_EXCHANGE;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixPhaseChange::init()
{
  // set index and check validity of region

  iregion = domain->find_region(idregion);
  if (iregion == -1)
    error->all(FLERR,"Region ID for fix phase_change does not exist");

  // need a full neighbor list, built whenever re-neighboring occurs

  int irequest = neighbor->request((void *) this);
  neighbor->requests[irequest]->pair = 0;
  neighbor->requests[irequest]->fix = 1;
  neighbor->requests[irequest]->half = 0;
  neighbor->requests[irequest]->full = 1;
}

void FixPhaseChange::init_list(int, NeighList *ptr)
{
  list = ptr;
}

/* ----------------------------------------------------------------------
   perform phase change
------------------------------------------------------------------------- */

void FixPhaseChange::pre_exchange()
{
  // just return if should not be called on this timestep

  if (next_reneighbor != update->ntimestep) return;

  double *sublo,*subhi;
  if (domain->triclinic == 0) {
    sublo = domain->sublo;
    subhi = domain->subhi;
  } else {
    sublo = domain->sublo_lamda;
    subhi = domain->subhi_lamda;
  }

  // attempt an insertion until successful


    // choose random position for new atom within region
  int nins = 0;
  int nlocal = atom->nlocal;
  int* numneigh = list->numneigh;
  double **x = atom->x;
  double **v = atom->v;
  double **vest = atom->vest;
  double **cg = atom->colorgradient;
  double *rmass = atom->rmass;
  double *rho = atom->rho;
  double *cv = atom->cv;
  double *e   = atom->e;
  double *de = atom->de;
  delocal   = atom->de;
  int *type = atom->type;
  
  int nall;
  if (force->newton) nall = atom->nlocal + atom->nghost;
  else nall = atom->nlocal;
  for (int i = 0; i < nall; i++) {
    delocal[i] = 0.0;
  }

  /// TODO: how to distribute to ghosts?
  for (int i = 0; i < nlocal; i++) {
    double abscgi = sqrt(cg[i][0]*cg[i][0] +
			 cg[i][1]*cg[i][1] +
			 cg[i][2]*cg[i][2]);
    double Ti = sph_energy2t(e[i], cv[i]);
    if  ( (random->uniform()<change_chance) && (Ti>Tt) && (type[i] == to_type) && isfromphasearound(i) )  {
      double coord[3];
      bool ok;
      double delta = dr;
      int natempt = 0;
      do { 
	create_newpos(x[i], cg[i], delta, coord);
	ok = insert_one_atom(coord, sublo, subhi);
	// reduce dr
	delta = 0.75*delta;
	natempt++;
      } while (!ok && natempt<10);
      if (ok) {
	/// create a new particle and cool down a particle i
	/// we have some energy to distribute:
	/// latent heat + change of energy of particle i
	/// NOTE: this energy is in J and not in J/kg
	//double energy_to_dist = Hwv*to_mass  + rmass[i]*(sph_t2energy(Tc,cv[i]) - e[i]);
	double energy_to_dist = 0.0;
	nins++;
	// look for the neighbors of the type from_type
	// and subtract energy from all of them
	double xtmp = x[i][0];
	double ytmp = x[i][1];
	double ztmp = x[i][2];
	int** firstneigh = list->firstneigh;
	int jnum = numneigh[i];
	int* jlist = firstneigh[i];
	// collect
	double wtotal = 0.0;
	  /// TODO: make it run in parallel
	for (int jj = 0; jj < jnum; jj++) {
	  int j = jlist[jj];
	  j &= NEIGHMASK;
	  double Tj = sph_energy2t(e[j],cv[j]);
	  if ( (type[j]==from_type) && (Tj>Ti) ) {
	    double delx = xtmp - x[j][0];
	    double dely = ytmp - x[j][1];
	    double delz = ztmp - x[j][2];
	    double rsq = delx * delx + dely * dely + delz * delz;
	    double wfd;
	    if (domain->dimension == 3) {
	      wfd = sph_kernel_quintic3d(sqrt(rsq)*cutoff);
	    } else {
	      wfd = sph_kernel_quintic2d(sqrt(rsq)*cutoff);
	    }
	    wtotal+=wfd*(Tj-Ti);
	    assert(wfd*(Tj-Ti)>=0);
	  }
	}

	// distribute
	for (int jj = 0; jj < jnum; jj++) {
	  int j = jlist[jj];
	  j &= NEIGHMASK;
	  double Tj = sph_energy2t(e[j],cv[j]);
	  if ( (type[j]==from_type) && (Tj>Ti) ) {
	    double delx = xtmp - x[j][0];
	    double dely = ytmp - x[j][1];
	    double delz = ztmp - x[j][2];
	    double rsq = delx * delx + dely * dely + delz * delz;
	    double wfd;
	    if (domain->dimension == 3) {
	      wfd = sph_kernel_quintic3d(sqrt(rsq)*cutoff);
	    } else {
	      wfd = sph_kernel_quintic2d(sqrt(rsq)*cutoff);
	    }
 	    delocal[j] -= (energy_to_dist/rmass[j]) * wfd*(Tj-Ti)/wtotal;
	    assert(wfd*(Tj-Ti)>=0);
	    assert(wtotal>=0);
	  }
	}
	
 	// for a new atom
	rmass[atom->nlocal-1] = to_mass;
	rho[atom->nlocal-1] = rho[i];
	e[atom->nlocal-1] = sph_t2energy(Tc,cv[i]);
	cv[atom->nlocal-1] = cv[i];
	de[atom->nlocal-1] = 0.0;
	// conserve momentum total momentum
	double km = to_mass/(to_mass + rmass[i]);
	v[atom->nlocal-1][0] = vest[atom->nlocal-1][0] = v[i][0]*km;
	v[atom->nlocal-1][1] = vest[atom->nlocal-1][1] = v[i][1]*km;
	v[atom->nlocal-1][2] = vest[atom->nlocal-1][2] = v[i][2]*km;

	double ki = rmass[i]/(to_mass + rmass[i]);
	v[i][0] = vest[i][0] = v[i][0]*ki;
	v[i][1] = vest[i][1] = v[i][1]*ki;
	v[i][2] = vest[i][2] = v[i][2]*ki;

	//e[i] = sph_t2energy(Tc,cv[i]);
	e[i] -= Hwv;
      }
    }
  }

  /// find a total number of inserted atoms
  int ninsall;
  next_reneighbor += nfreq;
  comm->reverse_comm_fix(this);
  for (int i = 0; i < nlocal; i++) {
    e[i] += delocal[i];
  }
  
  // reset global natoms
  // set tag # of new particle beyond all previous atoms
  // if global map exists, reset it now instead of waiting for comm
  // since deleting atoms messes up ghosts
  MPI_Allreduce(&nins,&ninsall,1,MPI_INT,MPI_SUM,world);
  if (ninsall>0) {
    atom->natoms += ninsall;
    if (atom->tag_enable) {
      atom->tag_extend();
    }
    atom->nghost = 0;
    if (atom->map_style) {
      atom->map_init();
      atom->map_set();
    }
  }
}

/* ----------------------------------------------------------------------
   parse optional parameters at end of input line
------------------------------------------------------------------------- */

void FixPhaseChange::options(int narg, char **arg)
{
  if (narg < 0) error->all(FLERR,"Illegal fix indent command");

  int iarg = 0;
  while (iarg < narg) {
    if (strcmp(arg[iarg],"region") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix phase_change command");
      iregion = domain->find_region(arg[iarg+1]);
      if (iregion == -1)
        error->all(FLERR,"Region ID for fix phase_change does not exist");
      int n = strlen(arg[iarg+1]) + 1;
      idregion = new char[n];
      strcpy(idregion,arg[iarg+1]);
      iarg += 2;
    } else if (strcmp(arg[iarg],"attempt") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix phase_change command");
      maxattempt = atoi(arg[iarg+1]);
      iarg += 2;
    } else if (strcmp(arg[iarg],"units") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix phase_change command");
      if (strcmp(arg[iarg+1],"box") == 0) scaleflag = 0;
      else if (strcmp(arg[iarg+1],"lattice") == 0) scaleflag = 1;
      else error->all(FLERR,"Illegal fix phase_change command");
      iarg += 2;
    } else error->all(FLERR,"Illegal fix phase_change command");
  }
}

/* ----------------------------------------------------------------------
   pack entire state of Fix into one write
------------------------------------------------------------------------- */

void FixPhaseChange::write_restart(FILE *fp)
{
  int n = 0;
  double list[4];
  list[n++] = next_reneighbor;

  if (comm->me == 0) {
    int size = n * sizeof(double);
    fwrite(&size,sizeof(int),1,fp);
    fwrite(list,sizeof(double),n,fp);
  }
}

/* ----------------------------------------------------------------------
   use state info from restart file to restart the Fix
------------------------------------------------------------------------- */

void FixPhaseChange::restart(char *buf)
{
  int n = 0;
  double *list = (double *) buf;

  seed = static_cast<int> (list[n++]);
  nfirst = static_cast<int> (list[n++]);
  next_reneighbor = static_cast<int> (list[n++]);

  random->reset(seed);
}

bool FixPhaseChange::insert_one_atom(double* coord, double* sublo, double* subhi)
{
  int flag;
  double lamda[3];
  double *newcoord;

  int nfix = modify->nfix;
  Fix **fix = modify->fix;

  // check if new atom is in my sub-box or above it if I'm highest proc
  // if so, add to my list via create_atom()
  // initialize info about the atoms
  // set group mask to "all" plus fix group
  if (domain->triclinic) {
    domain->x2lamda(coord,lamda);
    newcoord = lamda;
  } else newcoord = coord;

  flag = 0;
  if (newcoord[0] >= sublo[0] && newcoord[0] < subhi[0] &&
      newcoord[1] >= sublo[1] && newcoord[1] < subhi[1] &&
      newcoord[2] >= sublo[2] && newcoord[2] < subhi[2]) flag = 1;
  else if (domain->dimension == 3 && newcoord[2] >= domain->boxhi[2] &&
	   comm->myloc[2] == comm->procgrid[2]-1 &&
	   newcoord[0] >= sublo[0] && newcoord[0] < subhi[0] &&
	   newcoord[1] >= sublo[1] && newcoord[1] < subhi[1]) flag = 1;
  else if (domain->dimension == 2 && newcoord[1] >= domain->boxhi[1] &&
	   comm->myloc[1] == comm->procgrid[1]-1 &&
	   newcoord[0] >= sublo[0] && newcoord[0] < subhi[0]) flag = 1;

  if (flag) {
    atom->avec->create_atom(to_type,coord);
    int m = atom->nlocal - 1;
    atom->type[m] = to_type;
    atom->mask[m] = 1 | groupbit;
    for (int j = 0; j < nfix; j++)
      if (fix[j]->create_attribute) fix[j]->set_arrays(m);
  }
  if (flag) {
    return true;
  } else {
    return false;
  }
}

void FixPhaseChange::create_newpos(double* xone, double* cgone, double delta, double* coord) {
  double eij[3];
  if (domain->dimension==3) {
    double b1[3];
    b1[0] =  -cgone[1] ;
    b1[1] =  cgone[0] ;
    b1[2] =  0 ;
    double b1abs = sqrt(b1[0]*b1[0] + b1[1]*b1[1] + b1[2]*b1[2]);
    b1[0] = b1[0]/b1abs;     b1[1] = b1[1]/b1abs;     b1[2] = b1[2]/b1abs;
    assert( b1[0]*cgone[0] + b1[1]*cgone[1] + b1[2]*cgone[2] < 1e-8);

    double b2[3];
    b2[0] =  -cgone[0]*cgone[1]*cgone[2]/(pow(cgone[1],2)+pow(cgone[0],2)) ;
    b2[1] =  -cgone[2]*pow(cgone[1],2)/(pow(cgone[1],2)+pow(cgone[0],2)) ;
    b2[2] =  cgone[1];
    double b2abs = sqrt(b2[0]*b2[0] + b2[1]*b2[1] + b2[2]*b2[2]);
    b2[0] = b2[0]/b2abs;     b2[1] = b2[1]/b2abs;     b2[2] = b2[2]/b2abs;
    assert( b2[0]*cgone[0] + b2[1]*cgone[1] + b2[2]*cgone[2] < 1e-8);

    double atmp = random->uniform() - 0.5;
    double btmp = random->uniform() - 0.5;
    eij[0] = atmp*b1[0] + btmp*b2[0];
    eij[1] = atmp*b1[1] + btmp*b2[1];
    eij[2] = atmp*b1[2] + btmp*b2[2];
    assert( eij[0]*cgone[0] + eij[1]*cgone[1] + eij[2]*cgone[2] < 1e-8);
  } else {
    // TODO: find direction cheaper
    double atmp = random->uniform();
    assert(atmp<=1.0);
    assert(atmp>=0.0);
    if (atmp>0.5) atmp=1; else atmp=-1;
    eij[0] = -atmp*cgone[1];
    eij[1] = atmp*cgone[0];
    eij[2] = 0.0;
  }
  double eijabs = sqrt(eij[0]*eij[0] + eij[1]*eij[1] + eij[2]*eij[2]);
  /// TODO: add scale
  coord[0] = xone[0] + eij[0]*delta/eijabs;
  coord[1] = xone[1] + eij[1]*delta/eijabs;
  coord[2] = xone[2] + eij[2]*delta/eijabs;
}

int FixPhaseChange::pack_reverse_comm(int n, int first, double *buf)
{
  int i,m,last;

  m = 0;
  last = first + n;

    for (i = first; i < last; i++) {
      buf[m++] = delocal[i];
    }
  return comm_reverse;
}

/* ---------------------------------------------------------------------- */

void FixPhaseChange::unpack_reverse_comm(int n, int *list, double *buf)
{
  int i,j,m;

  m = 0;
  for (i = 0; i < n; i++) {
    j = list[i];
    delocal[j] += buf[m++];
  }
}

bool FixPhaseChange::isfromphasearound(int i) {
  int* numneigh = list->numneigh;
  double **x = atom->x;
  int *type = atom->type;
  double cutoff2 = cutoff*cutoff;
  double xtmp = x[i][0];
  double ytmp = x[i][1];
  double ztmp = x[i][2];
  int** firstneigh = list->firstneigh;
  int jnum = numneigh[i];
  int* jlist = firstneigh[i];
  for (int jj = 0; jj < jnum; jj++) {
    int j = jlist[jj];
    j &= NEIGHMASK;
    if (type[j]==from_type) {
	double delx = xtmp - x[j][0];
	double dely = ytmp - x[j][1];
	double delz = ztmp - x[j][2];
	double rsq = delx * delx + dely * dely + delz * delz;
	if (rsq<=cutoff2) return true;
    }
  }
  return  false;
}