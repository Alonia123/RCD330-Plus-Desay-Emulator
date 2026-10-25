#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# status.sh -> state of all services started by boot.sh
#
# chroot.sh /opt/mib2/status.sh
#

for pid_file in /tmp/log/*.pid
do
    name=$(basename "$pid_file" .pid)

    if kill -0 "$(cat "$pid_file")" 2>/dev/null
    then
        state=RUN
    else
        state=DEAD
    fi

    last_error=$(grep -E 'ERROR|FATAL|Segmentation|Abort' /tmp/log/$name.log | tail -1 | cut -c1-150)
    printf '%-22s %-5s %s\n' "$name" "$state" "$last_error"
done
