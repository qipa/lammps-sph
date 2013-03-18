#! /bin/bash

set -e
set -u
configfile=$HOME/lammps-sph.sh
if [ -f "${configfile}" ]; then
    source "${configfile}"
else
    printf "cannot find config file: %s\n" ${configfile} > "/dev/stderr"
    exit -1
fi

nproc=6
ndim=3d
Nbeads=100
Nsolvent=100
Npoly=100
dname=dataNpoly-${Npoly}Nbeads-${Nbeads}

rm -rf dum* im* poly* log.lammps
${lmp} -var ndim ${ndim} -in sdpd-polymer-init.lmp
${restart2data} poly3d.restart poly3d.txt

awk -v Nbeads=${Nbeads} -v Nsolvent=${Nsolvent} -v Npoly=${Npoly} \
    -f addpolymer.awk poly3d.txt > poly3.txt
nbound=$(tail -n 1 poly3.txt | awk '{print $1}')
sed "s/_NUMBER_OF_BOUNDS_/$nbound/1" poly3.txt > poly3d.txt

# add molecular IDs
#awk -f addmollabel.awk poly3d.txt poly3d.txt poly3d.txt  > poly3m.txt
#mv poly3m.txt poly3d.txt

${mpirun} -np ${nproc} ${lmp} -var dname ${dname} -var ndim ${ndim} -in sdpd-polymer-run.lmp
