#include <cmath>

#include "colvarmodule.h"
#include "colvarvalue.h"
#include "colvarparse.h"
#include "colvar.h"
#include "colvarcomp.h"



/// \file cvc_distance.cpp \brief Collective variables
/// determining various type of distances between two groups

// "twogroup" flag defaults to true; set to false by selfCoordNum
// (only distance-derived component based on only one group)

colvar::distance::distance (std::string const &conf, bool twogroups)
  : cvc (conf)
{
  function_type = "distance";
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  if (get_keyval (conf, "forceNoPBC", b_no_PBC, false)) {
    cvm::log ("Computing distance using absolute positions (not minimal-image)");
  }
  if (twogroups && get_keyval (conf, "oneSiteSystemForce", b_1site_force, false)) {
    cvm::log ("Computing system force on group 1 only");
  }
  parse_group (conf, "group1", group1);
  atom_groups.push_back (&group1);
  if (twogroups) {
    parse_group (conf, "group2", group2);
    atom_groups.push_back (&group2);
  }
  x.type (colvarvalue::type_scalar);
}


colvar::distance::distance()
  : cvc ()
{
  function_type = "distance";
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  b_1site_force = false;
  x.type (colvarvalue::type_scalar);
}

void colvar::distance::calc_value()
{
  group1.reset_atoms_data();
  group2.reset_atoms_data();

  group1.read_positions();
  group2.read_positions();

  if (b_no_PBC) {
    dist_v = group2.center_of_mass() - group1.center_of_mass();
  } else {
    dist_v = cvm::position_distance (group1.center_of_mass(),
                                     group2.center_of_mass());
  }
  x.real_value = dist_v.norm();
}

void colvar::distance::calc_gradients()
{
  cvm::rvector const u = dist_v.unit();
  group1.set_weighted_gradient (-1.0 * u);
  group2.set_weighted_gradient (       u);
}

void colvar::distance::calc_force_invgrads()
{
  group1.read_system_forces();
  if ( b_1site_force ) {
    ft.real_value = -1.0 * (group1.system_force() * dist_v.unit());
  } else {
    group2.read_system_forces();
    ft.real_value = 0.5 * ((group2.system_force() - group1.system_force()) * dist_v.unit());
  }
}

void colvar::distance::calc_Jacobian_derivative()
{
  jd.real_value = x.real_value ? (2.0 / x.real_value) : 0.0;
}

void colvar::distance::apply_force (colvarvalue const &force)
{
  if (!group1.noforce)
    group1.apply_colvar_force (force.real_value);

  if (!group2.noforce)
    group2.apply_colvar_force (force.real_value);
}



colvar::distance_vec::distance_vec (std::string const &conf)
  : distance (conf)
{
  function_type = "distance_vec";
  x.type (colvarvalue::type_vector);
}

colvar::distance_vec::distance_vec()
  : distance()
{
  function_type = "distance_vec";
  x.type (colvarvalue::type_vector);
}

void colvar::distance_vec::calc_value()
{
  group1.reset_atoms_data();
  group2.reset_atoms_data();

  group1.read_positions();
  group2.read_positions();

  if (b_no_PBC) {
    x.rvector_value = group2.center_of_mass() - group1.center_of_mass();
  } else {
    x.rvector_value = cvm::position_distance (group1.center_of_mass(),
                                              group2.center_of_mass());
  }
}

void colvar::distance_vec::calc_gradients()
{ 
  // gradients are not stored: a 3x3 matrix for each atom would be
  // needed to store just the identity matrix
}

void colvar::distance_vec::apply_force (colvarvalue const &force)
{
  if (!group1.noforce)
    group1.apply_force (-1.0 * force.rvector_value);

  if (!group2.noforce)
    group2.apply_force (       force.rvector_value);
}



colvar::distance_z::distance_z (std::string const &conf)
  : cvc (conf)
{
  function_type = "distance_z";
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  x.type (colvarvalue::type_scalar);

  // TODO detect PBC from MD engine (in simple cases)
  // and then update period in real time
  if (period != 0.0)
    b_periodic = true;    

  if ((wrap_center != 0.0) && (period == 0.0)) {
    cvm::fatal_error ("Error: wrapAround was defined in a distanceZ component,"
                      " but its period has not been set.\n");
  }

  parse_group (conf, "main", main);
  parse_group (conf, "ref", ref1);
  atom_groups.push_back (&main);
  atom_groups.push_back (&ref1);
  // this group is optional
  parse_group (conf, "ref2", ref2, true);
 
  if (ref2.size()) {
    atom_groups.push_back (&ref2);
    cvm::log ("Using axis joining the centers of mass of groups \"ref\" and \"ref2\"");
    fixed_axis = false;
    if (key_lookup (conf, "axis"))
      cvm::log ("Warning: explicit axis definition will be ignored!");
  } else {
    if (get_keyval (conf, "axis", axis, cvm::rvector (0.0, 0.0, 1.0))) {
      if (axis.norm2() == 0.0)
        cvm::fatal_error ("Axis vector is zero!");
      if (axis.norm2() != 1.0) {
        axis = axis.unit();
        cvm::log ("The normalized axis is: "+cvm::to_str (axis)+".\n");
      }
    }
    fixed_axis = true;
  }

  if (get_keyval (conf, "forceNoPBC", b_no_PBC, false)) {
    cvm::log ("Computing distance using absolute positions (not minimal-image)");
  }
  if (get_keyval (conf, "oneSiteSystemForce", b_1site_force, false)) {
    cvm::log ("Computing system force on group \"main\" only");
  }
}

