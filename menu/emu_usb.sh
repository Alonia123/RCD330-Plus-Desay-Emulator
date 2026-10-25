#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
S=$B/scripts

#-----------------------------------------------------------------------------------
# USB menu
#-----------------------------------------------------------------------------------

echo
echo -e "\e[92m Select what do you want to do with the USB stick. \e[97m"
echo
echo -e "\e[92m Select: [folder] [last] [out] [back]\e[97m"

while :
do
  read INPUT_STRING
  case $INPUT_STRING in
	folder)
			echo -e '\e[92m [Info]: enter a Windows folder (e.g. C:\\Users\\<name>\\Music): \e[97m'

			read -r WINDOWS_FOLDER

			wsl_folder=$(wslpath -u "$WINDOWS_FOLDER" 2>/dev/null)

			if [ -n "$wsl_folder" ] && [ -d "$wsl_folder" ]
			then
				$S/usb-stick.sh in "$wsl_folder"
			else
				echo -e "\e[0;31m [Info]: folder not found: $WINDOWS_FOLDER \e[97m"
			fi
echo
echo -e "\e[92m Select: [folder] [last] [out] [back]\e[97m"
echo
		;;
	last)
			$S/usb-stick.sh in
echo
echo -e "\e[92m Select: [folder] [last] [out] [back]\e[97m"
echo
		;;
	out)
			$S/usb-stick.sh out
echo
echo -e "\e[92m Select: [folder] [last] [out] [back]\e[97m"
echo
		;;
	back)
			clear
			echo
			echo -e "\e[92m Select: [start] [stop] [state] [(app)s] [(can) vehicle] [(usb) stick] [shell] [(set)up] [close]\e[97m"
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
