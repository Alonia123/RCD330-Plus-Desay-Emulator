#!/bin/sh
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# mib2-env.sh -> Runtime environment for device programs inside the chroot
#

#-----------------------------------------------------------------------------------
# Paths
#-----------------------------------------------------------------------------------

# gstreamer/libs -> env_setup.sh
export LD_LIBRARY_PATH=/svp/lib:/svp/lib/hw:/opt/device/libfilt:/svp/lib/gstreamer/libs
export PATH=/svp/bin:/usr/sbin:/usr/bin:/sbin:/bin:/opt/device/usr/bin
export XDG_RUNTIME_DIR=/tmp/xdg
export SVP_ROOT_PATH=/svp
export SVP_APP_PKG_PATH=/storage/update/app-pkg

#-----------------------------------------------------------------------------------
# Graphics -> Mesa instead of PowerVR
#-----------------------------------------------------------------------------------

export WAYLAND_DISPLAY=wayland-0
export EGL_PLATFORM=wayland
export LIBGL_ALWAYS_SOFTWARE=1

export GALLIUM_DRIVER=${GALLIUM_DRIVER:-softpipe}
export LP_NUM_THREADS=${LP_NUM_THREADS:-2}

export MESA_DEBUG=${MESA_DEBUG-1}
export FONTCONFIG_FILE=/opt/device/etc/fonts/fonts.conf

#-----------------------------------------------------------------------------------
# glibc -> sysroot glibc (bookworm) instead of device glibc 2.15
#-----------------------------------------------------------------------------------

export GLIBC_TUNABLES=glibc.malloc.tcache_count=0

#-----------------------------------------------------------------------------------
# Preload libraries -> hardware shim and GL forwarding
#-----------------------------------------------------------------------------------

# Hardware shim (mcu_spi, uinput, mtd, ...) -> mib2emu-shim.c
export LD_PRELOAD=/opt/mib2/libmib2emu.so

if [ "$MIB2_GLFWD" = 1 ] && [ -f /opt/mib2/libglfwd.so ]
then
    export LD_PRELOAD="/opt/mib2/libglfwd.so /opt/mib2/libmib2emu.so"
    export MIB2_GLFWD_SOCK=/tmp/run/emu/glfwd.sock
fi

export MIB2_FAKEROOT=/opt/mib2/fakeroot
export MIB2_MCU_SOCK=/tmp/run/emu/mcu.sock

mkdir -p "$XDG_RUNTIME_DIR" /tmp/run/service/registry /tmp/run/emu && chmod 0700 "$XDG_RUNTIME_DIR"
