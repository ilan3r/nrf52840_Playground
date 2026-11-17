"""Copy a built application's UF2 to the nice!nano bootloader on Windows."""

import argparse
import ctypes
import os
import re
import sys
import time
from pathlib import Path

if os.name == "nt":
    import winreg


REPO_ROOT = Path(r"C:\repo\nrf52840_Playground")
UF2_FILENAME = "zephyr.uf2"
BOARD_ID = "nrf52840-nicenano"
DRIVE_REMOVABLE = 2
DRIVE_FIXED = 3
DRIVE_DISCONNECTED_ERRORS = {34, 433, 1167}


def find_uf2(app, build_dir):
    app = (REPO_ROOT / app).resolve()
    build = app / build_dir
    candidates = (
        build / app.name / "zephyr" / UF2_FILENAME,
        build / "zephyr" / UF2_FILENAME,
    )
    matches = [path for path in candidates if path.is_file()]
    if len(matches) == 1:
        return matches[0]
    if matches:
        raise ValueError(f"Found both a Sysbuild and a standalone UF2 under {build}. Use --build-dir to select a build directory with a single output.")
    raise FileNotFoundError(f"No {UF2_FILENAME} found under {build}. Build the application with a UF2 board target or specify its build directory with --build-dir.")


def choose_uf2(build_dir):
    apps = {}
    for folder, subdirs, files in os.walk(REPO_ROOT):
        if "CMakeCache.txt" in files:
            subdirs.clear()
            continue
        subdirs[:] = [name for name in subdirs if not name.startswith(".")]
        if "CMakeLists.txt" in files:
            app = Path(folder).relative_to(REPO_ROOT)
            try:
                apps[app] = find_uf2(app, build_dir)
            except (FileNotFoundError, ValueError):
                # Only offer applications with one unambiguous UF2 to flash.
                continue

    if not apps:
        raise FileNotFoundError(
            f"No applications with a single {UF2_FILENAME} under {build_dir} "
            f"were found in {REPO_ROOT}. Build an app first or use --app and --build-dir."
        )

    print("Select an application to flash:")
    choices = {}
    for number, app in enumerate(sorted(apps), start=1):
        choices[str(number)] = apps[app]
        print(f"  {number}. {app}")

    while True:
        choice = input("Application number (or q to quit): ").strip().lower()
        if choice == "q":
            raise KeyboardInterrupt
        if choice in choices:
            return choices[choice]
        print("Enter one of the listed numbers.")


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
    print(f"Copying {UF2_FILENAME} to {drive} ...")
    port_was_present = port_present(port)
    started_writing = False
    try:
        with (drive / UF2_FILENAME).open("wb") as destination:
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
    parser.add_argument("--app", type=Path,
                        help="application folder, absolute or relative to the repo root (omit to choose from a menu)")
    parser.add_argument("--build-dir", type=Path, default=Path("build_uf2"),
                        help="build directory, absolute or relative to the application folder (default: build_uf2)")
    parser.add_argument("--port", default="COM10", help="expected app serial port (default: COM10)")
    args = parser.parse_args()
    port = args.port.upper()
    if not re.fullmatch(r"COM[1-9][0-9]*", port):
        parser.error("--port must be a Windows COM port, such as COM10")
    if os.name != "nt" or not hasattr(os, "listdrives"):
        parser.error("this script requires Python 3.12 or later on Windows")
    if args.app is None and args.build_dir.is_absolute():
        parser.error("provide --app when --build-dir points to an absolute path")

    try:
        uf2 = (
            choose_uf2(args.build_dir)
            if args.app is None
            else find_uf2(args.app, args.build_dir)
        )
        firmware = uf2.read_bytes()
        if len(firmware) % 512 or not firmware.startswith(b"UF2\x0a\x57\x51\x5d\x9e"):
            raise ValueError(f"{uf2} is not a complete UF2 file")
        print(f"Using {uf2}")
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
