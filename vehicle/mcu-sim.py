#!/usr/bin/env python3
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# mcu-sim.py -> MCU simulator for /dev/mcu_spi of the G5/MIB2 firmware
#
#   The LD_PRELOAD shim connects /dev/mcu_spi with this SOCK_SEQPACKET server.
#   Every write() message of the firmware arrives here as one packet,
#   every packet sent from here results in exactly one read() on the firmware side.
#
#   Frame format (both directions, from libremote SVPRmtAdapter::SendCommand):
#       [0] (cmd >> 8) & 7   [1] cmd >> 8   [2] cmd & 0xFF   [3] n + 1   [4] 0   [5..] n data bytes
#   devicemgr distributes the packets via bit mask 1 << (cmd_hi >> 3) to the subscribed services.
#
#   Known commands (powermgr SVPRmtPower::PowerAction):
#       MCU->SoC 0x5101 [1] = power off, [2] = power on (UserPowerOn), [6]/[7] = state 7/8
#       MCU->SoC 0x5201 [0] = state request
#       SoC->MCU 0x5401 [state, x] = acknowledge; [3, 0] comes during startup (SoC ready)
#
#   BAP/CAN (canservice SVPRmtCan::MsgDecode, group 0x73):
#       MCU->SoC 0x7380 [viewId, ctrl]  request screen (viewId <= 42, ctrl 1 = show, 0 = close)
#       MCU->SoC 0x7381..0x73CD         ScreenViewOption/Data for single viewIds
#       MCU->SoC 0x73C6 / 0x73C7        time / date
#       MCU->SoC 0x7361 [18 bytes]      coding (BT, CarLife/AA/CarPlay/MirrorLink), see CODING
#       MCU->SoC 0x73B9 [language]      system language (0 = English, 1 = Chinese) -> appmgr ChangeLanguage
#       SoC->MCU 0x7281 [id, value]     setting changed (id 1 = language) -> simulator confirms via 0x73B9
#
# Usage (host, root):
#   mcu-sim.py [--sock PATH] [--log FILE] [--ctl FIFO] [--no-power-on]
#   Commands (stdin or control FIFO): "cmd 5101 02 00 00" -> send command, or raw hex bytes

import argparse
import os
import selectors
import socket
import sys
import time

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# Constants
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

DEFAULT_SOCK = "/home/ubuntu/mib2emu/sysroot/tmp/run/emu/mcu.sock"

# Power
CMD_POWER_ACTION = 0x5101
CMD_POWER_ACK = 0x5401
POWER_ON = 2

# Settings / BAP
CMD_BAP_ITEM_VAL = 0x7281       # SoC->MCU: user selected setting [id, value] (canservice SetBapScreenItemVal)
CMD_LANGUAGE = 0x73B9           # MCU->SoC: set system language (appmgr SVPRmtCan::ChangeLanguage), 0 = English 1 = Chinese
CMD_BAP_VIEW_REQUEST = 0x7380   # MCU->SoC: request screen [viewId, ctrl]
ITEM_LANGUAGE = 1

# MCU->SoC: vehicle coding (canservice SVPRmtCan::MsgDecode 0x7361)
#   [0] BtOnOff [1] BtSniffOnOff [2] BtPbProfile
#   [3] BaiduCarLife [4] GoogleGal (Android Auto) [5] AppleDio (CarPlay) [6] Mirrorlink
#   [7] MirrorlinkCountryCode [8] MirrorlinkRbgPreference [9] MirrorlinkSpeedLimit [10..] 0
CMD_CODING = 0x7361
CODING = bytes([1, 0, 0, 0, 1, 1, 1] + [0] * 11)

# Car menu (carmenu) -> assignment from canservice MsgDecode / carmenu BAPDataMgr
CMD_IGNITION = 0x7341           # MCU->SoC [mode]  0 = off .. 2 = terminal 15 on
CMD_SPEED = 0x7342              # MCU->SoC [v_hi, v_lo]
CMD_CAR_ADAPTION = 0x7367       # MCU->SoC 26 bytes: per byte bits 0..4 = available vehicle functions
                                #   byte 10 bit 0 = trip computer, byte 15/24 = vehicle state (carmenu GetTargetAdaption)