colvar::distance_z::distance_z()
{
  function_type = "distance_z";
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  x.type (colvarvalue::type_scalar);
}

void colvar::distance_z::calc_value()
{
  main.reset_atoms_data();
  ref1.reset_atoms_data();

  main.read_positions();
  ref1.read_positions();

  if (fixed_axis) {
    if (b_no_PBC) {
      dist_v = main.center_of_mass() - ref1.center_of_mass();
    } else {
      dist_v = cvm::position_distance (ref1.center_of_mass(),
                                       main.center_of_mass());
    }
  } else {
    ref2.reset_atoms_data();
    ref2.read_positions();

    if (b_no_PBC) {
      dist_v = main.center_of_mass() -
               (0.5 * (ref1.center_of_mass() + ref2.center_of_mass()));
      axis = ref2.center_of_mass() - ref1.center_of_mass();
    } else {
      dist_v = cvm::position_distance (0.5 * (ref1.center_of_mass() +
               ref2.center_of_mass()), main.center_of_mass());
      axis = cvm::position_distance (ref1.center_of_mass(), ref2.center_of_mass());
    }
    axis_norm = axis.norm();
    axis = axis.unit();
  }
  x.real_value = axis * dist_v;
  this->wrap (x);
}

void colvar::distance_z::calc_gradients()
{
  main.set_weighted_gradient ( axis );

  if (fixed_axis) {
    ref1.set_weighted_gradient (-1.0 * axis);
  } else {
    if (b_no_PBC) {
      ref1.set_weighted_gradient ( 1.0 / axis_norm * (main.center_of_mass() - ref2.center_of_mass() -
                                   x.real_value * axis ));
      ref2.set_weighted_gradient ( 1.0 / axis_norm * (ref1.center_of_mass() - main.center_of_mass() +
                                   x.real_value * axis ));
    } else {
      ref1.set_weighted_gradient ( 1.0 / axis_norm * (
        cvm::position_distance (ref2.center_of_mass(), main.center_of_mass()) - x.real_value * axis ));
      ref2.set_weighted_gradient ( 1.0 / axis_norm * (
        cvm::position_distance (main.center_of_mass(), ref1.center_of_mass()) + x.real_value * axis ));
    }
  }
}

void colvar::distance_z::calc_force_invgrads()
{
  main.read_system_forces();

  if (fixed_axis && !b_1site_force) {
    ref1.read_system_forces();
    ft.real_value = 0.5 * ((main.system_force() - ref1.system_force()) * axis);
  } else {
    ft.real_value = main.system_force() * axis;
  }
}

void colvar::distance_z::calc_Jacobian_derivative()
{
  jd.real_value = 0.0;
}

void colvar::distance_z::apply_force (colvarvalue const &force)
{
  if (!ref1.noforce)
    ref1.apply_colvar_force (force.real_value);

  if (ref2.size() && !ref2.noforce)
    ref2.apply_colvar_force (force.real_value);

  if (!main.noforce)
    main.apply_colvar_force (force.real_value);
}



colvar::distance_xy::distance_xy (std::string const &conf)
  : distance_z (conf)
{
  function_type = "distance_xy";
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  x.type (colvarvalue::type_scalar);
}

colvar::distance_xy::distance_xy()
  : distance_z()
{
  function_type = "distance_xy";
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  x.type (colvarvalue::type_scalar);
}

void colvar::distance_xy::calc_value()
{
  ref1.reset_atoms_data();
  main.reset_atoms_data();

  ref1.read_positions();
  main.read_positions();

  if (b_no_PBC) {
    dist_v = main.center_of_mass() - ref1.center_of_mass();
  } else {
    dist_v = cvm::position_distance (ref1.center_of_mass(),
                                     main.center_of_mass());
  }
  if (!fixed_axis) {
    ref2.reset_atoms_data();
    ref2.read_positions();

    if (b_no_PBC) {
      v12 = ref2.center_of_mass() - ref1.center_of_mass();
    } else {
      v12 = cvm::position_distance (ref1.center_of_mass(), ref2.center_of_mass());
    }
    axis_norm = v12.norm();
    axis = v12.unit();
  }

  dist_v_ortho = dist_v - (dist_v * axis) * axis;
  x.real_value = dist_v_ortho.norm();
}

