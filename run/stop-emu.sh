#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# stop-emu.sh -> stop the running emulator completely
#
# stop-emu.sh
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu

#-----------------------------------------------------------------------------------
# Stop host processes and all ARM processes
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: stop emulator... \e[97m'

pkill -f "[r]un-emu.sh"
pkill -f "$B/scripts/[m]cu-sim.py"
pkill -f "$B/scripts/[f]ocus-daemon.py"
pkill -f "$B/scripts/[a]udio-bridge.sh"
pkill -f "[p]acat --playback --raw"
pkill -f "$B/[g]lfwd-server"
pkill -f "[g]db-multiarch"
pkill -9 -f "[q]emu-binfmt/arm"
pkill -9 -f "[q]emu-arm"
sleep 1

remaining=$(pgrep -fc "[q]emu-binfmt/arm")
echo -e "\e[92m [Info]: emulator stopped (remaining ARM processes: $remaining) \e[97m"
