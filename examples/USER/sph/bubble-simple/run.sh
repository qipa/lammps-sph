#! /bin/bash

set -e
set -u
Lx=1.0
nx=80
ndim=2
np=8
D_heat_g=4.0
dT=0.01
Hwv=1.0
alpha=0.25
dprob=0.01
sph_rho_g=100
time_k=1.0
cv_g=4.0
# parameters for kana
lmp=../../../../src/lmp_linux
mpirun=mpirun
proc="-np ${np}"

dname=data-nx${nx}-ndim${ndim}-Lx${Lx}-D_heat_g${D_heat_g}-alpha${alpha}\
-Hwv${Hwv}-dprob${dprob}-time_k${time_k}-cv_g${cv_g}-sph_rho_g${sph_rho_g}-dT${dT}

rm -rf ${dname}
mkdir -p ${dname}
${mpirun} ${proc} ${lmp} -in insert.lmp \
    -var alpha ${alpha} -var D_heat_g ${D_heat_g} -var ndim ${ndim} -var nx ${nx} \
    -var Hwv ${Hwv} -var dprob ${dprob} -var time_k ${time_k} \
    -var Lx ${Lx} -var cv_g ${cv_g} -var sph_rho_g ${sph_rho_g} -var dT ${dT} \
    -var dname ${dname}