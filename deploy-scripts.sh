#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# deploy-scripts.sh -> copy the files to WSL andto chroot (/opt/mib2).
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

SRC="${1:-$(cd "$(dirname "$0")" && pwd)}"
B=/home/ubuntu/mib2emu

SOURCE_FILE=$B/scripts/.source-folder

if [ ! -d "$SRC" ]
then
    echo -e "\e[0;31m [Error]: folder not found: $SRC \e[97m"
    exit 1
fi

mkdir -p $B/scripts $B/sysroot/opt/mib2

# all files that belong to the emulator
files() { find "$SRC" -type f \( -name '*.sh' -o -name '*.c' -o -name '*.h' -o -name '*.ini' -o -name '*.py' -o -name '*.conf' \) "$@"; }

duplicates=$(files -printf '%f\n' | sort | uniq -d)

if [ -n "$duplicates" ]
then
    echo -e "\e[0;31m [Error]: duplicate file names: $duplicates \e[97m" >&2
    exit 1
fi

#-----------------------------------------------------------------------------------
# Copy to WSL (Windows line endings removed) and to chroot
#-----------------------------------------------------------------------------------

echo -e "\e[92m [Info]: copy files from $SRC to $B/scripts... \e[97m"
files -exec cp {} $B/scripts/ \;
find $B/scripts -maxdepth 1 -type f -exec sed -i 's/\r$//' {} +
chmod +x $B/scripts/*.sh

echo -e '\e[92m [Info]: copy scripts into the chroot (/opt/mib2)... \e[97m'
cp $B/scripts/*.ini $B/sysroot/opt/mib2/
cp $B/scripts/*.sh $B/sysroot/opt/mib2/
cp $B/scripts/*.py $B/sysroot/opt/mib2/ 2>/dev/null

echo "$SRC" > $SOURCE_FILE

echo -e "\e[92m [Info]: deploy finished ($(files | wc -l) files) \e[97m"
true

