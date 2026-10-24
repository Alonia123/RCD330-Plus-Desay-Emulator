#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# build-rootfs.sh -> build the runtime root of the emulator
#   dump/     : unchanged copy of the device dump
#   sysroot/  : Debian bookworm armhf, with /svp, /storage, /opt/device of the dump on top
#

set -e

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

BASE=/home/ubuntu/mib2emu
DUMP=$BASE/dump
ROOT=$BASE/sysroot
RE=arm-linux-gnueabihf-readelf

#-----------------------------------------------------------------------------------
# 1. Repair the dump
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: set the execute permissions in the dump... \e[97m'

# all ELF files and shell scripts
find "$DUMP" -type f | while read -r file
do
    case "$(head -c4 "$file" 2>/dev/null | od -An -c | tr -d ' ')" in
        177ELF*|'#!'*)
            chmod +x "$file"
            ;;
    esac
done

echo -e '\e[92m [Info]: rebuild library symlinks from SONAME... \e[97m'

# symlinks
for dir in lib usr/lib svp/lib svp/lib/hw svp/lib/gstreamer usr/lib/gstreamer-1.0
do
    [ -d "$DUMP/$dir" ] || continue

    for file in "$DUMP/$dir"/*.so*
    do
        [ -f "$file" ] && [ ! -L "$file" ] || continue

        soname=$($RE -d "$file" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\]/\1/p')
        [ -n "$soname" ] && [ "$soname" != "$(basename "$file")" ] && [ ! -e "$DUMP/$dir/$soname" ] && ln -s "$(basename "$file")" "$DUMP/$dir/$soname"

        plain=$(basename "$file" | sed 's/\.so\..*/.so/')
        [ "$plain" != "$(basename "$file")" ] && [ ! -e "$DUMP/$dir/$plain" ] && ln -s "$(basename "$file")" "$DUMP/$dir/$plain"
    done
done

ln -sf ld-2.15.so "$DUMP/lib/ld-linux-armhf.so.3"

# media plugin is missing in the dump (radio crashes without player)
# (SVP_CreatePlayer/SVP_DestroyPlayer) -> WAV player as replacement
[ -e "$DUMP/svp/lib/libmedia-prompt.so" ] || ln -s libmedia-wav.so.1.0.0 "$DUMP/svp/lib/libmedia-prompt.so"

for name in app-run app-kill app-deploy
do
    ln -sf app-utils "$DUMP/svp/bin/$name"
done

#-----------------------------------------------------------------------------------
# 2. Overlay into the sysroot
#-----------------------------------------------------------------------------------

echo -e '\e[0;31m [Info]: old /svp, /storage and /opt/device in the sysroot are removed! \e[97m'
rm -rf "$ROOT/svp" "$ROOT/storage" "$ROOT/opt/device"

echo -e '\e[92m [Info]: copy /svp and /storage of the dump into the sysroot... \e[97m'
cp -a "$DUMP/svp" "$DUMP/storage" "$ROOT/"

# Remove the shader cache of the device (PowerVR SGX binary code)
rm -f "$ROOT"/storage/data/data/*.cache

echo -e '\e[92m [Info]: copy device libraries to /opt/device... \e[97m'
mkdir -p "$ROOT/opt/device"
cp -a "$DUMP/lib" "$DUMP/usr" "$DUMP/sbin" "$DUMP/etc" "$ROOT/opt/device/"

for file in version_extra build_date profile.d/svp-environment.sh xdg/weston/weston.ini
do
    [ -e "$DUMP/etc/$file" ] && mkdir -p "$ROOT/etc/$(dirname $file)" && cp -a "$DUMP/etc/$file" "$ROOT/etc/$file"
done

#-----------------------------------------------------------------------------------
# 3. Filtered library path
#-----------------------------------------------------------------------------------

#   Contains only device libraries that Debian does NOT bring itself.
#   glibc, libstdc++, EGL/GLES, wayland, drm etc. come from Debian (backwards compatible).

echo -e '\e[92m [Info]: create filtered library path /opt/device/libfilt... \e[97m'

FILT=$ROOT/opt/device/libfilt
rm -rf "$FILT"
mkdir -p "$FILT"

debian_has() {
    ls "$ROOT/lib/arm-linux-gnueabihf/$1" "$ROOT/usr/lib/arm-linux-gnueabihf/$1" >/dev/null 2>&1
}

BLOCK='^(ld-linux|libc\.|libm\.|libpthread|libdl\.|librt\.|libutil|libresolv|libnsl|libcrypt\.|libstdc\+\+|libgcc_s|libEGL|libGLES|libpvr|libPVR|libwayland|libdrm\.|libgbm|libz\.)'

for dir in usr/lib lib usr/lib/pulseaudio
do
    for file in "$ROOT/opt/device/$dir"/*.so*
    do
        name=$(basename "$file")
        echo "$name" | grep -qE "$BLOCK" && continue
        debian_has "$name" && continue

        [ -L "$FILT/$name" ] || ln -s "/opt/device/$dir/$name" "$FILT/$name"
    done
done

# PowerVR Wayland EGL -> Mesa libwayland-egl (identical wl_egl_window_* API)
ln -sf /usr/lib/arm-linux-gnueabihf/libwayland-egl.so.1 "$FILT/libpvr_wlegl.so"
ln -sf /usr/lib/arm-linux-gnueabihf/libwayland-egl.so.1 "$FILT/libpvr_wlegl.so.1"

echo -e "\e[92m [Info]: rootfs finished: $ROOT \e[97m"
echo -e '\e[0;31m [Info]: now run patch-emu.sh! \e[97m'
