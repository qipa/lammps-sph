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

#ifndef PAIR_LJ_CLASS2_COUL_CUT_H
#define PAIR_LJ_CLASS2_COUL_CUT_H

#include "pair.h"

namespace LAMMPS_NS {

class PairLJClass2CoulCut : public Pair {
 public:
  PairLJClass2CoulCut(class LAMMPS *);
  ~PairLJClass2CoulCut();
  void compute(int, int);
  void settings(int, char **);
  void coeff(int, char **);
  double init_one(int, int);
  void init_style();
  void write_restart(FILE *);
  void read_restart(FILE *);
  void write_restart_settings(FILE *);
  void read_restart_settings(FILE *);
  void single(int, int, int, int, double, double, double, int, One &);

 private:
  double cut_lj_global,cut_coul_global;
  double **cut_lj,**cut_ljsq;
  double **cut_coul,**cut_coulsq;
  double **epsilon,**sigma;
  double **lj1,**lj2,**lj3,**lj4,**offset;

  void allocate();
};

}

#endif