void colvar::distance_xy::calc_gradients()
{
  // Intermediate quantity (r_P3 / r_12 where P is the projection
  // of 3 (main) on the plane orthogonal to 12, containing 1 (ref1))
  cvm::real A;
  cvm::real x_inv;

  if (x.real_value == 0.0) return;
  x_inv = 1.0 / x.real_value;

  if (fixed_axis) {
    ref1.set_weighted_gradient (-1.0 * x_inv * dist_v_ortho);
    main.set_weighted_gradient (       x_inv * dist_v_ortho);
  } else {
    if (b_no_PBC) {
      v13 = main.center_of_mass() - ref1.center_of_mass();
    } else {
      v13 = cvm::position_distance (ref1.center_of_mass(), main.center_of_mass());
    }
    A = (dist_v * axis) / axis_norm;

    ref1.set_weighted_gradient ( (A - 1.0) * x_inv * dist_v_ortho);
    ref2.set_weighted_gradient ( -A        * x_inv * dist_v_ortho);
    main.set_weighted_gradient (      1.0  * x_inv * dist_v_ortho);
  }
}

void colvar::distance_xy::calc_force_invgrads()
{
  main.read_system_forces();

  if (fixed_axis && !b_1site_force) {
    ref1.read_system_forces();
    ft.real_value = 0.5 / x.real_value * ((main.system_force() - ref1.system_force()) * dist_v_ortho);
  } else {
    ft.real_value = 1.0 / x.real_value * main.system_force() * dist_v_ortho;
  }
}

void colvar::distance_xy::calc_Jacobian_derivative()
{
  jd.real_value = x.real_value ? (1.0 / x.real_value) : 0.0;
}

void colvar::distance_xy::apply_force (colvarvalue const &force)
{
  if (!ref1.noforce)
    ref1.apply_colvar_force (force.real_value);

  if (ref2.size() && !ref2.noforce)
    ref2.apply_colvar_force (force.real_value);

  if (!main.noforce)
    main.apply_colvar_force (force.real_value);
}



colvar::distance_dir::distance_dir (std::string const &conf)
  : distance (conf)
{
  function_type = "distance_dir";
  x.type (colvarvalue::type_unitvector);
}


colvar::distance_dir::distance_dir()
  : distance()
{
  function_type = "distance_dir";
  x.type (colvarvalue::type_unitvector);
}


void colvar::distance_dir::calc_value()
{
  group1.reset_atoms_data();
  group2.reset_atoms_data();

  group1.read_positions();
  group2.read_positions();

  if (b_no_PBC) {
    dist_v = group2.center_of_mass() - group1.center_of_mass();
  } else {
    dist_v = cvm::position_distance (group1.center_of_mass(),
                                     group2.center_of_mass());
  }
  x.rvector_value = dist_v.unit();
}


void colvar::distance_dir::calc_gradients()
{
  // gradients are computed on the fly within apply_force()
  // Note: could be a problem if a future bias relies on gradient
  // calculations...
}


void colvar::distance_dir::apply_force (colvarvalue const &force)
{
  // remove the radial force component
  cvm::real const iprod = force.rvector_value * x.rvector_value;
  cvm::rvector const force_tang = force.rvector_value - iprod * x.rvector_value;

  if (!group1.noforce)
    group1.apply_force (-1.0 * force_tang);

  if (!group2.noforce)
    group2.apply_force (       force_tang);
}



colvar::distance_inv::distance_inv (std::string const &conf)
  : distance (conf)
{
  function_type = "distance_inv";
  get_keyval (conf, "exponent", exponent, 6);
  if (exponent%2) {
    cvm::fatal_error ("Error: odd exponent provided, can only use even ones.\n");
  }
  if (exponent <= 0) {
    cvm::fatal_error ("Error: negative or zero exponent provided.\n");
  }

  for (cvm::atom_iter ai1 = group1.begin(); ai1 != group1.end(); ai1++) {
    for (cvm::atom_iter ai2 = group2.begin(); ai2 != group2.end(); ai2++) {
      if (ai1->id == ai2->id)
        cvm::fatal_error ("Error: group1 and group1 have some atoms in common: this is not allowed for distanceInv.\n");
    }
  }

  b_inverse_gradients = false;
  b_Jacobian_derivative = false;
  x.type (colvarvalue::type_scalar);
}

colvar::distance_inv::distance_inv()
{
  function_type = "distance_inv";
  exponent = 6;
  b_inverse_gradients = false;
  b_Jacobian_derivative = false;
  b_1site_force = false;
  x.type (colvarvalue::type_scalar);
}

