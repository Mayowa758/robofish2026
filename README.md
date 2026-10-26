# robofish2026

A **transmitter** (gamepad in, serial terminal out) talks over a UART link to a **receiver** that drives the servos.
Everything that crosses the link follows one protocol, so new features (sensors, camera, ...) can be added without touching the rest.

Specifications

1. Control movement for the fish's servos (tail + two side fins implemented so far)
2. The servos respond to an external (gamepad) controller
3. The transmitter shows what the receiver is doing (on its terminal and in the RoboFish app)
4. The receiver's firmware can be updated over the link
5. The receiver's camera sends pictures over the link to the RoboFish app

## Hardware

|                | Transmitter                      | Receiver                                                                                     |
| -------------- | -------------------------------- | -------------------------------------------------------------------------------------------- |
| Board          | ESP32 (`nodemcu-32s`)            | ESP32-S3 N16R8 (`esp32-s3-devkitc-1`, 16 MB flash, 8 MB PSRAM)                               |
| PlatformIO env | `nodemcu-32s`                    | `esp32dev`                                                                                   |
| Link (UART1)   | RX = GPIO14, TX = GPIO12         | RX = GPIO14, TX = GPIO21                                                                     |
| Extras         | Bluepad32 gamepad over Bluetooth | tail servo GPIO40, left fin GPIO39, right fin GPIO38, LED GPIO2, camera on the ribbon connector |

- The link is 115200 baud, **8N1**, and cross-connected (transmitter TX -> receiver RX and back).
- The receiver's TX is GPIO21, not GPIO12, because GPIO12 is one of the camera's data lines.
- Only 4 wires run between the boards (power, GND, TX, RX), so there are no reset / boot lines. That is why firmware updates are done over the protocol itself.
- The receiver LED blinks every 500 ms while the firmware is running.

## Controls

Hold **triangle** and press **d-pad up** for **SERVO** mode or **d-pad down** for **SENSOR** mode. A mode stays until you change it, and `[MODE] SERVO` / `[MODE] SENSOR` is printed on the transmitter terminal. It starts in SERVO mode. While triangle is held no other commands are sent, so a mode switch never changes the speed.

| Button                     | Function (all modes)                       |
| -------------------------- | ------------------------------------------ |
| L1 / L2                    | left fin to +70 / -70 degrees              |
| R1 / R2                    | right fin to +70 / -70 degrees             |
| D-pad up / down            | tail wave speed to max (20) / min (1)      |
| Square                     | calibrate (fins and speed back to neutral) |
| Triangle + d-pad up / down | switch to SERVO / SENSOR mode              |

In SENSOR mode, **cross** starts the camera and **circle** stops it (`checkCameraButtons()` in `transmitter.cpp`). In SERVO mode cross and circle are free for per-mode functions. There is no sensor code yet. Add per-mode buttons in `sendModeCommands()` in `transmitter.cpp`.

Fin and speed commands use the old controller behaviour: the transmitter re-sends every 200 ms while a button is held, and the receiver eases one smoothing step (5% of the way, at least 1 degree) toward the target on each message. Let go and the fin stays where it is.

## Transmitter terminal

Open the transmitter's serial monitor (115200). You will see:

| Line                                  | Meaning                                                                   |
| ------------------------------------- | ------------------------------------------------------------------------- |
| `[MODE] SERVO` / `[MODE] SENSOR`      | mode changed                                                              |
| `[SERVO] left=5 right=0 speed=1`      | receiver's servo state (sent by the receiver, throttled to 4 per second)  |
| `[SENSOR] ...`                        | receiver's sensor messages (none yet beyond "no sensors implemented yet") |
| `[CAMERA] ...`                        | receiver's camera messages (`ready`, `streaming`, `stopped`, errors)      |
| `[CAM] <frameId> <offset> <total> <data>` | one piece of a camera picture, base64 (read by the app, not meant for people) |
| `[TX] Sent 16384 / 412000 bytes`      | firmware update progress (every 16 KB and at the end)                     |
| `Update successful` / `Update failed` | firmware update result                                                    |

The tags are there so a GUI can split the lines into boxes (servo box, sensor box, update progress, current mode).
Only SERVO, SENSOR and CAMERA messages are sent over the link. Receiver boot and update messages stay on the receiver's own serial. Nothing is sent during a firmware update so the link stays clear for the data.

Send `v` / `x` / `p` in the serial monitor to start / stop the camera or take a single picture.

## Protocol

`[SUBSYS] [OPCODE] {...args} {body}`, implemented in `lib/Protocol` (`Protocol.h` is the single source of truth for ids and opcodes).

| SUBSYS  | OPCODE      | args                         | body           | direction |
| ------- | ----------- | ---------------------------- | -------------- | --------- |
| SERVO   | SET_ANGLE   | fin (LEFT/RIGHT), angle      |                | TX -> RX  |
|         | DELTA_ANGLE | fin, angle                   |                | TX -> RX  |
|         | RESET       |                              |                | TX -> RX  |
|         | SET_SPEED   | speed                        |                | TX -> RX  |
| SENSOR  | GET         | sensor                       |                | TX -> RX  |
|         | DATA        | sensor, values...            |                | RX -> TX  |
| UPDATE  | BEGIN       | size                         |                | TX -> RX  |
|         | CHUNK       | offset                       | firmware bytes | TX -> RX  |
|         | END         |                              |                | TX -> RX  |
|         | ABORT       |                              |                | TX -> RX  |
|         | ACK         | opcode, bytesReceived        |                | RX -> TX  |
|         | NACK        | opcode, error, bytesReceived |                | RX -> TX  |
| MESSAGE | TEXT        |                              | message text   | RX -> TX  |
| CAMERA  | START       | intervalMs (0 = as fast as possible) |        | TX -> RX  |
|         | STOP        |                              |                | TX -> RX  |
|         | SNAP        |                              |                | TX -> RX  |
|         | SET         | setting (SIZE/QUALITY/VFLIP/HMIRROR), value |  | TX -> RX  |
|         | FRAME       | frameId, offset, total       | JPEG bytes     | RX -> TX  |
|         | ACK         | frameId, bytesForwarded      |                | TX -> RX  |

