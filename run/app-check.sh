#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# app-check.sh -> bring an app to the front via appmgr and report the result
#
# app-check.sh <app_id> [wait_seconds=40]
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
L=$B/sysroot/tmp/log/appmgr.log
ID=$1
WAIT=${2:-40}

log_lines_before=$(wc -l < $L)

#-----------------------------------------------------------------------------------
# Request app and wait for appmgr's answer
#-----------------------------------------------------------------------------------

$B/scripts/chroot.sh /opt/mib2/app.sh "$ID" > /dev/null 2>&1

state=TIMEOUT

for counter in $(seq $((WAIT * 2)))
do
    new_lines=$(tail -n +$((log_lines_before + 1)) $L)

    if echo "$new_lines" | grep -qE "Switch Activity: [0-9]+ -> $ID\b|Activity \[$ID\] switch"
    then
        state=OK
        break
    fi

    if echo "$new_lines" | grep -qE "Start activity \($ID\) fail|Activity \[$ID\] Wait FG Fail"
    then
        state=FAIL
        break
    fi

    sleep 0.5
done

#-----------------------------------------------------------------------------------
# Check for crashes and which surface was shown
#-----------------------------------------------------------------------------------

sleep 6
new_lines=$(tail -n +$((log_lines_before + 1)) $L)

crash_count=$(echo "$new_lines" | grep -c "SIGSEGV\|SIGABRT\|SIGIOT")
surface=$(echo "$new_lines" | grep -oE "ShowApp\(\) - app_id: $ID, surface_id: [0-9]+, show: 1" | grep -oE "surface_id: [0-9]+" | tail -1)

[ "$crash_count" -gt 0 ] && state="$state+CRASH($crash_count)"

echo "$ID $state ${surface:-no-surface-shown}"
