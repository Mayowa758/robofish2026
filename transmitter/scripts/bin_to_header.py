
from pathlib import Path

source = Path("../reciever/.pio/build/esp32dev/firmware.bin")
output = Path("include/receiver_firmware.h")

data = source.read_bytes()

output.parent.mkdir(parents=True, exist_ok=True)

with output.open("w") as f:
    f.write("#pragma once\n")
    f.write("#include <stdint.h>\n\n")
    f.write(f"const uint8_t receiverFirmware[] = {{\n")

    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        f.write("    ")
        f.write(", ".join(f"0x{b:02X}" for b in chunk))
        f.write(",\n")

    f.write("};\n")
    f.write(f"const uint32_t receiverFirmwareSize = {len(data)};\n")

print(f"Generated {output}: {len(data)} bytes")