Notes:

- Angles are degrees relative to neutral (90). Fins are clamped to -70..+70 (`FIN_MIN` / `FIN_MAX`), speed to 0..20 (`SPEED_MIN` = 1 means no tail wave).
- `BACK` (tail) exists as a reserved fin id but is not implemented. There is no tail trim.
- No sensor is implemented yet. SENSOR GET / DATA are defined but unused. Sensor ids (e.g. `GYRO`) are just ids.
- UPDATE: the receiver ACKs every packet. NACK makes the transmitter resend. If a NACK says the receiver is at a different offset, the transmitter resumes from there. A lost ACK is handled because a repeated chunk is simply ACKed again. Chunks are 512 bytes, up to 5 retries per chunk, and the receiver aborts an update that goes quiet for 15 s.
- During an update the receiver pauses the tail wave and the camera.
- CAMERA: each picture is a JPEG cut into 512-byte FRAME pieces. The transmitter passes each piece on to the PC as a `[CAM]` line and then ACKs it. The receiver keeps at most 2 pieces un-ACKed, so neither link can overflow. No ACK for 1.5 s drops the picture, and 3 dropped pictures in a row stop the stream. A picture with a missing piece is thrown away by the app.
- CAMERA defaults: 320x240, JPEG quality 20, camera clock 10 MHz (20 MHz glitched over the ribbon extension). These are in the Settings block of `Camera.cpp`. The 115200 baud PC connection is the bottleneck: about 1 to 2 pictures per second.

### Wire frame

```
0xA5 | subsys u8 | opcode u8 | argc u8 | bodyLen u16 LE | args (argc x int32 LE) | body | crc16 LE
```

- Max 4 args, max body 512 bytes.
- `crc16` is CRC-16/CCITT-FALSE over everything between the start byte and the CRC.
- Frames are binary safe (firmware goes in `body`). A corrupted frame is dropped, the parser resyncs on the next `0xA5`, and a half-received frame is abandoned after 100 ms.

## Repo layout

```
lib/Protocol/            packet format, CRC, Parser  (Protocol.h / Protocol.cpp)
  test/test_protocol.cpp host unit test (no hardware needed)
reciever/src/receiver.cpp     receiver: servo, sensor and update handling, all plain functions in one file
reciever/src/Camera.h / Camera.cpp   receiver camera: capture, sending pictures in pieces, settings
transmitter/src/transmitter.cpp  gamepad -> packets, receiver messages -> terminal, firmware sender, camera forwarding
transmitter/include/receiver_firmware.h   receiver firmware embedded in the transmitter (generated, see below)
update_receiver.py       rebuild + embed + upload helper for receiver updates
robofish_app.py          the RoboFish app (PySide6): servo, sensor/camera and update boxes, log
camera_feed.py           puts camera pictures back together from the [CAM] lines (no Qt, testable on its own)
snapshots/               pictures saved by the app's Snap button (created on first use)
```

Both boards only need `lib/Protocol` (`lib_extra_dirs = ../lib`). PlatformIO treats a folder under `lib/` as a library only if it has its own `src/` and `include/` directly inside it.

## Build and flash

1. Receiver: `cd reciever && pio run -t upload` (USB). Needed the first time, and whenever the receiver's update code itself changes. Also needed after changing the receiver's PSRAM / flash / partition settings in `platformio.ini` (`memory_type = qio_opi`, `flash_size = 16MB`, `default_16MB.csv`, `-DBOARD_HAS_PSRAM`); the camera needs the PSRAM, and an update over the link can't change these.
2. Transmitter: `cd transmitter && pio run -t upload` (USB; set `upload_port` in `platformio.ini` for your machine). If the build says the app is too big (it carries the receiver firmware), use `board_build.partitions = huge_app.csv`.

## Updating the receiver over the link

1. Run `python3 update_receiver.py` from the project root. It builds the receiver, embeds the resulting `firmware.bin` into `transmitter/include/receiver_firmware.h`, rebuilds the transmitter and uploads it over USB (the port and environments are set at the top of the script).
2. Open the transmitter's serial monitor and send `u`.
3. Watch the progress (`[TX] Sent 16384 / ... bytes`). On success the receiver reboots with the new firmware and the transmitter prints `Update successful`.

## Adding a feature

1. Add an id to `proto::Subsys` (and an opcode namespace) in `Protocol.h`.
2. Receiver: write a `handleXxx(const proto::Packet &p)` function in `receiver.cpp` and add a `case` for it in `handlePacket()`. Use `report(...)` for anything that should show on the transmitter terminal. A bigger feature can live in its own file instead, like `Camera.h` / `Camera.cpp`.
3. Transmitter: send it with `sendPacket(Subsys::XXX, opcode, {args})`. Put mode-specific buttons in `sendModeCommands()`. Print anything received from the receiver in `readLink()`.
4. Add a line to the tables in this README.

## Not done yet

- Sensors (gyro etc.): protocol ids exist, no code.
- Faster camera pictures: the transmitter's 115200 baud PC connection limits the frame rate. Raising it means changing the transmitter, `BAUD_RATE` in the app and `update_receiver.py` together.