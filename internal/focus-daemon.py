#!/usr/bin/env python3
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# focus-daemon.py -> focus service for mouse/touch control
#

import os
import re
import subprocess
import time

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# Constants
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

B = "/home/ubuntu/mib2emu"
LOG = f"{B}/sysroot/tmp/log/appmgr.log"
SHOW = re.compile(r"SVPiLMMgr::ShowApp\(\) - app_id: (\d+), surface_id: (\d+), show: ([01])")
IGNORE = {1001, 5004}
REFRESH = 15

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# Functions
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

def setFocus(surface):
    result = subprocess.run([f"{B}/scripts/chroot.sh", "/opt/mib2/focus.sh", str(surface)],
                            capture_output=True, text=True)
    print(f"{time.strftime('%H:%M:%S')} Info: focus -> surface {surface}: {result.stdout.strip()}", flush=True)
    return "succeeded" in result.stdout


def followFile(path):
    file, inode = None, None

    while True:
        try:
            state = os.stat(path)
            if file is None or state.st_ino != inode:
                file, inode = open(path, "r", errors="replace"), state.st_ino
        except FileNotFoundError:
            time.sleep(1)
            yield None
            continue

        line = file.readline()

        if not line:
            time.sleep(0.3)

        yield line or None

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# main function
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

def main():
    visible = []
    focused = None
    retryAt, retries = 0.0, 0
    refreshAt = time.time() + REFRESH

    for line in followFile(LOG):
        match = SHOW.search(line) if line else None

        if match:
            surface, show = int(match.group(2)), match.group(3) == "1"

            if surface not in IGNORE:
                if surface in visible:
                    visible.remove(surface)
                if show:
                    visible.append(surface)

                focused = None
                retryAt, retries = 0.0, 0

        # refresh
        if time.time() >= refreshAt:
            focused = None
            refreshAt = time.time() + REFRESH

        target = visible[-1] if visible else None

        if target is not None and target != focused and retries < 10 and time.time() >= retryAt:
            if setFocus(target):
                focused = target
            else:
                retries += 1
                retryAt = time.time() + 2


if __name__ == "__main__":
    main()

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
