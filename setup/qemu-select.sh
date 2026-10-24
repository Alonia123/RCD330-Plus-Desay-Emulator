#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# qemu-select.sh -> select the qemu interpreter for ARM programs
#

set -e

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

E=/proc/sys/fs/binfmt_misc/qemu-arm
MAGIC='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00'
MASK='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'

case "$1" in
    new)
        INTERP=/opt/qemu10/qemu-arm-static
        ;;
    old)
        INTERP=/usr/libexec/qemu-binfmt/arm-binfmt-P
        ;;
    *)
        grep -E "interpreter|flags" $E
        exit 0
        ;;
esac

#-----------------------------------------------------------------------------------
# Register interpreter again
#-----------------------------------------------------------------------------------

if [ ! -x "$INTERP" ]
then
    echo -e "\e[0;31m [Error]: missing: $INTERP \e[97m"
    exit 1
fi

[ -e $E ] && echo -1 > $E
echo ":qemu-arm:M::$MAGIC:$MASK:$INTERP:FP" > /proc/sys/fs/binfmt_misc/register

echo -e "\e[92m [Info]: qemu interpreter set: \e[97m"
grep -E "interpreter|flags" $E
