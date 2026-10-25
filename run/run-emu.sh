#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# run-emu.sh -> start the emulator
# MCU simulator -> audio bridge -> GL server -> device Weston -> services -> system apps
#
# run-emu.sh [services|all]   (default: all)
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
MODE=${1:-all}

#-----------------------------------------------------------------------------------
# Stop everything that is still running from a previous start
#-----------------------------------------------------------------------------------

stop_all() {
    pkill -f "$B/scripts/mcu-sim.py"
    pkill -f "$B/scripts/focus-daemon.py"
    pkill -f "$B/glfwd-server"
    pkill -f "$B/scripts/audio-bridge.sh"
    pkill -f "[p]acat --playback --raw"
    pkill -9 -f qemu-binfmt/arm
    pkill -9 -f qemu-arm
}
trap 'stop_all; exit 0' INT TERM

echo -e '\e[92m [Info]: stop old emulator processes... \e[97m'
stop_all
sleep 1

#-----------------------------------------------------------------------------------
# MCU simulator and audio
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: start MCU simulator... \e[97m'
python3 $B/scripts/mcu-sim.py --log $B/mcu.log --ctl $B/mcu.ctl > $B/mcu.out 2>&1 &

# Audio
if [ -S /mnt/wslg/PulseServer ]
then
    echo -e '\e[92m [Info]: start audio bridge... \e[97m'
    $B/scripts/audio-bridge.sh > $B/audio.log 2>&1 &
fi

#-----------------------------------------------------------------------------------
# GL forwarding -> native render server
#-----------------------------------------------------------------------------------

export MIB2_GLFWD=${MIB2_GLFWD:-1}

if [ "$MIB2_GLFWD" = 1 ] && [ -x $B/glfwd-server ]
then
    echo -e '\e[92m [Info]: start GL server... \e[97m'
    mkdir -p $B/sysroot/tmp/run/emu
    GALLIUM_DRIVER=${GLFWD_DRIVER:-llvmpipe} $B/glfwd-server $B/sysroot/tmp/run/emu/glfwd.sock > $B/glfwd-server.log 2>&1 &

    for counter in $(seq 25)
    do
        [ -S $B/sysroot/tmp/run/emu/glfwd.sock ] && break
        sleep 0.2
    done
fi

#-----------------------------------------------------------------------------------
# Device Weston (window in Windows)
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: start device Weston... \e[97m'
$B/scripts/chroot.sh /opt/mib2/weston.sh > $B/weston.out 2>&1 &

for counter in $(seq 50)
do
    [ -S $B/sysroot/tmp/xdg/wayland-0 ] && break
    sleep 0.2
done

echo -e "\e[92m [Info]: start services and apps (mode: $MODE)... \e[97m"
$B/scripts/chroot.sh /opt/mib2/boot.sh "$MODE" > $B/boot.log 2>&1 &

# Mouse control
python3 $B/scripts/focus-daemon.py > $B/focus.log 2>&1 &

echo
echo -e '\e[92m [Info]: emulator is running. Radio appears after approx. 1-2 min. \e[97m'
echo -e "\e[92m         Logs: $B/sysroot/tmp/log/  MCU: $B/mcu.log  Weston: $B/sysroot/tmp/weston.log \e[97m"
echo -e '\e[0;31m [Info]: Ctrl+C stops the emulator. \e[97m'
echo
wait
