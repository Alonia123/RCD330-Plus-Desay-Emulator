#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
DUMP=$B/dump
SOURCE="$1"

# Windows path -> WSL path
case "$SOURCE" in
    [A-Za-z]:*)
        SOURCE=$(wslpath -u "$SOURCE")
        ;;
esac

if [ ! -d "$SOURCE/svp" ] || [ ! -d "$SOURCE/storage" ] || [ ! -d "$SOURCE/lib" ]
then
    echo -e "\e[0;31m [Error]: no device dump in: $1 (svp, storage and lib are needed) \e[97m"
    exit 1
fi

#-----------------------------------------------------------------------------------
# Copy
#-----------------------------------------------------------------------------------

echo -e '\e[0;31m [Info]: old dump in WSL is removed! \e[97m'
rm -rf $DUMP
mkdir -p $DUMP

for folder in etc lib sbin svp storage usr
do
    if [ -d "$SOURCE/$folder" ]
    then
        echo -e "\e[92m [Info]: copy $folder... \e[97m"
        cp -r "$SOURCE/$folder" $DUMP/
    fi
done

echo -e "\e[92m [Info]: dump copied: $DUMP ($(du -sh $DUMP | cut -f1)) \e[97m"
