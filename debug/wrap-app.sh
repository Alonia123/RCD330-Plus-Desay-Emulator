#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# wrap-app.sh -> start an app with additional environment
#
#   wrap-app.sh on  <package> "VAR=value VAR2=value" 
#   wrap-app.sh on sv.g5.mib2.setting "WAYLAND_DEBUG=client"
#
#   wrap-app.sh off <package>
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

ROOT=/home/ubuntu/mib2emu/sysroot
PKG=$ROOT/storage/app/$2
NAME=$(sed -n 's:.*<exec>bin/\(.*\)</exec>.*:\1:p' $PKG/manifest.xml)
BIN=$PKG/bin/$NAME

#-----------------------------------------------------------------------------------
# Wrapper on/off
#-----------------------------------------------------------------------------------

case "$1" in
on)
    [ -f $BIN.real ] || mv $BIN $BIN.real
    cat > $BIN <<EOF
#!/bin/bash
export $3
exec -a /storage/app/$2/bin/$NAME /storage/app/$2/bin/$NAME.real "\$@"
EOF
    chmod +x $BIN
    echo -e "\e[92m [Info]: wrapper on for $NAME: $3 \e[97m"
    ;;
off)
    [ -f $BIN.real ] && mv -f $BIN.real $BIN
    echo -e "\e[92m [Info]: wrapper off for $NAME \e[97m"
    ;;
esac