void colvar::distance_inv::calc_value()
{
  group1.reset_atoms_data();
  group2.reset_atoms_data();

  group1.read_positions();
  group2.read_positions();

  x.real_value = 0.0;
  if (b_no_PBC) {
    for (cvm::atom_iter ai1 = group1.begin(); ai1 != group1.end(); ai1++) {
      for (cvm::atom_iter ai2 = group2.begin(); ai2 != group2.end(); ai2++) {
        cvm::rvector const dv = ai2->pos - ai1->pos;
        cvm::real const d2 = dv.norm2();
        cvm::real dinv = 1.0;
        for (int ne = 0; ne < exponent/2; ne++)
          dinv *= 1.0/d2;
        x.real_value += dinv;
        cvm::rvector const dsumddv = -(cvm::real (exponent)) * dinv/d2 * dv;
        ai1->grad += -1.0 * dsumddv;
        ai2->grad +=        dsumddv;
      }
    }
  } else {
    for (cvm::atom_iter ai1 = group1.begin(); ai1 != group1.end(); ai1++) {
      for (cvm::atom_iter ai2 = group2.begin(); ai2 != group2.end(); ai2++) {
        cvm::rvector const dv = cvm::position_distance (ai1->pos, ai2->pos);
        cvm::real const d2 = dv.norm2();
        cvm::real dinv = 1.0;
        for (int ne = 0; ne < exponent/2; ne++)
          dinv *= 1.0/d2;
        x.real_value += dinv;
        cvm::rvector const dsumddv = -(cvm::real (exponent)) * dinv/d2 * dv;
        ai1->grad += -1.0 * dsumddv;
        ai2->grad +=        dsumddv;
      }
    }
  }

  x.real_value *= 1.0 / cvm::real (group1.size() * group2.size());
  x.real_value = std::pow (x.real_value, -1.0/(cvm::real (exponent)));
}

void colvar::distance_inv::calc_gradients()
{
  cvm::real const dxdsum = (-1.0/(cvm::real (exponent))) * std::pow (x.real_value, exponent+1) / cvm::real (group1.size() * group2.size());
  for (cvm::atom_iter ai1 = group1.begin(); ai1 != group1.end(); ai1++) {
    ai1->grad *= dxdsum;
  }
  for (cvm::atom_iter ai2 = group2.begin(); ai2 != group2.end(); ai2++) {
    ai2->grad *= dxdsum;
  }
}

void colvar::distance_inv::apply_force (colvarvalue const &force)
{
  if (!group1.noforce)
    group1.apply_colvar_force (force.real_value);

  if (!group2.noforce)
    group2.apply_colvar_force (force.real_value);
}



colvar::gyration::gyration (std::string const &conf)
  : cvc (conf)
{
  function_type = "gyration";
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  parse_group (conf, "atoms", atoms);
  atom_groups.push_back (&atoms);
  x.type (colvarvalue::type_scalar);
}


colvar::gyration::gyration()
{
  function_type = "gyration";
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  x.type (colvarvalue::type_scalar);
}


void colvar::gyration::calc_value()
{
  atoms.reset_atoms_data();
  atoms.read_positions();
  atoms.apply_translation ((-1.0) * atoms.center_of_geometry());

  x.real_value = 0.0;
  for (cvm::atom_iter ai = atoms.begin(); ai != atoms.end(); ai++) {
    x.real_value += (ai->pos).norm2();
  }
  x.real_value = std::sqrt (x.real_value / cvm::real (atoms.size()));
}


void colvar::gyration::calc_gradients()
{
  cvm::real const drdx = 1.0/(cvm::real (atoms.size()) * x.real_value);
  for (cvm::atom_iter ai = atoms.begin(); ai != atoms.end(); ai++) {
    ai->grad = drdx * ai->pos;
  }
}


void colvar::gyration::calc_force_invgrads()
{
  atoms.read_system_forces();

  cvm::real const dxdr = 1.0/x.real_value;
  ft.real_value = 0.0;

  for (cvm::atom_iter ai = atoms.begin(); ai != atoms.end(); ai++) {
    ft.real_value += dxdr * ai->pos * ai->system_force;
  }
}


void colvar::gyration::calc_Jacobian_derivative()
{
  jd = x.real_value ? (3.0 * cvm::real (atoms.size()) - 4.0) / x.real_value : 0.0;
}


void colvar::gyration::apply_force (colvarvalue const &force)
{
  if (!atoms.noforce)
    atoms.apply_colvar_force (force.real_value);
}



colvar::inertia::inertia (std::string const &conf)
  : cvc (conf)
{
  function_type = "inertia";
  parse_group (conf, "atoms", atoms);
  atom_groups.push_back (&atoms);
  x.type (colvarvalue::type_scalar);
}


colvar::inertia::inertia()
{
  function_type = "inertia";
  x.type (colvarvalue::type_scalar);
}


void colvar::inertia::calc_value()
{
  atoms.reset_atoms_data();
  atoms.read_positions();
  atoms.apply_translation ((-1.0) * atoms.center_of_mass());

  x.real_value = 0.0;
  for (cvm::atom_iter ai = atoms.begin(); ai != atoms.end(); ai++) {
    x.real_value += ai->mass * (ai->pos).norm2();
  }
}


void colvar::inertia::calc_gradients()
{
  for (cvm::atom_iter ai = atoms.begin(); ai != atoms.end(); ai++) {
    ai->grad = 2.0 * ai->mass * ai->pos;
  }
}


void colvar::inertia::apply_force (colvarvalue const &force)
{
  if (!atoms.noforce)
    atoms.apply_colvar_force (force.real_value);
}


