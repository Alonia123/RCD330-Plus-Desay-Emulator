#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# focus.sh -> set the input focus (pointer/touch/keyboard)
#
#   The output ("... succeeded") is evaluated by focus-daemon.py -> do not change it.
#

. /opt/mib2/mib2-env.sh

LayerManagerControl set surfaces "$1" input focus all 2>&1 | tail -1
