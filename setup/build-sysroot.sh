#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************

set -e

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
ROOT=$B/sysroot
MIRROR=http://deb.debian.org/debian

KEYRING=$B/debian-bookworm-keyring.gpg
KEY_URLS="https://ftp-master.debian.org/keys/release-12.asc
https://ftp-master.debian.org/keys/archive-key-12.asc"

PACKAGES="adduser apitrace apitrace-tracers apt base-files base-passwd bash bsdutils busybox bzip2
ca-certificates coreutils dash debconf debian-archive-keyring debianutils diffutils dpkg e2fsprogs
file findutils gcc-12-base gdbserver gpgv grep gzip hostname init-system-helpers krb5-locales less
libacl1 libapt-pkg6.0 libattr1 libaudit-common libaudit1 libblkid1 libbrotli1 libbsd0 libbz2-1.0
libc-bin libc6 libcap-ng0 libcap2 libcom-err2 libcrypt1 libdb5.3 libdbus-1-3 libdebconfclient0
libdeflate0 libdrm-amdgpu1 libdrm-common libdrm-nouveau2 libdrm-radeon1 libdrm2 libedit2 libegl-dev
libegl-mesa0 libegl1 libelf1 libevdev2 libexpat1 libext2fs2 libffi-dev libffi8
libfile-find-rule-perl libfreetype6 libfribidi0 libgbm1 libgcc-s1 libgcrypt20 libgdbm-compat4
libgdbm6 libgl-dev libgl1 libgl1-mesa-dri libglapi-mesa libgles-dev libgles1 libgles2 libglib2.0-0
libglvnd0 libglx-dev libglx-mesa0 libglx0 libgmp10 libgnutls30 libgomp1 libgpg-error0
libgraphite2-3 libgssapi-krb5-2 libgudev-1.0-0 libharfbuzz0b libhogweed6 libicu72 libidn2-0
libimagequant0 libinput-bin libinput10 libjbig0 libjpeg62-turbo libk5crypto3 libkeyutils1 libkrb5-3
libkrb5support0 liblcms2-2 liblerc4 libllvm15 liblz4-1 liblzma5 libmagic-mgc libmagic1 libmd0
libmount1 libmtdev1 libncursesw6 libnettle8 libnsl2 libnumber-compare-perl libopenjp2-7 libp11-kit0
libpam-modules libpam-modules-bin libpam-runtime libpam0g libpcre2-8-0 libperl5.36 libpng16-16
libproc2-0 libpthread-stubs0-dev libpython3-stdlib libpython3.11-minimal libpython3.11-stdlib
libraqm0 libreadline8 libseccomp2 libselinux1 libsemanage-common libsemanage2 libsensors-config
libsensors5 libsepol2 libsmartcols1 libsqlite3-0 libss2 libssl3 libstdc++6 libsystemd0 libtasn1-6
libtext-glob-perl libtiff6 libtinfo6 libtirpc-common libtirpc3 libudev1 libunistring2 libuuid1
libwacom-common libwacom9 libwaffle-1-0 libwayland-bin libwayland-client0 libwayland-cursor0
libwayland-dev libwayland-egl1 libwayland-server0 libwebp7 libwebpdemux2 libwebpmux3 libx11-6
libx11-data libx11-dev libx11-xcb1 libxau-dev libxau6 libxcb-dri2-0 libxcb-dri3-0 libxcb-glx0
libxcb-present0 libxcb-randr0 libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxcb1 libxcb1-dev
libxdmcp-dev libxdmcp6 libxext6 libxfixes3 libxkbcommon0 libxml2 libxshmfence1 libxxf86vm1
libxxhash0 libz3-4 libzstd1 login logsave mailcap mawk media-types mesa-utils-bin mime-support
mount ncurses-base ncurses-bin openssl passwd perl perl-base perl-modules-5.36 procps psmisc
python3 python3-minimal python3-olefile python3-pil python3.11 python3.11-minimal readline-common
sed strace sysvinit-utils tar tzdata usrmerge util-linux util-linux-extra x11proto-dev xkb-data
xorg-sgml-doctools xtrans-dev xz-utils zlib1g"

#-----------------------------------------------------------------------------------
# Existing sysroot
#-----------------------------------------------------------------------------------

if [ -x $ROOT/bin/bash ]
then
    if [ "$1" != "--force" ]
    then
        echo -e "\e[92m [Info]: sysroot already exists ($ROOT) -> nothing to do (--force creates it again) \e[97m"
        exit 0
    fi

    echo -e '\e[0;31m [Info]: old sysroot is removed! \e[97m'
    $B/scripts/stop-emu.sh > /dev/null 2>&1 || true
    for mountPoint in tmp/wslg tmp/xdg dev sys proc
    do
        umount -l $ROOT/$mountPoint 2>/dev/null || true
    done
    rm -rf $ROOT
fi

#-----------------------------------------------------------------------------------
# 1. Debian base system (minbase), the ARM parts run via qemu-user (binfmt)
#-----------------------------------------------------------------------------------

mkdir -p $B

if [ ! -s $KEYRING ]
then
    echo -e '\e[92m [Info]: download Debian release keys... \e[97m'
    for url in $KEY_URLS
    do
        wget -qO- "$url"
    done | gpg --dearmor > $KEYRING \
        || { rm -f $KEYRING; echo -e '\e[0;31m [Error]: Debian keys could not be downloaded \e[97m'; exit 1; }
fi

echo -e '\e[92m [Info]: debootstrap Debian bookworm armhf (takes a while)... \e[97m'
debootstrap --arch=armhf --variant=minbase --keyring=$KEYRING bookworm $ROOT $MIRROR > $B/debootstrap.log 2>&1 \
    || { echo -e "\e[0;31m [Error]: debootstrap failed, see $B/debootstrap.log \e[97m"; exit 1; }

#-----------------------------------------------------------------------------------
# 2. Packages of the tested setup
#-----------------------------------------------------------------------------------

echo -e '\e[92m [Info]: install packages in the sysroot (takes a while)... \e[97m'
cp /etc/resolv.conf $ROOT/etc/resolv.conf
mountpoint -q $ROOT/proc || mount -t proc proc $ROOT/proc

chroot $ROOT apt-get update > $B/sysroot-apt.log 2>&1
DEBIAN_FRONTEND=noninteractive chroot $ROOT apt-get install -y --no-install-recommends $PACKAGES >> $B/sysroot-apt.log 2>&1 \
    || { umount -l $ROOT/proc; echo -e "\e[0;31m [Error]: package installation failed, see $B/sysroot-apt.log \e[97m"; exit 1; }
chroot $ROOT apt-get clean

umount -l $ROOT/proc

echo -e "\e[92m [Info]: sysroot finished: $ROOT ($(chroot $ROOT dpkg-query -W | wc -l) packages) \e[97m"