colvar::inertia_z::inertia_z (std::string const &conf)
  : inertia (conf)
{
  function_type = "inertia_z";
  if (get_keyval (conf, "axis", axis, cvm::rvector (0.0, 0.0, 1.0))) {
    if (axis.norm2() == 0.0)
      cvm::fatal_error ("Axis vector is zero!");
    if (axis.norm2() != 1.0) {
      axis = axis.unit();
      cvm::log ("The normalized axis is: "+cvm::to_str (axis)+".\n");
    }
  }
  x.type (colvarvalue::type_scalar);
}


colvar::inertia_z::inertia_z()
{
  function_type = "inertia_z";
  x.type (colvarvalue::type_scalar);
}


void colvar::inertia_z::calc_value()
{
  atoms.reset_atoms_data();
  atoms.read_positions();
  atoms.apply_translation ((-1.0) * atoms.center_of_mass());

  x.real_value = 0.0;
  for (cvm::atom_iter ai = atoms.begin(); ai != atoms.end(); ai++) {
    cvm::real const iprod = ai->pos * axis;
    x.real_value += ai->mass * iprod * iprod;
  }
}


void colvar::inertia_z::calc_gradients()
{
  for (cvm::atom_iter ai = atoms.begin(); ai != atoms.end(); ai++) {
    ai->grad = 2.0 * ai->mass * (ai->pos * axis) * axis;
  }
}


void colvar::inertia_z::apply_force (colvarvalue const &force)
{
  if (!atoms.noforce)
    atoms.apply_colvar_force (force.real_value);
}



colvar::rmsd::rmsd (std::string const &conf)
  : cvc (conf)
{
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  function_type = "rmsd";
  x.type (colvarvalue::type_scalar);

  parse_group (conf, "atoms", atoms);
  atom_groups.push_back (&atoms);

  if (atoms.b_dummy) 
    cvm::fatal_error ("Error: \"atoms\" cannot be a dummy atom.");

  if (!atoms.b_prevent_fitting) {
    // fit everything, unless the user made an explicit choice
    cvm::log ("No value was specified within \"atoms\" for \"centerReference\" or "
              "\"rotateReference\": enabling both.\n");
    // NOTE: this won't work for class V cvc's
    atoms.b_center = true;
    atoms.b_rotate = true;
  }

  if (atoms.b_center || atoms.b_rotate) {
    // request the calculation of the derivatives of the rotation defined by the atom group
    atoms.rot.request_group1_gradients (atoms.size());
    // request derivatives of optimal rotation wrt 2nd group for Jacobian: this is only
    // required for ABF, but we do both groups here for better caching
    atoms.rot.request_group2_gradients (atoms.size());

    if (atoms.ref_pos_group != NULL) {
      cvm::log ("The option \"refPositionsGroup\" (alternative group for fitting) was enabled: "
                "Jacobian derivatives of the RMSD will not be calculated.\n");
      b_Jacobian_derivative = false;
    }
  }

  // the following is a simplified version of the corresponding atom group options: 
  // to define the reference coordinates to compute this variable
  if (get_keyval (conf, "refPositions", ref_pos, ref_pos)) {
    cvm::log ("Using reference positions from configuration file to calculate the variable.\n");
    if (ref_pos.size() != atoms.size()) {
      cvm::fatal_error ("Error: the number of reference positions provided ("+
                        cvm::to_str (ref_pos.size())+
                        ") does not match the number of atoms of group \"atoms\" ("+
                        cvm::to_str (atoms.size())+").\n");
    }
  }
  {
    std::string ref_pos_file;
    if (get_keyval (conf, "refPositionsFile", ref_pos_file, std::string (""))) {

      if (ref_pos.size()) {
        cvm::fatal_error ("Error: cannot specify \"refPositionsFile\" and "
                          "\"refPositions\" at the same time.\n");
      }

      std::string ref_pos_col;
      double ref_pos_col_value;
      
      if (get_keyval (conf, "refPositionsCol", ref_pos_col, std::string (""))) {
        // if provided, use PDB column to select coordinates
        bool found = get_keyval (conf, "refPositionsColValue", ref_pos_col_value, 0.0);
        if (found && !ref_pos_col_value)
          cvm::fatal_error ("Error: refPositionsColValue, "
                            "if provided, must be non-zero.\n");
      } else {
        // if not, rely on existing atom indices for the group
        atoms.create_sorted_ids();
      }
      cvm::load_coords (ref_pos_file.c_str(), ref_pos, atoms.sorted_ids,
                        ref_pos_col, ref_pos_col_value);
    }
  }
  


}

  
void colvar::rmsd::calc_value()
{
  atoms.reset_atoms_data();
  atoms.read_positions();
  // rotational fit is done internally

  // atoms_cog = atoms.center_of_geometry();
  // rot.calc_optimal_rotation (ref_pos, atoms.positions_shifted (-1.0 * atoms_cog));

  // cvm::real group_pos_sum2 = 0.0;
  // for (size_t i = 0; i < atoms.size(); i++) {
  //   group_pos_sum2 += (atoms[i].pos - atoms_cog).norm2();
  // }

  // // value of the RMSD (Coutsias et al)
  // cvm::real const MSD = 1.0/(cvm::real (atoms.size())) *
  //   ( group_pos_sum2 + ref_pos_sum2 - 2.0 * rot.lambda );

  // x.real_value = (MSD > 0.0) ? std::sqrt (MSD) : 0.0;

  x.real_value = 0.0;
  for (size_t ia = 0; ia < atoms.size(); ia++) {
    x.real_value += (atoms[ia].pos - ref_pos[ia]).norm2();
  }
  x.real_value /= cvm::real (atoms.size()); // MSD
  x.real_value = (x.real_value > 0.0) ? std::sqrt (x.real_value) : 0.0;
}


