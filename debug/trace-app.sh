#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# trace-app.sh -> record an app with apitrace (egltrace) on its next start
#
#   trace-app.sh on  <package>
#   trace-app.sh off <package>
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
export LD_PRELOAD="/usr/lib/arm-linux-gnueabihf/apitrace/wrappers/egltrace.so \$LD_PRELOAD"
export TRACE_FILE=/tmp/$NAME.trace
exec -a /storage/app/$2/bin/$NAME /storage/app/$2/bin/$NAME.real "\$@"
EOF
    chmod +x $BIN
    echo -e "\e[92m [Info]: apitrace on for $NAME -> /tmp/$NAME.trace (inside the chroot) \e[97m"
    ;;
off)
    [ -f $BIN.real ] && mv -f $BIN.real $BIN
    echo -e "\e[92m [Info]: apitrace off for $NAME \e[97m"
    ;;
esac
