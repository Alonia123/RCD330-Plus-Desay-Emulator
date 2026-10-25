#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# menu.sh -> Sends a BAP screen request via the simulated MCU and brings the matching app to the front.
#
# menu.sh settings          -> settings (BAP viewId 0, app 41)
# menu.sh <viewId> <app>    -> any combination
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu

case "$1" in
    settings)
        VIEW=0
        APP=41
        ;;
    *)
        VIEW=$1
        APP=$2
        ;;
esac

#-----------------------------------------------------------------------------------
# BAP request (0x7380 [viewId, 01]) -> bring app to the front
#-----------------------------------------------------------------------------------

$B/scripts/mcu-send.sh 7380 $(printf '%02X' "$VIEW") 01
sleep 3

[ -n "$APP" ] && $B/scripts/chroot.sh /opt/mib2/app.sh "$APP" > /dev/null 2>&1

echo -e "\e[92m [Info]: BAP view $VIEW requested${APP:+, app $APP brought to the front} \e[97m"
