#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# weston.sh -> start the original Weston 1.9
#
# weston.sh [WIDTH HEIGHT]   (default 800x480 = device LCD)
#

. /opt/mib2/mib2-env.sh

# real screen size (do not change!)
W=${1:-800}
H=${2:-480}

[ -e /usr/lib/weston ] || ln -s /opt/device/usr/lib/weston /usr/lib/weston

rm -f "$XDG_RUNTIME_DIR"/wayland-0 "$XDG_RUNTIME_DIR"/wayland-0.lock

exec env WAYLAND_DISPLAY=/tmp/wslg/wayland-0 \
    /opt/device/usr/bin/weston --backend=wayland-backend.so --use-pixman \
    --width="$W" --height="$H" --config=/opt/mib2/weston-emu.ini --idle-time=0 \
    --log=/tmp/weston.log
