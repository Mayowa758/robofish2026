
from pathlib import Path
import subprocess
import time
import sys

import serial

# Project folders, relative to this script
ROOT = Path(__file__).resolve().parent
RECEIVER_DIR = ROOT / "reciever"
TRANSMITTER_DIR = ROOT / "transmitter"

# Update these to match your PlatformIO environments.
# Leave as None if each project has only one environment.
RECEIVER_ENV = "esp32dev"
TRANSMITTER_ENV = "nodemcu-32s"

TRANSMITTER_PORT = "/dev/cu.usbserial-0001"
BAUD_RATE = 115200


def run_command(command):
    print("\n>", " ".join(map(str, command)))
    subprocess.run(command, check=True)


def build_project(project_dir, environment):
    command = [
        "python3", "-m", "platformio",
        "run", "-d", str(project_dir)
    ]
    if environment:
        command += ["-e", environment]
    run_command(command)


def find_firmware(project_dir, environment):
    build_dir = project_dir / ".pio" / "build"

    if environment:
        firmware = build_dir / environment / "firmware.bin"
        if not firmware.exists():
            raise FileNotFoundError(
                f"Firmware not found: {firmware}"
            )
        return firmware

    candidates = list(build_dir.glob("*/firmware.bin"))

    if len(candidates) != 1:
        raise RuntimeError(
            "Expected one firmware.bin, but found "
            f"{len(candidates)}. Set the environment "
            "variable in this script explicitly.\n"
            + "\n".join(map(str, candidates))
        )

    return candidates[0]


def generate_header(firmware_path, header_path):
    data = firmware_path.read_bytes()

    header_path.parent.mkdir(parents=True, exist_ok=True)

    with header_path.open("w") as file:
        file.write("#pragma once\n")
        file.write("#include <Arduino.h>\n\n")
        file.write("const uint8_t receiverFirmware[] = {\n")

        for start in range(0, len(data), 16):
            chunk = data[start:start + 16]
            file.write(
                "    "
                + ", ".join(f"0x{byte:02X}" for byte in chunk)
                + ",\n"
            )

        file.write("};\n\n")
        file.write(
            "const uint32_t receiverFirmwareSize = "
            "sizeof(receiverFirmware);\n"
        )

    print(f"Generated {header_path}")
    print(f"Receiver firmware size: {len(data)} bytes")


def trigger_receiver_update():
    print("\nOpening transmitter serial port...")

    # Opening the port may reset the ESP32.
    with serial.Serial(
        TRANSMITTER_PORT,
        BAUD_RATE,
        timeout=0.25,
        write_timeout=3
    ) as port:
        time.sleep(3)
        port.reset_input_buffer()

        print("Sending update command 'u'...")
        port.write(b"u")
        port.flush()

        deadline = time.time() + 180
        output = ""

        while time.time() < deadline:
            line = port.readline()

            if line:
                text = line.decode("utf-8", errors="replace").strip()
                if text:
                    print(text)
                    output += text + "\n"

                    if "Update successful" in output:
                        print("\nReceiver update completed.")
                        return

                    if "Update failed" in output:
                        raise RuntimeError(
                            "Transmitter reported update failure."
                        )

        raise TimeoutError(
            "Timed out waiting for update confirmation. "
            "Check the serial logs and receiver."
        )


def main():
    if not RECEIVER_DIR.exists() or not TRANSMITTER_DIR.exists():
        raise FileNotFoundError(
            "Expected receiver/ and transmitter/ folders "
            "next to this script."
        )

    # 1. Build the receiver
    build_project(RECEIVER_DIR, RECEIVER_ENV)

    # 2. Generate the embedded receiver firmware header
    receiver_bin = find_firmware(RECEIVER_DIR, RECEIVER_ENV)
    header = TRANSMITTER_DIR / "include" / "receiver_firmware.h"
    generate_header(receiver_bin, header)

    # 3. Build the transmitter with the new header
    build_project(TRANSMITTER_DIR, TRANSMITTER_ENV)

    # 4. Upload the transmitter via USB
    command = [
    "python3", "-m", "platformio",
    "run", "-d", str(TRANSMITTER_DIR)
    ]
    if TRANSMITTER_ENV:
        command += ["-e", TRANSMITTER_ENV]
    command += ["-t", "upload"]
    run_command(command)

    print("\nTransmitter uploaded successfully.")
    print("Receiver firmware is ready to update.")
    print("Open the transmitter Serial Monitor and enter 'u' to begin.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"\nERROR: {error}", file=sys.stderr)
        sys.exit(1)