void colvar::rmsd::calc_gradients()
{
  cvm::real const drmsddx2 = (x.real_value > 0.0) ?
    0.5 / (x.real_value * cvm::real (atoms.size())) :
    0.0;

  for (size_t ia = 0; ia < atoms.size(); ia++) {
    atoms[ia].grad = (drmsddx2 * 2.0 * (atoms[ia].pos - atoms.ref_pos[ia]));
  }
}


void colvar::rmsd::apply_force (colvarvalue const &force)
{
  if (!atoms.noforce)
    atoms.apply_colvar_force (force.real_value);
}


void colvar::rmsd::calc_force_invgrads()
{
  atoms.read_system_forces();
  ft.real_value = 0.0;
    
  // Note: gradient square norm is 1/N_atoms
          
  for (size_t ia = 0; ia < atoms.size(); ia++) {
    ft.real_value += atoms[ia].grad * atoms[ia].system_force;
  }
  ft.real_value *= atoms.size();
}


void colvar::rmsd::calc_Jacobian_derivative()
{
  // divergence of the rotated coordinates (including only derivatives of the rotation matrix)
  cvm::real divergence = 0.0;

  if (atoms.b_rotate) {

    // gradient of the rotation matrix
    cvm::matrix2d <cvm::rvector, 3, 3> grad_rot_mat;
    // gradients of products of 2 quaternion components 
    cvm::rvector g11, g22, g33, g01, g02, g03, g12, g13, g23;
    for (size_t ia = 0; ia < atoms.size(); ia++) {

      // Gradient of optimal quaternion wrt current Cartesian position
      cvm::vector1d< cvm::rvector, 4 >      &dq = atoms.rot.dQ0_1[ia];

      g11 = 2.0 * (atoms.rot.q)[1]*dq[1];
      g22 = 2.0 * (atoms.rot.q)[2]*dq[2];
      g33 = 2.0 * (atoms.rot.q)[3]*dq[3];
      g01 = (atoms.rot.q)[0]*dq[1] + (atoms.rot.q)[1]*dq[0];
      g02 = (atoms.rot.q)[0]*dq[2] + (atoms.rot.q)[2]*dq[0];
      g03 = (atoms.rot.q)[0]*dq[3] + (atoms.rot.q)[3]*dq[0];
      g12 = (atoms.rot.q)[1]*dq[2] + (atoms.rot.q)[2]*dq[1];
      g13 = (atoms.rot.q)[1]*dq[3] + (atoms.rot.q)[3]*dq[1];
      g23 = (atoms.rot.q)[2]*dq[3] + (atoms.rot.q)[3]*dq[2];

      // Gradient of the rotation matrix wrt current Cartesian position
      grad_rot_mat[0][0] = -2.0 * (g22 + g33); 
      grad_rot_mat[1][0] =  2.0 * (g12 + g03); 
      grad_rot_mat[2][0] =  2.0 * (g13 - g02); 
      grad_rot_mat[0][1] =  2.0 * (g12 - g03); 
      grad_rot_mat[1][1] = -2.0 * (g11 + g33); 
      grad_rot_mat[2][1] =  2.0 * (g01 + g23); 
      grad_rot_mat[0][2] =  2.0 * (g02 + g13); 
      grad_rot_mat[1][2] =  2.0 * (g23 - g01); 
      grad_rot_mat[2][2] = -2.0 * (g11 + g22); 

      cvm::atom_pos &x = atoms[ia].pos; 

      for (size_t i = 0; i < 3; i++) {
        for (size_t j = 0; j < 3; j++) {
          divergence += grad_rot_mat[i][j][i] * x[j];
        }
      }
    }
  }

  jd.real_value = x.real_value > 0.0 ? (3.0 * atoms.size() - 4.0 - divergence) / x.real_value : 0.0;
}