CMD_VIEW_SHOW_REQ = 0x7280      # SoC->MCU [viewId, x]: app asks the head unit to open the screen
CMD_DRIVING_OPTION = 0x738F     # MCU->SoC ViewOption trip computer (view 28), state per entry:
                                #   1 = not available ("---"), 2 = available
CMD_DRIVING_DATA = 0x738C       # MCU->SoC ViewData trip computer (view 28), 25 bytes, unit byte 0 = metric:
                                #   [0] mode 0 = since start 1 = long term 2 = since refuelling
                                #   [1..4] distance u32 BE (0.1 km) [5] unit
                                #   [6..9] driving time u32 BE (min) [10] unit
                                #   [11..12] avg. speed s16 (0.1 km/h) [13] unit
                                #   [14..15] avg. consumption s16 (0.1 l/100km) [16] unit
                                #   [17..18] range s16 (km) [19] unit
                                #   [20..24] state of the climate quick keys

CAR_ADAPTION = bytes([0x1F] * 26)
DRIVING_OPTION_STATE = 2

# mode: (distance, driving time, avg. speed, avg. consumption, range)
DRIVING_DATA = {
    0: (235, 28, 503, 64, 540),
    1: (84230, 6120, 825, 59, 540),
    2: (4120, 310, 797, 61, 540),
}

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# Helper functions -> hex string, build frame, parse frame, trip computer data
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

def toHexString(data):
    return " ".join(f"{byte:02X}" for byte in data)


def buildFrame(cmd, *data):
    cmdHigh, cmdLow = cmd >> 8, cmd & 0xFF
    return bytes([cmdHigh & 7, cmdHigh, cmdLow, len(data) + 1, 0, *data])


def parseFrame(packet):
    # return (cmd, data) -> None if the packet does not match the frame format
    if len(packet) < 5 or packet[0] != packet[1] & 7 or packet[3] != len(packet) - 4:
        return None
    return (packet[1] << 8) | packet[2], packet[5:]


def buildDrivingData(mode):
    distance, drivingTime, avgSpeed, avgConsumption, drivingRange = DRIVING_DATA[mode]
    return bytes([mode, *distance.to_bytes(4, "big"), 0, *drivingTime.to_bytes(4, "big"), 0,
                  *avgSpeed.to_bytes(2, "big"), 0, *avgConsumption.to_bytes(2, "big"), 0,
                  *drivingRange.to_bytes(2, "big"), 0, 0, 0, 0, 0, 0])

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# McuSim -> server, answer rules, delayed messages, commands from stdin/FIFO
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

