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

#ifdef PAIR_CLASS

PairStyle(brownian,PairBrownian)

#else

#ifndef LMP_PAIR_BROWNIAN_H
#define LMP_PAIR_BROWNIAN_H

#include "pair.h"

namespace LAMMPS_NS {

class PairBrownian : public Pair {
 public:
  PairBrownian(class LAMMPS *);
  virtual ~PairBrownian();
  virtual void compute(int, int); 
  void settings(int, char **);
  void coeff(int, char **);
  virtual double init_one(int, int);
  virtual void init_style();
  void write_restart(FILE *);
  void read_restart(FILE *);
  void write_restart_settings(FILE *);
  void read_restart_settings(FILE *); 

 protected:
  double cut_inner_global,cut_global;
  double t_target,mu;
  int flaglog,flagfld;
  int seed;
  double **cut_inner,**cut;
  double R0,RT0;

  class RanMars *random;

  void set_3_orthogonal_vectors(double*,double*,double*);
  void allocate();
};

}

#endif
#endif
