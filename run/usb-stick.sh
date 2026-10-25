#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# usb-stick.sh -> virtual USB stick (FAT32 image)
#
# usb-stick.sh in  [folder]   copy folder (e.g. /mnt/c/Users/<name>/Music) to the stick and plug it in
# usb-stick.sh out            unplug the stick
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
IMG=$B/usbstick.img
MNT=$B/sysroot/media/usbstorage1
FLAG=$B/sysroot/tmp/run/emu/usbstick
MUSIC_RE='\.(mp3|wma|flac|ape)$'

#-----------------------------------------------------------------------------------
# Unplug USB device
#-----------------------------------------------------------------------------------

stick_out() {
    rm -f $FLAG
    sleep 2
    mountpoint -q $MNT && umount -l $MNT
    rmdir $MNT 2>/dev/null
    rm -f $B/sysroot/tmp/usb_fs_type
}

case "$1" in
in)
    if [ -n "$2" ] && [ ! -d "$2" ]
    then
        echo -e "\e[0;31m [Error]: folder not found: $2 \e[97m"
        exit 1
    fi

    if [ -e $FLAG ] || mountpoint -q $MNT
    then
        stick_out
        echo -e '\e[92m [Info]: old stick unplugged... \e[97m'
        sleep 3
    fi

    if [ ! -f $IMG ] || [ -n "$2" ]
    then
        echo -e '\e[92m [Info]: create new stick image (FAT32, 2 GB)... \e[97m'
        rm -f $IMG
        truncate -s 2G $IMG
        mkfs.vfat -F 32 -n MIB2USB $IMG > /dev/null
        mkdir -p /tmp/usbfill && mount -o loop $IMG /tmp/usbfill

        if [ -n "$2" ]
        then
            music_files=$(find "$2" -type f | grep -ciE "$MUSIC_RE")
            echo -e "\e[92m [Info]: copy $(find "$2" -type f | wc -l) files ($music_files music files) from $2... \e[97m"
            cp -r "$2"/. /tmp/usbfill/ || echo -e '\e[0;31m [Warning]: not all files copied (FAT does not allow e.g. : ? * " < > |) \e[97m'
        else
            # create test songs (sine tones with ID3 tags)
            echo -e '\e[92m [Info]: create 3 test tones... \e[97m'
            mkdir -p "/tmp/usbfill/Music"
            for track in 1 2 3
            do
                ffmpeg -loglevel error -f lavfi -i "sine=frequency=$((220 * track)):duration=30" -ac 2 \
                    -metadata title="Testton $track" -metadata artist="MIB2 Emulator" -metadata album="Testalbum" \
                    -metadata track=$track -b:a 128k "/tmp/usbfill/Music/0$track Testton $track.mp3"
            done
        fi

        sync
        umount /tmp/usbfill
    fi

    mountpoint -q $B/sysroot/media || mount -t tmpfs -o mode=755 media $B/sysroot/media

    touch $FLAG
    sleep 4
    mkdir -p $MNT
    mount -o loop,uid=0,gid=0,umask=000 $IMG $MNT
    echo vfat > $B/sysroot/tmp/usb_fs_type

    echo -e "\e[92m [Info]: stick plugged in: $(find $MNT -type f | grep -ciE "$MUSIC_RE") music files \e[97m"
    ;;
out)
    stick_out
    echo -e '\e[92m [Info]: stick unplugged \e[97m'
    ;;
*)
    echo -e "\e[0;31m [Info]: usage: $0 in [folder] | out \e[97m"
    exit 1
    ;;
esac
