#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# mcu-send.sh -> send a message to the running MCU simulator
#
# mcu-send.sh 7380 00 01        -> cmd 0x7380 with data 00 01 (BAP: show screen 0)
# mcu-send.sh raw 03 73 80 ...  -> raw bytes
#

FIFO=/home/ubuntu/mib2emu/mcu.ctl

if [ ! -p $FIFO ] || ! pgrep -f "/home/ubuntu/mib2emu/scripts/[m]cu-sim.py" > /dev/null
then
    echo -e "\e[0;31m [Error]: MCU simulator is not running \e[97m"
    exit 1
fi

if [ "$1" = raw ]
then
    shift
    echo "$*" > $FIFO
else
    echo "cmd $*" > $FIFO
fi