class McuSim:

    def __init__(self, sockPath, logPath, powerOn=True, ctlPath=None):
        self.powerOn = powerOn
        self.ctlPath = ctlPath
        self.pending = []
        self.lastView = 0
        self.selector = selectors.DefaultSelector()
        self.clients = {}
        self.log = open(logPath, "a", buffering=1) if logPath else None

        #--------------------------SEQPACKET server for the shim--------------------------
        os.makedirs(os.path.dirname(sockPath), exist_ok=True)
        if os.path.exists(sockPath):
            os.unlink(sockPath)
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.server.bind(sockPath)
        os.chmod(sockPath, 0o777)
        self.server.listen(8)
        self.selector.register(self.server, selectors.EVENT_READ, self.accept)

        #--------------------------commands--------------------------
        if sys.stdin.isatty():
            self.selector.register(sys.stdin, selectors.EVENT_READ, self.readStdin)

        if ctlPath:
            if not os.path.exists(ctlPath):
                os.mkfifo(ctlPath, 0o666)
            self.openControlFifo()

        self.printLog(f"Info: MCU simulator listens on {sockPath}")

    def openControlFifo(self):
        self.ctl = os.fdopen(os.open(self.ctlPath, os.O_RDWR | os.O_NONBLOCK), "r")
        self.selector.register(self.ctl, selectors.EVENT_READ, self.readControlFifo)

    def readControlFifo(self, fifo):
        for line in fifo.read().splitlines():
            self.runCommand(line.strip())

    def printLog(self, message):
        line = f"{time.strftime('%H:%M:%S')} {message}"
        print(line, flush=True)
        if self.log:
            self.log.write(line + "\n")

    #--------------------------connections--------------------------

    def accept(self, server):
        client, _ = server.accept()
        self.clients[client.fileno()] = client
        self.selector.register(client, selectors.EVENT_READ, self.receive)
        self.printLog(f"Info: client #{client.fileno()} connected")

    def receive(self, client):
        data = client.recv(4096)

        if not data:
            self.printLog(f"Info: client #{client.fileno()} disconnected")
            self.selector.unregister(client)
            del self.clients[client.fileno()]
            client.close()
            return

        message = parseFrame(data)
        description = f"cmd {message[0]:04X} data [{toHexString(message[1])}]" if message else "(unknown format)"
        self.printLog(f"SoC->MCU #{client.fileno()} [{len(data):3d}] {toHexString(data):30s} {description}")

        if message:
            for reply in self.handleMessage(*message):
                self.send(client, reply)

    #--------------------------answer rules of the simulated MCU--------------------------

    def handleMessage(self, cmd, data):
        if cmd == CMD_POWER_ACK and data[:1] == b"\x03" and self.powerOn:
            self.powerOn = False
            self.printLog("Info: SoC ready -> send power on + vehicle coding")
            self.sendLater(2.0, buildFrame(CMD_CODING, *CODING))
            self.sendLater(2.0, buildFrame(CMD_CAR_ADAPTION, *CAR_ADAPTION))
            self.sendLater(3.0, buildFrame(CMD_IGNITION, 2))
            self.sendLater(3.0, buildFrame(CMD_SPEED, 0, 0))
            return [buildFrame(CMD_POWER_ACTION, POWER_ON, 0, 0)]

        if cmd == CMD_VIEW_SHOW_REQ and len(data) >= 1:
            view = data[0]
            replies = []

            if 28 <= view <= 30:
                mode = view - 28
                replies += [buildFrame(CMD_DRIVING_OPTION, *([DRIVING_OPTION_STATE] * 12)),
                            buildFrame(CMD_DRIVING_DATA, *buildDrivingData(mode))]

            return replies + [buildFrame(CMD_BAP_VIEW_REQUEST, view, 0)]

        if cmd == CMD_BAP_ITEM_VAL and len(data) >= 2 and data[0] == ITEM_LANGUAGE:
            self.printLog(f"Info: head unit: language -> {data[1]}")
            self.sendLater(1.0, buildFrame(CMD_BAP_VIEW_REQUEST, self.lastView, 1))
            return [buildFrame(CMD_LANGUAGE, data[1])]

        return []

    #--------------------------send--------------------------

    def sendLater(self, delay, packet):
        self.pending.append((time.time() + delay, packet))

    def sendToAll(self, packet):
        for client in list(self.clients.values()):
            self.send(client, packet)

    def send(self, client, data):
        message = parseFrame(data)

        if message and message[0] == CMD_BAP_VIEW_REQUEST and len(message[1]) >= 2 and message[1][1] == 1:
            self.lastView = message[1][0]

        self.printLog(f"MCU->SoC #{client.fileno()} [{len(data):3d}] {toHexString(data)}")
        client.send(data)

    #--------------------------commands from stdin or control FIFO--------------------------

    def readStdin(self, stream):
        self.runCommand(stream.readline().strip())

    def runCommand(self, line):
        if not line or line.startswith("#"):
            return

        try:
            if line.startswith("cmd "):
                parts = line.split()
                data = buildFrame(int(parts[1], 16), *bytes.fromhex("".join(parts[2:])))
            else:
                data = bytes.fromhex(line.replace(",", " "))
        except (ValueError, IndexError):
            self.printLog(f"Info: invalid hex input: {line}")
            return

        for client in list(self.clients.values()):
            self.send(client, data)

    #--------------------------main loop--------------------------

    def run(self):
        while True:
            for key, _ in self.selector.select(timeout=0.2):
                key.data(key.fileobj)

            now = time.time()

            for item in [entry for entry in self.pending if entry[0] <= now]:
                self.pending.remove(item)
                self.sendToAll(item[1])

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# main - function
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--sock", default=DEFAULT_SOCK)
    parser.add_argument("--log", default=None)
    parser.add_argument("--no-power-on", action="store_true", help="no automatic power on")
    parser.add_argument("--ctl", default=None, help="control FIFO for commands")
    arguments = parser.parse_args()

    try:
        McuSim(arguments.sock, arguments.log, powerOn=not arguments.no_power_on,
               ctlPath=arguments.ctl).run()
    except KeyboardInterrupt:
        pass

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
