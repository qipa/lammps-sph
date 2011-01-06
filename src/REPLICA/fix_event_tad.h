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

#ifdef FIX_CLASS

FixStyle(EVENT/TAD,FixEventTAD)

#else

#ifndef LMP_FIX_EVENT_TAD_H
#define LMP_FIX_EVENT_TAD_H

#include "fix_event.h"

namespace LAMMPS_NS {

class FixEventTAD : public FixEvent {
 public:
  int event_number;      // event counter
  int event_timestep;    // timestep of last event
  double tlo;            // event time at low temperature 
  double ebarrier;       // energy barrier for this event

  FixEventTAD(class LAMMPS *, int, char **);
  ~FixEventTAD() {}

  void write_restart(FILE *);
  void restart(char *);

  // methods specific to FixEventTAD, invoked by TAD

  void store_event_tad(int);

 private:

};

}

#endif
#endif
