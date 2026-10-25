#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
#
# chroot.sh                 -> interactive shell
# chroot.sh <command...>    -> run command with the device environment
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

ROOT=/home/ubuntu/mib2emu/sysroot
SHARED=/run/mib2-xdg

#-----------------------------------------------------------------------------------
# Mount (only if not mounted yet)
#-----------------------------------------------------------------------------------

mkdir -p $SHARED "$ROOT/tmp/xdg"
chmod 0700 $SHARED

# mnt <source> <target> <mount options...>
mnt() { mountpoint -q "$2" || mount "${@:3}" "$1" "$2"; }

mnt proc    "$ROOT/proc"    -t proc
mnt sysfs   "$ROOT/sys"     -t sysfs
mnt /dev    "$ROOT/dev"     --rbind
mnt $SHARED "$ROOT/tmp/xdg" --bind

if [ -d /mnt/wslg/runtime-dir ]
then
    mkdir -p "$ROOT/tmp/wslg"
    mnt /mnt/wslg/runtime-dir "$ROOT/tmp/wslg" --bind
fi

mkdir -p "$ROOT/dev/bus/usb"
cp /etc/resolv.conf "$ROOT/etc/resolv.conf" 2>/dev/null

#-----------------------------------------------------------------------------------
# Shell or command
#-----------------------------------------------------------------------------------

if [ $# -eq 0 ]
then
    exec chroot "$ROOT" /bin/bash -c '. /opt/mib2/mib2-env.sh; exec bash -i'
fi

exec chroot "$ROOT" /bin/bash -c '. /opt/mib2/mib2-env.sh; exec "$@"' _ "$@"
