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

#ifndef LMP_SPH_KERNEL_LAGUERREWENDLAND4_EPS_H
#define LMP_SPH_KERNEL_LAGUERREWENDLAND4_EPS_H

#include "math.h"
#include "sph_kernel.h"

namespace LAMMPS_NS {
  class SPHKernelLaguerreWendland4Eps : public SPHKernel {
  public:
    virtual double w3d (double r);
    virtual double w2d (double r);
    virtual double dw3d (double r);
    virtual double dw2d (double r);
static const double eps = 2.821103955647963;
      // 2D: sqrt(2.0)*sqrt(180.0-sqrt(2370.0))/sqrt(33.0);
// 3D:  sqrt(2)*sqrt(170-sqrt(3910))/sqrt(35);
  };
}
#endif
