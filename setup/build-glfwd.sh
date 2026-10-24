#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# build-glfwd.sh -> Host side: build the GL forwarding
#   ARM client -> sysroot/opt/mib2/libglfwd.so   (LD_PRELOAD in the device apps)
#   x86 server -> /home/ubuntu/mib2emu/glfwd-server
#

set -e

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
ROOT=$B/sysroot
S=$B/scripts
G=$B/glfwd-build

#-----------------------------------------------------------------------------------
# Generate code (opcodes, client stubs, server dispatcher)
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: generate GL forwarding code... \e[97m'
mkdir -p $G
python3 $S/glfwd-gen.py $G

#-----------------------------------------------------------------------------------
# ARM client
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: build ARM client libglfwd.so... \e[97m'
arm-linux-gnueabihf-gcc -shared -fPIC -O2 -Wall -Wno-unused-function \
    -I$G -I$S -idirafter $ROOT/usr/include \
    -o $ROOT/opt/mib2/libglfwd.so $S/glfwd-client.c \
    -ldl -lpthread \
    -L$ROOT/usr/lib/arm-linux-gnueabihf -lwayland-client \
    -Wl,--allow-shlib-undefined \
    -Wl,-rpath-link=$ROOT/lib/arm-linux-gnueabihf:$ROOT/usr/lib/arm-linux-gnueabihf

#-----------------------------------------------------------------------------------
# x86 server
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: build x86 server glfwd-server... \e[97m'
gcc -O2 -Wall -Wno-unused-function -I$G -I$S -o $B/glfwd-server $S/glfwd-server.c -lEGL -lGLESv2 -lpthread

echo -e "\e[92m [Info]: glfwd finished: $ROOT/opt/mib2/libglfwd.so, $B/glfwd-server \e[97m"
