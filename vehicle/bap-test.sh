#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# bap-test.sh send a BAP screen request
#
# bap-test.sh <viewId> [ctrl=1] [wait_seconds=30]
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
L=$B/sysroot/tmp/log/appmgr.log
ID=$1
CTRL=${2:-1}
WAIT=${3:-30}

log_lines_before=$(wc -l < $L)

#-----------------------------------------------------------------------------------
# BAP request 0x7380 [viewId, ctrl]
#-----------------------------------------------------------------------------------

$B/scripts/mcu-send.sh 7380 $(printf '%02X %02X' $ID $CTRL)
sleep $WAIT

#-----------------------------------------------------------------------------------
# Result
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: MCU (last lines) \e[97m'
tail -4 $B/mcu.log

echo -e '\e[92m [Info]: appmgr.log since the request (filtered) \e[97m'
tail -n +$((log_lines_before + 1)) $L | sed -E 's/^\([0-9|xA-F]*\) //' | grep -vE "Binary shader|CFontMgr|NWET|SZF|mib2emu-shim\] (gl|egl)" \
    | grep -iE "BapScreen|ViewRequest|Dlg|Switch|ShowApp|RunApp|activity|mode|ERROR|WARN" | head -40 | cut -c1-180
