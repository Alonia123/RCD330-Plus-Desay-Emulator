#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
#
# Usage (PowerShell): wsl -d RCD330-Plus-Desay-Emulator -u root -- /home/ubuntu/mib2emu/scripts/emu_main.sh
#

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
S=$B/scripts

#-----------------------------------------------------------------------------------
# Main menu
#-----------------------------------------------------------------------------------

clear
echo
echo -e "\e[92m ------------------------------------------ \e[97m"
echo -e "\e[92m ------- RCD330-Plus Desay Emulator ------- \e[97m"
echo -e "\e[92m ------------------------------------------ \e[97m"
echo
echo -e "\e[92m Select: [start] [stop] [state] [(app)s] [(can) vehicle] [(usb) stick] [shell] [(set)up] [close]\e[97m"

while :
do
  read INPUT_STRING
  case $INPUT_STRING in
	start)
			if pgrep -f "[r]un-emu.sh" > /dev/null
			then
				echo -e '\e[0;31m [Info]: emulator is already running! \e[97m'
			else
				echo -e '\e[92m [Info]: start emulator... \e[97m'
				setsid nohup $S/run-emu.sh all > $B/run.log 2>&1 < /dev/null &
			fi
echo
echo -e "\e[92m Select: [start] [stop] [state] [(app)s] [(can) vehicle] [(usb) stick] [shell] [(set)up] [close]\e[97m"
echo
		;;
	stop)
			bash $S/stop-emu.sh
echo
echo -e "\e[92m Select: [start] [stop] [state] [(app)s] [(can) vehicle] [(usb) stick] [shell] [(set)up] [close]\e[97m"
echo
		;;
	state)
			if pgrep -f "[r]un-emu.sh" > /dev/null
			then
				echo -e '\e[92m [Info]: state of the SVP services: \e[97m'
				$S/chroot.sh /opt/mib2/status.sh
			else
				echo -e '\e[0;31m [Info]: emulator is not running. \e[97m'
			fi
echo
echo -e "\e[92m Select: [start] [stop] [state] [(app)s] [(can) vehicle] [(usb) stick] [shell] [(set)up] [close]\e[97m"
echo
		;;
	app)
			bash $S/emu_apps.sh
		;;
	can)
			bash $S/emu_vehicle.sh
		;;
	usb)
			bash $S/emu_usb.sh
		;;
	shell)
			echo -e '\e[92m [Info]: shell in the device system (/svp, /storage ...), "exit" returns to the menu \e[97m'
			$S/chroot.sh
echo
echo -e "\e[92m Select: [start] [stop] [state] [(app)s] [(can) vehicle] [(usb) stick] [shell] [(set)up] [close]\e[97m"
echo
		;;
	set)
			bash $S/emu_setup.sh
		;;
	close)
			if pgrep -f "[r]un-emu.sh" > /dev/null
			then
				echo -e '\e[0;31m [Info]: the emulator is still running. Stop it? [y/n] \e[97m'
				read ANSWER
				[ "$ANSWER" = "y" ] && bash $S/stop-emu.sh
			fi
			break
		;;
	*)
			echo -e "\e[92m Wrong Input\e[97m"
			sleep 1
			echo -e '\e[92m please try it again. \e[97m'
			sleep 1
		;;
   esac
  done
  sleep 1
