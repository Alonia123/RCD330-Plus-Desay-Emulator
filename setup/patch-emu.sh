#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# patch-emu.sh -> Host side: binary patches for the emulation
#   Every patch checks the expected original bytes and can be run again.
#

set -e

ROOT=/home/ubuntu/mib2emu/sysroot

#-----------------------------------------------------------------------------------
# patch <file> <offset> <old_hex> <new_hex> <description>
#-----------------------------------------------------------------------------------

patch() {
    local file=$1 offset=$2 old=$3 new=$4 current

    current=$(xxd -p -s $offset -l $(( ${#old} / 2 )) "$file")

    if [ "$current" = "$new" ]
    then
        echo -e "\e[92m [Info]: already patched: $5 \e[97m"
        return
    fi

    if [ "$current" != "$old" ]
    then
        echo -e "\e[0;31m [Error]: $5 -> expected $old, found $current \e[97m"
        exit 1
    fi

    echo "$new" | xxd -r -p | dd of="$file" bs=1 seek=$offset conv=notrunc status=none
    echo -e "\e[92m [Info]: patched: $5 \e[97m"
}

#-----------------------------------------------------------------------------------
# appmgr -> wait time for the answers of the apps 10000 ms -> 65535 ms (qemu is slow)
#-----------------------------------------------------------------------------------

patch $ROOT/svp/bin/appmgr $((0x744f0 - 0x8000)) 101702e3 ff1f0fe3 "appmgr SVPAppActivity::Start() timeout 65s"
patch $ROOT/svp/bin/appmgr $((0x73e0c - 0x8000)) 101702e3 ff1f0fe3 "appmgr SVPAppActivity::FG() timeout 65s"
patch $ROOT/svp/bin/appmgr $((0x73f18 - 0x8000)) 101702e3 ff1f0fe3 "appmgr SVPAppActivity::BG() timeout 65s"
