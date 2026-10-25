# RCD330-Plus-Desay-Emulator

RCD330-Plus Desay emulator for WSL2 (qemu-user chroot, GL forwarding, MCU/CAN simulation)

> This repository does **not** contain any firmware.
> The firmware and the file system dump are
> copyrighted, so you'll need a dump of your own device.

The main goal is firmware development and testing in a controlled environment.
Whether you're experimenting with system services, changing application behavior,
modifying startup scripts or investigating firmware internals,
you can validate your changes before attempting to flash them onto the real head unit.

No car required. No physical head unit required for development. And, most importantly, a much safer place to catch mistakes before they have a chance to brick your device.

***Emulation cannot guarantee that modified firmware will work on real hardware. Differences in timing, hardware behavior and simulated peripherals may still cause problems after flashing. Always keep a known-good firmware image and a recovery plan.***

---

## Contents

- [Requirements](#requirements)
- [Installation](#installation)
- [Getting started](#getting-started)
- [Single commands](#single-commands)
- [CAN Emulation](#can-emulation)
- [Emulate USB-Stick](#emulate-usb-stick)

---

## Requirements

- Windows with **WSL2** and **WSLg**
- **Ubuntu 22.04** in WSL
- A **file system dump** of the head unit (6RD 035 187B_TR7247_71_SW0317_Desay (2020.03.03))

---

## Installation

### The easy way

Open PowerShell and run:

```powershell
wsl -d RCD330-Plus-Desay-Emulator -u root -- bash /mnt/c/<path to repo>/setup/install.sh "C:/<path to dump>"
```

Grab a coffee... this takes roughly 30 to 60 minutes.

If a step fails, the script tells you which one and where to find the log. Fix the problem and
simply run `install.sh` again.

### Step by step

If you prefer to know what's going on, here are the steps `install.sh` runs for you. All scripts
live in `setup/` (in WSL they end up flat in `/home/ubuntu/mib2emu/scripts/`):

| # | Script | What it does |
|---|---|---|
| 1 | `install-packages.sh` | Ubuntu packages: qemu-user, ARM cross compiler, Mesa, sound, USB stick tools |
| 2 | `build-sysroot.sh` | Debian bookworm armhf base system (debootstrap + the package list of the tested setup) |
| 3 | `deploy-scripts.sh` | copies this repository to WSL (run it from the repository folder) |
| 4 | `copy-dump.sh "<dump folder>"` | copies the device dump to WSL |
| 5 | `build-rootfs.sh` | repairs the dump (symlinks, permissions) and builds the runtime root |
| 6 | `patch-emu.sh` | raises the appmgr timeouts from 10 s to 65 s – **needed after every `build-rootfs.sh`** |
| 7 | `build-shim.sh` | builds the hardware shim and the fake device files |
| 8 | `build-glfwd.sh` | builds the GL forwarding (ARM client + PC server) |
| 9 | `setup-audio.sh` | routes the sound to your Windows speaker |

When everything is done, you'll find this in WSL under `/home/ubuntu/mib2emu/`:

| Path | Content |
|---|---|
| `scripts/` | a copy of this repository (flat, see [Making changes](#making-changes)) |
| `dump/` | a copy of the device dump |
| `sysroot/` | the runtime root: Debian bookworm armhf, with /svp and /storage from the dump on top |

---

## Getting started

Open a **PowerShell window** and leave it open.

```powershell
wsl -d RCD330-Plus-Desay-Emulator -u root -- /home/ubuntu/mib2emu/scripts/emu_main.sh
```

Type `start`.

A window pops up, and after a minute or two the radio
screen appears.

| Input | What happens |
|---|---|
| `start` / `stop` | start / stop the emulator |
| `state` | shows the status of the SVP services |
| `app` | sub menu: `radio`, `media`, `settings`, `app` (App-Connect), `phone`, `car` (vehicle menu), `climate` |
| `can` | sub menu: `ignition-on`, `ignition-off`, `speed`, `language` (`english`/`chinese`), `coding` (checks for 18 bytes), `trip` (trip computer), `set` (settings via BAP), `manual` (any message you like) |
| `usb` | sub menu: `folder` (Windows folder is mounted as a virtual USB drive) -> `C:\Users\<name>\Music`, `last` (last stick / test tones), `out` (unplug) |
| `shell` | a shell inside the device system, `exit` takes you back |
| `set` | sub menu: `deploy` (copy to WSL), `rootfs`, `patch`, `shim`, `glfwd`, `audio` one by one (`rootfs` asks first), or `all` |
| `back` / `close` | back to the previous menu / exit the emulator |

---

## Single commands

Everything the menu does can also be done manually. All commands run in **PowerShell**
and execute as root inside WSL:

### Start and stop

```powershell
# Start – in a SEPARATE PowerShell window that stays open
wsl -d Ubuntu -u root -- /home/ubuntu/mib2emu/scripts/run-emu.sh all

# Stop – Ctrl+C in the start window, or from another window:
wsl -d Ubuntu -u root -- /home/ubuntu/mib2emu/scripts/stop-emu.sh
```

```powershell
.../chroot.sh /opt/mib2/status.sh   # SVP service
.../chroot.sh                       # device system shell
```

### Apps

Works like the hard keys on the real unit:

```powershell
.../app-check.sh <ID>
```

| ID | App |
|---|---|
| 62 | Radio |
| 151 | Media |
| 66 | Phone |
| 154 | App |
| 41 | Setup |
| 152 | Car |
| 153 | Climate settings |
| 47 | Update |
| 48 | Service Mode |
| 155 | Engineering Menu |

Based on: `/svp/etc/app.conf`.

### HMI Screenshot

```powershell
cd <repo folder>\run
powershell -ExecutionPolicy Bypass -File screenshot.ps1 -Title "top-level window" -Out C:\temp\mib2.png
```
---

## CAN Emulation

`mcu-send.sh <cmd> <data bytes in hex>`:

```powershell
.../mcu-send.sh 7341 02        # ignition on
.../mcu-send.sh 73B9 00        # language English (01 = Chinese)
```

### Coding

The coding is message `0x7361` with 18 bytes:

| Byte | Meaning |
|---|---|
| 0 | Bluetooth on |
| 1 | BT sniff |
| 2 | BT phone book profile |
| 3 | Baidu CarLife |
| 4 | Android Auto |
| 5 | Apple CarPlay |
| 6 | MirrorLink |
| 7–9 | MirrorLink country / RBG / speed limit |
| 10–17 | 0 |

```powershell
# Turn on CarLife + CarPlay + MirrorLink
.../mcu-send.sh 7361 01 00 00 01 01 01 01 00 00 00 00 00 00 00 00 00 00 00
```
---

## Emulate USB-Stick

```powershell
.../usb-stick.sh in /mnt/c/Users/<name>/Music
.../usb-stick.sh in
.../usb-stick.sh out
```

Give it 30 to 60 seconds to read the stick ("Loading USB").
---