#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# boot.sh -> based on /svp/etc/init.sh "start"
#
#   boot.sh services   -> services only
#   boot.sh all        -> services + system apps (needs a running Weston, WAYLAND_DISPLAY)
#

. /opt/mib2/mib2-env.sh

#-----------------------------------------------------------------------------------
# SVP environment
#-----------------------------------------------------------------------------------

grep -vE '^\s*(mount|echo on|\. /home/root|export LD_PRELOAD)' /svp/etc/env_setup.sh > /tmp/env_setup.emu.sh
. /tmp/env_setup.emu.sh
. /svp/etc/func_startstop.sh

export LD_LIBRARY_PATH=/svp/lib:/svp/lib/hw:/opt/device/libfilt:/svp/lib/gstreamer/libs
export SVP_DEBUG_OUTPUT=console SVP_ENABLE_CGROUP=false SVP_ENABLE_PROCGUARD=false

#-----------------------------------------------------------------------------------
# Clean up temporary data
#-----------------------------------------------------------------------------------

LOG=/tmp/log

find /tmp/run -mindepth 1 -maxdepth 1 ! -name emu -exec rm -rf {} +
rm -rf "$LOG"
mkdir -p "$LOG" "$XDG_CACHE_HOME"

# svp_clear_tmp_data (func_cleanup.sh)
mkdir -p "$SVP_TMP_PATH/app" "$SVP_TMP_PATH/service/registry" "$SVP_TMP_DATA_PATH/fileinfo"

# svp_env_normal
mkdir -p "$SVP_USER_APP_INSTALL_PATH" "$SVP_USER_DATA_PATH/appinfo" "$SVP_USER_DATA_PATH/kanzi" "$SVP_USER_ETC_PATH"
[ -s "$SVP_USER_ROOT_PATH/ipod_config.xml" ] || cp -a /svp/bin/ipod_config.xml "$SVP_USER_ROOT_PATH/"
[ -s "$SVP_USER_DATA_PATH/HWR_SYS_SIN_UNI" ] || cp -a /svp/share/HWR_SYS_SIN_UNI "$SVP_USER_DATA_PATH/" 2>/dev/null

#-----------------------------------------------------------------------------------
# Start services (svp_start_service) -> with a log file per service
#-----------------------------------------------------------------------------------

# svc <priority> <name>
svc() {
    [ -x "/svp/bin/$2" ] || return 0
    echo -e "\e[92m [Info]: start service $2 \e[97m"
    (cd /svp/bin && exec nice -n "$1" "./$2" --debug-output console > "$LOG/$2.log" 2>&1) &
    echo $! > "$LOG/$2.pid"
    sleep "${SVC_DELAY:-1}"
}

svc -15 persistservice
svc -15 devicemgr
svc -15 audiomgr
svc -14 powermgr
svc -13 sourcemgr
svc -12 ilmservice
svc -11 appmgr

# SVPPower::PS_FIRST_CONNECT_COMPLETE
(powerctrl 2 > "$LOG/powerctrl.log" 2>&1 &)

for service in cameraservice canservice tunerservice auxservice btservice mediaservice ipodservice \
               exlapservice promptservice file-explorerservice updateservice engineeringservice \
               mdnsd carplayservice carlifeservice mirrorlinkservice androidautoservice \
               connectionservice miniplayerservice
do
    svc -7 $service
done

#-----------------------------------------------------------------------------------
# Start system apps
#-----------------------------------------------------------------------------------

# 12 vpanel | 13 statusbar | 16 tip | 14 launcher | 18 camera

if [ "$1" = "all" ]
then
    for app in "12 1 1" "13 1 0" "16 1 0" "14 1 1" "18 1 0"
    do
        set -- $app
        echo -e "\e[92m [Info]: app-run $1 \e[97m"
        app-run "$1" StartInShell "$2" "$3" >> "$LOG/app-run.log" 2>&1
    done
fi

echo -e "\e[92m [Info]: boot sequence done, logs in $LOG \e[97m"
wait
