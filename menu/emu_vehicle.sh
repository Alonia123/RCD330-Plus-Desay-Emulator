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
# Vehicle menu
#-----------------------------------------------------------------------------------

echo
echo -e "\e[92m Select which vehicle value do you want to send. \e[97m"
echo
echo -e "\e[92m Select: [ignition-on] [ignition-off] [speed] [language] [coding] [trip] [(set)tings via BAP] [manual] [back]\e[97m"

while :
do
  read INPUT_STRING
  SHOW_MENU=1

	if [ "$INPUT_STRING" != "back" ] && ! pgrep -f "$B/scripts/[m]cu-sim.py" > /dev/null
	then
		case $INPUT_STRING in
			ignition-on|ignition-off|speed|language|coding|trip|set|manual)
				echo -e '\e[0;31m [Info]: emulator is not running -> start it first. \e[97m'
				INPUT_STRING='-'
				;;
		esac
	fi

  case $INPUT_STRING in
	ignition-on)
			#--------------------------ignition on (terminal 15)--------------------------
			$S/mcu-send.sh 7341 02
			echo -e '\e[92m [Info]: 0x7341 [02] -> ignition on \e[97m'
		;;
	ignition-off)
			#--------------------------ignition off -> box switches to radio--------------------------
			$S/mcu-send.sh 7341 00
			echo -e '\e[92m [Info]: 0x7341 [00] -> ignition off \e[97m'
		;;
	speed)
			#--------------------------speed (2 bytes, hi lo)--------------------------
			echo -e '\e[92m [Info]: enter speed value (0-65535, e.g. 50): \e[97m'
			read SPEED_VALUE

			if [[ "$SPEED_VALUE" =~ ^[0-9]+$ ]] && [ "$SPEED_VALUE" -le 65535 ]
			then
				speed_hex=$(printf '%02X %02X' $((SPEED_VALUE >> 8)) $((SPEED_VALUE & 255)))
				$S/mcu-send.sh 7342 $speed_hex
				echo -e "\e[92m [Info]: 0x7342 [$speed_hex] -> speed $SPEED_VALUE \e[97m"
			else
				echo -e '\e[0;31m [Info]: please enter a number between 0 and 65535. \e[97m'
			fi
		;;
	language)
			#--------------------------language--------------------------
			echo -e '\e[92m [Info]: select language: [english] [chinese] \e[97m'
			read LANGUAGE

			case $LANGUAGE in
				english)
					$S/mcu-send.sh 73B9 00
					echo -e '\e[92m [Info]: 0x73B9 [00] -> English \e[97m'
					;;
				chinese)
					$S/mcu-send.sh 73B9 01
					echo -e '\e[92m [Info]: 0x73B9 [01] -> Chinese \e[97m'
					;;
				*)
					echo -e '\e[0;31m [Info]: unknown language, nothing sent. \e[97m'
					;;
			esac
		;;
	coding)
			#--------------------------coding 0x7361 (18 bytes)--------------------------
			echo -e '\e[92m [Info]: coding bytes: \e[97m'
			echo -e '\e[92m         0 BT on | 1 BT sniff | 2 BT phonebook | 3 Baidu CarLife | 4 Android Auto \e[97m'
			echo -e '\e[92m         5 Apple CarPlay | 6 MirrorLink | 7-9 MirrorLink country/RBG/speed limit | 10-17 = 00 \e[97m'
			echo -e '\e[92m [Info]: enter 18 bytes hex (e.g. 01 00 00 01 01 01 01 00 00 00 00 00 00 00 00 00 00 00): \e[97m'
			read CODING_BYTES

			byte_count=$(echo $CODING_BYTES | wc -w)

			if [ "$byte_count" -eq 18 ] && echo "$CODING_BYTES" | grep -qE '^([0-9A-Fa-f]{2} *)+$'
			then
				$S/mcu-send.sh 7361 $CODING_BYTES
				echo -e '\e[92m [Info]: coding sent. \e[97m'
				echo -e '\e[0;31m [Info]: App-Connect shows new logos after it is opened again (apps -> app). \e[97m'
			else
				echo -e "\e[0;31m [Info]: $byte_count bytes entered, 18 hex bytes needed -> nothing sent. \e[97m"
			fi
		;;
	trip)
			#--------------------------trip computer (BAP screen 28)--------------------------
			echo -e '\e[92m [Info]: request trip computer (BAP view 28), please wait... \e[97m'
			$S/bap-view.sh 28 0
		;;
	set)
			#--------------------------system settings like from the car (BAP view 0 + app 41)--------------------------
			$S/menu.sh settings
		;;
	manual)
			#--------------------------manual message--------------------------
			echo -e '\e[92m [Info]: enter command and data bytes hex (e.g. 7380 00 01): \e[97m'
			read MANUAL_MESSAGE

			if [ -n "$MANUAL_MESSAGE" ]
			then
				$S/mcu-send.sh $MANUAL_MESSAGE
				echo -e "\e[92m [Info]: sent: $MANUAL_MESSAGE \e[97m"
			fi
		;;
	back)
			clear
			echo
			echo -e "\e[92m Select: [start] [stop] [state] [(app)s] [(can) vehicle] [(usb) stick] [shell] [(set)up] [close]\e[97m"
			break
		;;
	-)
		;;
	*)
			echo -e "\e[92m Wrong Input\e[97m"
			sleep 1
			echo -e '\e[92m please try it again. \e[97m'
			sleep 1
			SHOW_MENU=0
		;;
   esac

	if [ "$SHOW_MENU" = 1 ]
	then
echo
echo -e "\e[92m Select: [ignition-on] [ignition-off] [speed] [language] [coding] [trip] [(set)tings via BAP] [manual] [back]\e[97m"
echo
	fi
  done
  sleep 1
