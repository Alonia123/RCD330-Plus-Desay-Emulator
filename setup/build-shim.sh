#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# build-shim.sh -> build the LD_PRELOAD shim
#   Create the fake device files and fake root
#

set -e

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
O=$B/sysroot/opt/mib2
FR=$O/fakeroot

#-----------------------------------------------------------------------------------
# Build shim
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: build libmib2emu.so... \e[97m'
arm-linux-gnueabihf-gcc -shared -fPIC -O2 -Wall -o $O/libmib2emu.so $B/scripts/mib2emu-shim.c -ldl

#-----------------------------------------------------------------------------------
# Fake device files
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: create fake device files... \e[97m'
mkdir -p $FR/dev $FR/sys/devices/platform/power_detect

[ -f $FR/dev/mtd1 ] || head -c 262144 /dev/zero | tr '\0' '\377' > $FR/dev/mtd1

touch $FR/dev/power_detect
[ -f $FR/sys/devices/platform/power_detect/POWER_STATUS ] || echo 1 > $FR/sys/devices/platform/power_detect/POWER_STATUS

echo -e "\e[92m [Info]: shim + fakeroot finished: $O \e[97m"
