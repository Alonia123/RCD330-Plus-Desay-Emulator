#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************

set -e

#-----------------------------------------------------------------------------------
# Packages
#   qemu-user-static, binfmt-support     -> run ARM programs
#   debootstrap, wget, gpg               -> create the Debian sysroot (keys from debian.org)
#   gcc-arm-linux-gnueabihf              -> build shim + GL client (ARM), readelf for build-rootfs.sh
#   gcc, libegl-dev, libgles-dev,
#   libegl-mesa0, libgl1-mesa-dri        -> build and run the GL server (Mesa on the PC)
#   python3                              -> MCU simulator, focus service, code generator
#   pulseaudio-utils                     -> sound to the Windows speaker (pacat)
#   dosfstools, ffmpeg                   -> virtual USB stick (FAT32 image, test tones)
#   xxd                                  -> patch-emu.sh
#-----------------------------------------------------------------------------------

PACKAGES="qemu-user-static binfmt-support debootstrap wget gpg
gcc-arm-linux-gnueabihf gcc libegl-dev libgles-dev libegl-mesa0 libgl1-mesa-dri
python3 pulseaudio-utils dosfstools ffmpeg xxd"

echo -e '\e[92m [Info]: install Ubuntu packages (takes a while)... \e[97m'
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $PACKAGES > /dev/null

if [ ! -e /proc/sys/fs/binfmt_misc/qemu-arm ]
then
    echo -e '\e[0;31m [Error]: binfmt entry qemu-arm missing -> restart WSL (wsl --shutdown) and run again \e[97m'
    exit 1
fi

echo -e '\e[92m [Info]: Ubuntu packages installed \e[97m'
