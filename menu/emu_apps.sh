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
# Apps menu
#-----------------------------------------------------------------------------------

# app IDs (/svp/etc/app.conf)

echo
echo -e "\e[92m Select which app do you want to open. \e[97m"
echo
echo -e "\e[92m Select: [radio] [media] [settings] [(app)-connect] [phone] [car] [climate] [back]\e[97m"

while :
do
  read INPUT_STRING
  APP_ID=''
  case $INPUT_STRING in
	radio)
			APP_ID=62
		;;
	media)
			APP_ID=151
		;;
	settings)
			APP_ID=41
		;;
	app)
			APP_ID=154
		;;
	phone)
			APP_ID=66
		;;
	car)
			APP_ID=152
		;;
	climate)
			APP_ID=153
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

	#--------------------------open app--------------------------
	if [ -n "$APP_ID" ]
	then
		if pgrep -f "[r]un-emu.sh" > /dev/null
		then
			echo -e "\e[92m [Info]: open app $APP_ID, please wait... \e[97m"
			$S/app-check.sh $APP_ID
		else
			echo -e '\e[0;31m [Info]: emulator is not running -> start it first. \e[97m'
		fi
echo
echo -e "\e[92m Select: [radio] [media] [settings] [(app)-connect] [phone] [car] [climate] [back]\e[97m"
echo
	fi
  done
  sleep 1
