#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# check-deps.sh -> list the libraries that cannot be resolved, per program
#
# chroot.sh /opt/mib2/check-deps.sh
#

. /opt/mib2/mib2-env.sh

for binary in /svp/bin/* /storage/app/*/bin/*
do
    head -c4 "$binary" | grep -q ELF || continue

    missing=$(ldd "$binary" 2>&1 | awk '/not found/{print $1}' | sort -u | tr '\n' ' ')
    [ -n "$missing" ] && echo "$(basename "$binary"): $missing"
done

echo -e '\e[92m [Info]: check finished \e[97m'
