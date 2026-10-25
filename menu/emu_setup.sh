#!/bin/bash
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************

#-----------------------------------------------------------------------------------
# Variables
#-----------------------------------------------------------------------------------

B=/home/ubuntu/mib2emu
S=$B/scripts

SRC=$(cat $S/.source-folder 2>/dev/null)

#-----------------------------------------------------------------------------------
# Setup menu
#-----------------------------------------------------------------------------------

echo
echo -e "\e[92m Select which setup step do you want to run. \e[97m"
echo
echo -e "\e[92m Select: [deploy] [rootfs] [patch] [shim] [glfwd] [audio] [(all)] [back]\e[97m"

while :
do
  read INPUT_STRING
  case $INPUT_STRING in
	deploy)
			#--------------------------copy the Windows folder to WSL--------------------------
			if [ -n "$SRC" ] && [ -f "$SRC/deploy-scripts.sh" ]
			then
				bash "$SRC/deploy-scripts.sh" "$SRC"
			else
				echo -e '\e[0;31m [Info]: repository folder unknown -> run deploy-scripts.sh once from the repository. \e[97m'
			fi
		;;
	rootfs)
			#--------------------------rootfs (deletes the emulator data!)--------------------------
			echo -e '\e[0;31m [Info]: build-rootfs.sh deletes /svp and /storage in the sysroot \e[97m'
			echo -e '\e[0;31m         -> language, radio presets and app data of the emulator are lost! \e[97m'
			echo -e '\e[0;31m         Continue? [y/n] \e[97m'
			read ANSWER

			if [ "$ANSWER" = "y" ]
			then
				$S/build-rootfs.sh && $S/patch-emu.sh
			fi
		;;
	patch)
			$S/patch-emu.sh
		;;
	shim)
			$S/build-shim.sh
		;;
	glfwd)
			$S/build-glfwd.sh
		;;
	audio)
			$S/setup-audio.sh
		;;
	all)
			#--------------------------all steps in the right order--------------------------
			echo -e '\e[0;31m [Info]: runs rootfs, patch, shim, glfwd, audio -> the emulator data in /storage are lost! \e[97m'
			echo -e '\e[0;31m         Continue? [y/n] \e[97m'
			read ANSWER

			if [ "$ANSWER" = "y" ]
			then
				$S/build-rootfs.sh && $S/patch-emu.sh && $S/build-shim.sh && $S/build-glfwd.sh && $S/setup-audio.sh
			fi
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
			continue
		;;
   esac
echo
echo -e "\e[92m Select: [deploy] [rootfs] [patch] [shim] [glfwd] [audio] [(all)] [back]\e[97m"
echo
  done
  sleep 1
