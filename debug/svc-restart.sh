#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# svc-restart.sh -> restart SVP service
#
# svc-restart.sh <service> [gdb_port]
#

. /opt/mib2/mib2-env.sh

#-----------------------------------------------------------------------------------
# SVP environment
#-----------------------------------------------------------------------------------

grep -vE '^\s*(mount|echo on|\. /home/root|export LD_PRELOAD)' /svp/etc/env_setup.sh > /tmp/env_setup.emu.sh
. /tmp/env_setup.emu.sh

export LD_LIBRARY_PATH=/svp/lib:/svp/lib/hw:/opt/device/libfilt:/svp/lib/gstreamer/libs
export SVP_DEBUG_OUTPUT=console SVP_ENABLE_CGROUP=false SVP_ENABLE_PROCGUARD=false

. /opt/mib2/mib2-env.sh

S=$1

#-----------------------------------------------------------------------------------
# Stop service
#-----------------------------------------------------------------------------------

pkill -9 -f "[.]/$S --debug-output" 2>/dev/null
sleep 2

#-----------------------------------------------------------------------------------
# Start service again
#-----------------------------------------------------------------------------------

cd /svp/bin

if [ -n "$2" ]
then
    setsid nohup /opt/mib2/qemu-arm10 -g "$2" ./$S --debug-output console > /tmp/log/$S.log 2>&1 < /dev/null &
else
    setsid nohup ./$S --debug-output console > /tmp/log/$S.log 2>&1 < /dev/null &
fi

echo -e "\e[92m [Info]: $S restarted (pid $!) \e[97m"