colvar::eigenvector::eigenvector (std::string const &conf)
  : cvc (conf)
{
  b_inverse_gradients = true;
  b_Jacobian_derivative = true;
  function_type = "eigenvector";
  x.type (colvarvalue::type_scalar);

  parse_group (conf, "atoms", atoms);
  atom_groups.push_back (&atoms);

  if (atoms.b_rotate) {
    cvm::fatal_error ("Error: rotateReference should be disabled:"
        "eigenvector component will set it internally.");
  }

  if (get_keyval (conf, "refPositions", ref_pos, ref_pos)) {
    cvm::log ("Using reference positions from input file.\n");
    if (ref_pos.size() != atoms.size()) {
      cvm::fatal_error ("Error: reference positions do not "
                        "match the number of requested atoms.\n");
    }
  }

  {
    std::string file_name;
    if (get_keyval (conf, "refPositionsFile", file_name)) {

      std::string file_col;
      double file_col_value;
      if (get_keyval (conf, "refPositionsCol", file_col, std::string (""))) {
        // use PDB flags if column is provided
        bool found = get_keyval (conf, "refPositionsColValue", file_col_value, 0.0);
        if (found && !file_col_value)
          cvm::fatal_error ("Error: refPositionsColValue, "
                            "if provided, must be non-zero.\n");
      } else {
        // if not, use atom indices
        atoms.create_sorted_ids();
      }

      ref_pos.resize (atoms.size());
      cvm::load_coords (file_name.c_str(), ref_pos, atoms.sorted_ids, file_col, file_col_value);
    }
  }

  // Set mobile frame of reference for atom group
  atoms.b_center = true;
  atoms.b_rotate = true;
  atoms.ref_pos = ref_pos;

  // now load the eigenvector
  if (get_keyval (conf, "vector", eigenvec, eigenvec)) {
    cvm::log ("Using vector components from input file.\n");
    if (eigenvec.size() != atoms.size()) {
      cvm::fatal_error ("Error: vector components do not "
                        "match the number of requested atoms.\n");
    }
  }

  {
    std::string file_name;
    if (get_keyval (conf, "vectorFile", file_name)) {

      std::string file_col;
      if (!get_keyval (conf, "vectorCol", file_col, std::string (""))) {
        cvm::fatal_error ("Error: parameter vectorCol is required if vectorFile is set.\n");
      }

      double file_col_value;
      bool found = get_keyval (conf, "vectorColValue", file_col_value, 0.0);
      if (found && !file_col_value)
        cvm::fatal_error ("Error: eigenvectorColValue, "
                          "if provided, must be non-zero.\n");

      eigenvec.resize (atoms.size());
      cvm::load_coords (file_name.c_str(), eigenvec, atoms.sorted_ids, file_col, file_col_value);
    }
  }

  if (!ref_pos.size() || !eigenvec.size()) {
    cvm::fatal_error ("Error: both reference coordinates"
                      "and eigenvector must be defined.\n");
  }

  cvm::rvector center (0.0, 0.0, 0.0);
  eigenvec_invnorm2 = 0.0;

  for (size_t i = 0; i < atoms.size(); i++) {
    center += eigenvec[i];
  }

  cvm::log ("Subtracting sum of eigenvector components: " + cvm::to_str (center) + "\n");

  for (size_t i = 0; i < atoms.size(); i++) {
    eigenvec[i] = eigenvec[i] - center;
    eigenvec_invnorm2 += eigenvec[i].norm2();
  }
  eigenvec_invnorm2 = 1.0 / eigenvec_invnorm2;

  // request derivatives of optimal rotation wrt 2nd group
  // for Jacobian
  atoms.rot.request_group1_gradients(atoms.size());
  atoms.rot.request_group2_gradients(atoms.size());
}

  
void colvar::eigenvector::calc_value()
{
  atoms.reset_atoms_data();
  atoms.read_positions(); // this will also update atoms.rot

  x.real_value = 0.0;
  for (size_t i = 0; i < atoms.size(); i++) {
    x.real_value += (atoms[i].pos - ref_pos[i]) * eigenvec[i];
  }
}


