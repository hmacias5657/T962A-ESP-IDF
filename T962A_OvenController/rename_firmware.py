#!/usr/bin/env python3
"""
rename_firmware.py — Post-build firmware versioning script.

Reads the firmware version from Config.h, copies/renames the built .bin
file to include the version and build timestamp.

Usage:
    python3 rename_firmware.py [build_dir]

The script looks for OvenController.bin in the build directory
(default: ./build) and creates a copy named:
    OvenController_v{major}.{minor}.{patch}_{timestamp}.bin
"""

import os
import re
import sys
import shutil
from datetime import datetime


def get_version_from_config(config_path):
    with open(config_path, "r") as f:
        content = f.read()

    major = re.search(r"#define FIRMWARE_VERSION_MAJOR\s+(\d+)", content)
    minor = re.search(r"#define FIRMWARE_VERSION_MINOR\s+(\d+)", content)
    patch = re.search(r"#define FIRMWARE_VERSION_PATCH\s+(\d+)", content)

    if not all([major, minor, patch]):
        print("Error: Could not find version defines in Config.h")
        sys.exit(1)

    return "v{}.{}.{}".format(major.group(1), minor.group(1), patch.group(1))


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    build_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(script_dir, "build")

    source_bin = os.path.join(build_dir, "OvenController.bin")
    if not os.path.exists(source_bin):
        print("Error: {} not found. Build the project first.".format(source_bin))
        sys.exit(1)

    config_path = os.path.join(script_dir, "main", "Config.h")
    version = get_version_from_config(config_path)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    dest_name = "OvenController_{}_{}.bin".format(version, timestamp)
    dest_path = os.path.join(build_dir, dest_name)

    shutil.copy2(source_bin, dest_path)
    print("Firmware renamed: {}".format(dest_name))
    print("  Source: {}".format(source_bin))
    print("  Dest:   {}".format(dest_path))

    latest_link = os.path.join(build_dir, "OvenController_{}.bin".format(version))
    shutil.copy2(source_bin, latest_link)
    print("  Latest: {}".format(latest_link))


if __name__ == "__main__":
    main()
