#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# app.sh -> bring an app to the front via appmgr
#
#   app.sh <app_id> [mode]
#

. /opt/mib2/mib2-env.sh

case "$1" in
    151)
        mode=${2:-hk_start_app}
        ;;
    *)
        mode=${2:-default}
        ;;
esac

exec app-run "$1" "$mode" 1 1
