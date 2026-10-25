#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
FIFO=$B/sysroot/tmp/run/emu/audio.fifo
export PULSE_SERVER=unix:/mnt/wslg/PulseServer

mkdir -p "$(dirname $FIFO)"

if [ ! -p $FIFO ]
then
    rm -f $FIFO
    mkfifo -m 666 $FIFO
fi

#-----------------------------------------------------------------------------------
# Playback loop
#-----------------------------------------------------------------------------------

while true
do
    pacat --playback --raw --format=s16le --rate=48000 --channels=2 \
          --client-name=mib2emu --stream-name=MIB2 --latency-msec=100 < $FIFO
    sleep 0.2
done