void colvar::eigenvector::calc_gradients()
{
  // There are two versions of this code
  // The simple version is not formally exact, but its
  // results are numerically indistinguishable from the
  // exact one. The exact one is more expensive and possibly
  // less stable in cases where the optimal rotation
  // becomes ill-defined.

  // Version A: simple, intuitive, cheap, robust. Wrong.
  // Works just fine in practice.
  for (size_t ia = 0; ia < atoms.size(); ia++) {
    atoms[ia].grad = eigenvec[ia];
  }

/*
  // Version B: complex, expensive, fragile. Right.
 
  // gradient of the rotation matrix
  cvm::matrix2d <cvm::rvector, 3, 3> grad_rot_mat;

  cvm::quaternion &quat0 = atoms.rot.q;

  // gradients of products of 2 quaternion components
  cvm::rvector g11, g22, g33, g01, g02, g03, g12, g13, g23;

  // a term that involves the rotation gradients
  cvm::rvector rot_grad_term;

  cvm::atom_pos x_relative;
  cvm::atom_pos atoms_cog = atoms.center_of_geometry();

  for (size_t ia = 0; ia < atoms.size(); ia++) {

    // Gradient of optimal quaternion wrt current Cartesian position
    // WARNING: we want derivatives wrt the FIRST group here (unlike RMSD)
    cvm::vector1d< cvm::rvector, 4 >      &dq = atoms.rot.dQ0_1[ia];

    g11 = 2.0 * quat0[1]*dq[1];
    g22 = 2.0 * quat0[2]*dq[2];
    g33 = 2.0 * quat0[3]*dq[3];
    g01 = quat0[0]*dq[1] + quat0[1]*dq[0];
    g02 = quat0[0]*dq[2] + quat0[2]*dq[0];
    g03 = quat0[0]*dq[3] + quat0[3]*dq[0];
    g12 = quat0[1]*dq[2] + quat0[2]*dq[1];
    g13 = quat0[1]*dq[3] + quat0[3]*dq[1];
    g23 = quat0[2]*dq[3] + quat0[3]*dq[2];

    // Gradient of the rotation matrix wrt current Cartesian position
    grad_rot_mat[0][0] = -2.0 * (g22 + g33);
    grad_rot_mat[1][0] =  2.0 * (g12 + g03);
    grad_rot_mat[2][0] =  2.0 * (g13 - g02);
    grad_rot_mat[0][1] =  2.0 * (g12 - g03);
    grad_rot_mat[1][1] = -2.0 * (g11 + g33);
    grad_rot_mat[2][1] =  2.0 * (g01 + g23);
    grad_rot_mat[0][2] =  2.0 * (g02 + g13);
    grad_rot_mat[1][2] =  2.0 * (g23 - g01);
    grad_rot_mat[2][2] = -2.0 * (g11 + g22);

    // this term needs to be rotated back, so we sum it separately
    rot_grad_term.reset();

    for (size_t ja = 0; ja < atoms.size(); ja++) {
      x_relative = atoms[ja].pos - atoms_cog;

      for (size_t i = 0; i < 3; i++) {
        for (size_t j = 0; j < 3; j++) {
          rot_grad_term += eigenvec[ja][i] * grad_rot_mat[i][j] * x_relative[j];
        }
      }
    }

    // Rotate correction term back to reference frame
    atoms[ia].grad = eigenvec[ia] + quat0.rotate (rot_grad_term);
  }
*/
}


void colvar::eigenvector::apply_force (colvarvalue const &force)
{
  if (!atoms.noforce)
    atoms.apply_colvar_force (force.real_value);
}


void colvar::eigenvector::calc_force_invgrads()
{
  atoms.read_system_forces();
  ft.real_value = 0.0;
    
  for (size_t ia = 0; ia < atoms.size(); ia++) {
    ft.real_value += eigenvec_invnorm2 * atoms[ia].grad *
      atoms[ia].system_force;
  }
}


void colvar::eigenvector::calc_Jacobian_derivative()
{
  // gradient of the rotation matrix
  cvm::matrix2d <cvm::rvector, 3, 3> grad_rot_mat;
  cvm::quaternion &quat0 = atoms.rot.q;

  // gradients of products of 2 quaternion components 
  cvm::rvector g11, g22, g33, g01, g02, g03, g12, g13, g23;

  cvm::atom_pos x_relative; 
  cvm::real sum = 0.0;

  for (size_t ia = 0; ia < atoms.size(); ia++) {

    // Gradient of optimal quaternion wrt current Cartesian position
    // trick: d(R^-1)/dx = d(R^t)/dx = (dR/dx)^t
    // we can just transpose the derivatives of the direct matrix
    cvm::vector1d< cvm::rvector, 4 >      &dq_1 = atoms.rot.dQ0_1[ia];

    g11 = 2.0 * quat0[1]*dq_1[1];
    g22 = 2.0 * quat0[2]*dq_1[2];
    g33 = 2.0 * quat0[3]*dq_1[3];
    g01 = quat0[0]*dq_1[1] + quat0[1]*dq_1[0];
    g02 = quat0[0]*dq_1[2] + quat0[2]*dq_1[0];
    g03 = quat0[0]*dq_1[3] + quat0[3]*dq_1[0];
    g12 = quat0[1]*dq_1[2] + quat0[2]*dq_1[1];
    g13 = quat0[1]*dq_1[3] + quat0[3]*dq_1[1];
    g23 = quat0[2]*dq_1[3] + quat0[3]*dq_1[2];

    // Gradient of the inverse rotation matrix wrt current Cartesian position
    // (transpose of the gradient of the direct rotation)
    grad_rot_mat[0][0] = -2.0 * (g22 + g33); 
    grad_rot_mat[0][1] =  2.0 * (g12 + g03); 
    grad_rot_mat[0][2] =  2.0 * (g13 - g02); 
    grad_rot_mat[1][0] =  2.0 * (g12 - g03); 
    grad_rot_mat[1][1] = -2.0 * (g11 + g33); 
    grad_rot_mat[1][2] =  2.0 * (g01 + g23); 
    grad_rot_mat[2][0] =  2.0 * (g02 + g13); 
    grad_rot_mat[2][1] =  2.0 * (g23 - g01); 
    grad_rot_mat[2][2] = -2.0 * (g11 + g22); 

    for (size_t i = 0; i < 3; i++) {
      for (size_t j = 0; j < 3; j++) {
        sum += grad_rot_mat[i][j][i] * eigenvec[ia][j];
      }
    }
  }

  jd.real_value = sum * std::sqrt (eigenvec_invnorm2); 
}


