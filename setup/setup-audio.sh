#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# setup-audio.sh -> set up ALSA in the sysroot
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
R=$B/sysroot

#-----------------------------------------------------------------------------------
# ALSA configuration
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: copy ALSA base configuration of the device... \e[97m'
mkdir -p $R/usr/share/alsa
cp -a $R/opt/device/usr/share/alsa/. $R/usr/share/alsa/
cp $B/scripts/asound-emu.conf $R/etc/asound.conf
chmod +x $B/scripts/audio-bridge.sh

#-----------------------------------------------------------------------------------
# pacat on the host
#-----------------------------------------------------------------------------------

if ! command -v pacat > /dev/null
then
    echo -e '\e[92m [Info]: install pulseaudio-utils... \e[97m'
    apt-get install -y -qq pulseaudio-utils
fi

echo -e "\e[92m [Info]: ALSA set up: $R/etc/asound.conf \e[97m"
