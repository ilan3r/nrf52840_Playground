"""Copy the latest Blinky UF2 to the nice!nano bootloader on Windows."""

import argparse
import ctypes
import os
import re
import sys
import time
from pathlib import Path

if os.name == "nt":
    import winreg


UF2_PATH = Path(r"C:\repo\nrf52840_Playground\blinky\build_uf2\blinky\zephyr\zephyr.uf2")
BOARD_ID = "nrf52840-nicenano"
DRIVE_REMOVABLE = 2
DRIVE_FIXED = 3
DRIVE_DISCONNECTED_ERRORS = {34, 433, 1167}


def find_bootloader():
    matches = []
    for root in os.listdrives():
        if ctypes.windll.kernel32.GetDriveTypeW(root) not in (DRIVE_REMOVABLE, DRIVE_FIXED):
            continue

        drive = Path(root)
        try:
            info = (drive / "INFO_UF2.TXT").read_text(encoding="ascii", errors="replace")
        except OSError:
            continue

        for line in info.splitlines():
            key, _, value = line.partition(":")
            if key == "Board-ID" and value.strip().lower() == BOARD_ID:
                matches.append(drive)
                break

    if len(matches) > 1:
        raise RuntimeError("More than one nice!nano bootloader is mounted. Disconnect the other board and try again.")
    return matches[0] if matches else None


def port_present(port):
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM") as key:
            for index in range(winreg.QueryInfoKey(key)[1]):
                if winreg.EnumValue(key, index)[1] == port:
                    return True
    except OSError:
        # Windows can change the registry entries while a USB device reconnects.
        pass
    return False


def wait_for_reboot(drive, port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if drive.anchor not in os.listdrives() and port_present(port):
            return True
        time.sleep(0.25)
    return False


def flash(drive, firmware, port):
    print(f"Copying {UF2_PATH.name} to {drive} ...")
    port_was_present = port_present(port)
    started_writing = False
    try:
        with (drive / UF2_PATH.name).open("wb") as destination:
            started_writing = True
            destination.write(firmware)
    except OSError as error:
        # The bootloader can reboot before Windows finishes closing the file.
        if (
            started_writing
            and getattr(error, "winerror", None) in DRIVE_DISCONNECTED_ERRORS
            and not port_was_present
            and wait_for_reboot(drive, port)
        ):
            print(f"The bootloader disconnected during the copy and {port} appeared. The board appears to have rebooted into the app.")
            return True
        print(f"Copy failed: {error}")
        return False

    if not port_was_present and wait_for_reboot(drive, port):
        print(f"Copy complete. The app is on {port}.")
    else:
        print(f"Copy complete. If {port} doesn't appear, check which COM port the app was assigned.")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM10", help="expected app serial port (default: COM10)")
    args = parser.parse_args()
    port = args.port.upper()
    if not re.fullmatch(r"COM[1-9][0-9]*", port):
        parser.error("--port must be a Windows COM port, such as COM10")
    if os.name != "nt" or not hasattr(os, "listdrives"):
        parser.error("this script requires Python 3.12 or later on Windows")

    try:
        firmware = UF2_PATH.read_bytes()
        if len(firmware) % 512 or not firmware.startswith(b"UF2\x0a\x57\x51\x5d\x9e"):
            raise ValueError(f"{UF2_PATH} is not a complete UF2 file")
        while True:
            drive = find_bootloader()
            if drive is None:
                print("The nice!nano bootloader drive is not mounted.")
            elif flash(drive, firmware, port):
                return 0
            input("Double-tap reset (briefly bridge RST to GND twice), then press Enter to retry. ")
            time.sleep(1)
    except (KeyboardInterrupt, EOFError):
        print("\nCancelled.")
        return 130
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Flash error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
