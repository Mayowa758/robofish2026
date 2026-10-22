# robofish2026
repo for robofish

Specifications

1. We need to control movement for 4 servos
2. Also need to code these servos so that they can respond to an external controller

## Protocol

`[SUBSYS] [OPCODE] {...args} {body}` — implemented in `lib/Protocol` (`Protocol.h` is the single source of truth for ids/opcodes).

| SUBSYS | OPCODE | args | body |
|---|---|---|---|
| SERVO | SET_ANGLE | fin (LEFT/RIGHT/BACK), angle | |
| | DELTA_ANGLE | fin, angle | |
| | RESET | | |
| | SET_SPEED | speed | |
| SENSOR | GET | sensor (GYRO) | |
| | DATA (reply) | sensor, values... | |
| UPDATE | BEGIN | size | |
| | CHUNK | offset | firmware bytes |
| | END / ABORT | | |
| | ACK (reply) | opcode, bytesReceived | |
| | NACK (reply) | opcode, error, bytesReceived | |

Wire frame: `0xA5 | subsys | opcode | argc | bodyLen(u16) | args (int32 LE each) | body | crc16`.

## Layout

- `lib/Protocol` – packet format, parser, `Endpoint` (UART link + dispatch) and the `Module` interface. Host test: `lib/Protocol/test`.
- `lib/ReceiverUpdater`, `lib/TransmitterUpdater` – UPDATE subsystem (pure software, shared via lib).
- `reciever/src` – receiver: `servo_module` (SERVO), `sensor_module` (SENSOR), `receiver.cpp` registers modules.
- `transmitter/src/transmitter.cpp` – gamepad -> packets, `u` in the serial monitor starts a receiver update.

## Adding a feature
1. Add an id to `proto::Subsys` and an opcode namespace in `Protocol.h`.
2. Write a `Module` subclass (`subsys()`, `handle()`, optional `tick()`).
3. `link.add(yourModule)` in the receiver (or transmitter) `setup()`.
