#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# install.sh -> complete installation
#
# wsl --install Ubuntu-22.04 --name RCD330-Plus-Desay-Emulator
#
# wsl -d RCD330-Plus-Desay-Emulator -u root -- bash /mnt/c/<path to repository>/setup/install.sh "<dump folder>"
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

REPO=$(cd "$(dirname "$0")/.." && pwd)
B=/home/ubuntu/mib2emu
S=$B/scripts
DUMP_SOURCE="$1"

if [ -z "$DUMP_SOURCE" ]
then
    echo -e '\e[0;31m [Info]: usage: install.sh "<dump folder>" \e[97m'
    exit 1
fi

# stop at the first failed step
step() {
    echo
    echo -e "\e[92m ---------- $1 ---------- \e[97m"
    shift
    "$@" || { echo -e "\e[0;31m [Error]: step failed -> fix the problem and run install.sh again \e[97m"; exit 1; }
}

#-----------------------------------------------------------------------------------
# Installation
#-----------------------------------------------------------------------------------

step "1/9 Ubuntu packages"        bash "$REPO/setup/install-packages.sh"
step "2/9 Debian sysroot"         bash "$REPO/setup/build-sysroot.sh"
step "3/9 copy scripts to WSL"    bash "$REPO/deploy-scripts.sh" "$REPO"
step "4/9 copy device dump"       $S/copy-dump.sh "$DUMP_SOURCE"
step "5/9 runtime root"           $S/build-rootfs.sh
step "6/9 appmgr patch"           $S/patch-emu.sh
step "7/9 hardware shim"          $S/build-shim.sh
step "8/9 GL forwarding"          $S/build-glfwd.sh
step "9/9 audio"                  $S/setup-audio.sh

echo
echo -e '\e[92m [Info]: installation finished. Start the emulator in an own PowerShell window: \e[97m'
echo -e '\e[92m         wsl -d RCD330-Plus-Desay-Emulator -u root -- /home/ubuntu/mib2emu/scripts/emu_main.sh \e[97m'
