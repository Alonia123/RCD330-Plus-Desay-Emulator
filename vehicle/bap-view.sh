#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# bap-view.sh request a BAP screen via the simulated MCU and show how the apps react
#
# bap-view.sh <viewId> [ctrl=1] [wait_seconds=15]

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
L=$B/sysroot/tmp/log/appmgr.log

log_lines_before=$(wc -l < $L)

#-----------------------------------------------------------------------------------
# BAP request 0x7380 [viewId, ctrl]
#-----------------------------------------------------------------------------------

$B/scripts/mcu-send.sh 7380 $(printf '%02X %02X' "$1" "${2:-1}")
sleep "${3:-15}"

tail -n +$((log_lines_before + 1)) $L | sed -E 's/^\([^)]*\) //' \
    | grep -E 'OnBapScreenViewRequest|RunApp|Switch Activity|ShowApp.*show: 1|carmenu|Carmenu|climate|BAPDataMgr|SIGSEGV' \
    | grep -vE 'CFontMgr|NWET|No-op' | head -12 | cut -c1-170
