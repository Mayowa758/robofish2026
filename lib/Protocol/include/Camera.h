#pragma once
// Camera on the receiver: plain functions in a namespace (no classes), same style as main.cpp.
//
// main.cpp only needs four hooks:
//   setup():        cam::begin(Serial1, report);          after Serial1.begin()
//   loop():         cam::tick(updateActive);              every loop
//   handlePacket(): case proto::Subsys::CAMERA: cam::handle(p); break;
//   Serial1:        setTxBufferSize(2048) before begin()  so sending a frame never blocks the loop
//
// Everything about the camera (pins, clock, frame size, chunking, flow control) lives in Camera.cpp.
// If the camera fails to start, the fish works as before and camera commands just report that.
#include <Arduino.h>
#include "Protocol.h"

namespace cam
{
    // main.cpp's report(): prints locally and sends the text to the transmitter.
    using ReportFn = void (*)(const char *fmt, ...);

    // Start the camera. Returns false if it did not start (no PSRAM, or the camera did not answer).
    bool begin(HardwareSerial &link, ReportFn report);

    // A Subsys::CAMERA packet from the transmitter (START / STOP / SNAP / SET / ACK).
    void handle(const proto::Packet &p);

    // Call every loop. paused = true while a firmware update runs: the camera sends nothing.
    void tick(bool paused);

    // true once begin() succeeded.
    bool ready();
}