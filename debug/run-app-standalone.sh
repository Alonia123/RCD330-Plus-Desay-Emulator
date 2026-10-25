#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# run-app-standalone.sh -> start a UI app without appmgr
#
# run-app-standalone.sh <package> <surface_id> [seconds=60]
#

. /opt/mib2/mib2-env.sh

#-----------------------------------------------------------------------------------
# SVP environment
#-----------------------------------------------------------------------------------

grep -vE '^\s*(mount|echo on|\. /home/root|export LD_PRELOAD)' /svp/etc/env_setup.sh > /tmp/env_setup.emu.sh
. /tmp/env_setup.emu.sh

PKG=/storage/app/$1
NAME=$(sed -n 's:.*<exec>bin/\(.*\)</exec>.*:\1:p' $PKG/manifest.xml)

export LD_LIBRARY_PATH=$PKG/lib:/svp/lib:/svp/lib/hw:/opt/device/libfilt
export SVP_SYS_APP_INSTALL_PATH=$PKG QT_IVI_SURFACE_ID=$2

#-----------------------------------------------------------------------------------
# Stop running instance and start the app
#-----------------------------------------------------------------------------------

pkill -9 -f "[s]torage/app/$1/bin"
sleep 1

cd $PKG
exec timeout "${3:-60}" env ${GDB_PORT:+QEMU_GDB=$GDB_PORT} bin/$NAME -mode default -name $NAME -surface "$2" -option --debug-output console
