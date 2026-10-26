// Receiver: servo and sensor are plain functions in this file (no classes of
// our own). SERVO and SENSOR messages are also sent to the transmitter (MESSAGE
// TEXT) so they show on its terminal. UPDATE (firmware over the link) is
// written out in this file too. Only lib/Protocol is needed (packet format).
// Add a subsystem = add a case in handlePacket().
#include <Arduino.h>
#include <Update.h>
#include <stdarg.h>
#include <string.h>

#include "Camera.h"
#include "Protocol.h"

#define RX1_PIN 14
#define TX1_PIN 21
#define LED 2

#define S1_PIN 40  // tail
#define S2_PIN 39  // left fin
#define S3_PIN 38  // right fin

// ============================================================================================
// State
// ============================================================================================
const int s1 = S1_PIN, s2 = S2_PIN,
          s3 = S3_PIN;  // tail, left fin, right fin (servos are just pins now)

int servoChannel(int pin) {
  return pin == S1_PIN ? 0 : pin == S2_PIN ? 2 : 4;
}  // fixed, never shared

void servoAttach(int pin) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(pin, 50, 14, servoChannel(pin));
#else
  ledcSetup(servoChannel(pin), 50, 14);
  ledcAttachPin(pin, servoChannel(pin));
#endif
}

const int NEUTRAL = 90;
const float SENSITIVITY = 0.05f;  // state[t+1] = (1-s)*state[t] + s*target

int leftFin = 0, rightFin = 0;  // degrees relative to neutral
int speedSetting = 1;           // tail wave speed (<=1 = no wave)
int wavePosition = 0, waveDirection = 1;
unsigned long lastWave = 0;
int lastTail = -1, lastLeft = -1,
    lastRight = -1;  // last angle written, to avoid rewriting PWM
unsigned long lastPrint = 0;

proto::Parser parser;  // reads bytes off Serial1 in loop()

bool updateActive = false;
uint32_t updateSize = 0, updateReceived = 0, lastChunkLen = 0;
unsigned long lastUpdateActivity = 0;
const unsigned long UPDATE_IDLE_ABORT_MS = 15000;

const unsigned long BLINK_INTERVAL = 3000;
unsigned long previousBlink = 0;
bool ledState = LOW;

// ============================================================================================
// report(): for SERVO and SENSOR messages. Prints on the receiver's own serial
// AND sends it to the transmitter (MESSAGE TEXT), where it shows up on the
// transmitter terminal (later: the GUI). Everything else (boot, update, errors)
// just uses Serial and stays on the receiver. Not sent during a firmware update
// so the link stays clear for the chunks.
// ============================================================================================
void report(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void report(const char* fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);

  Serial.println(msg);
  if (updateActive) return;

  proto::Packet p;
  p.subsys = proto::Subsys::MESSAGE;
  p.opcode = proto::message::TEXT;
  p.body = (const uint8_t*)msg;
  p.bodyLen = (uint16_t)strlen(msg);

  uint8_t buf[proto::MAX_FRAME];
  size_t n = proto::encode(p, buf, sizeof buf);
  if (n > 0) Serial1.write(buf, n);
}

// ============================================================================================
// Sending replies (receiver -> transmitter)
// ============================================================================================
void sendReply(uint8_t subsys, uint8_t opcode, uint8_t argc, int32_t a0 = 0,
               int32_t a1 = 0, int32_t a2 = 0, int32_t a3 = 0) {
  proto::Packet p;
  p.subsys = subsys;
  p.opcode = opcode;
  p.argc = argc;
  p.args[0] = a0;
  p.args[1] = a1;
  p.args[2] = a2;
  p.args[3] = a3;

  uint8_t buf[proto::MAX_FRAME];
  size_t n = proto::encode(p, buf, sizeof buf);
  if (n > 0) Serial1.write(buf, n);
}

// ============================================================================================
// SERVO
// ============================================================================================
int smoothStep(int current, int target) {
  if (current == target) return current;
  int next = (int)roundf((1 - SENSITIVITY) * current + target * SENSITIVITY);
  if (next == current) next += (target > current) ? 1 : -1;
  return next;
}

// Only touch the PWM when the angle actually changed.
void writeServo(int s, int& last, int angle, bool force) {
  angle = constrain(angle, 0, 180);
  if (force || angle != last) {
    uint32_t duty = (uint32_t)map(angle, 0, 180, 500, 2400) * 16383 / 20000;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(s, duty);
#else
    ledcWrite(servoChannel(s), duty);
#endif
    last = angle;
  }
}

void updateFins(bool force) {
  writeServo(s2, lastLeft, NEUTRAL + leftFin, force);
  writeServo(s3, lastRight, NEUTRAL + rightFin, force);
}

void updateTail(bool force) {
  writeServo(s1, lastTail, NEUTRAL + (speedSetting > 1 ? wavePosition : 0),
             force);
}

void printServoState()  // throttled: the controller re-sends while a button is
                        // held
{
  if (millis() - lastPrint < 250) return;
  lastPrint = millis();
  report("[SERVO] left=%d right=%d speed=%d", leftFin, rightFin, speedSetting);
}

void handleServo(const proto::Packet& p) {
  switch (p.opcode) {
    case proto::servo::SET_ANGLE:
    case proto::servo::DELTA_ANGLE: {
      if (p.argc < 2) return;
      bool delta = (p.opcode == proto::servo::DELTA_ANGLE);
      int fin = p.args[0];
      int angle = p.args[1];

      if (fin == proto::servo::LEFT || fin == proto::servo::RIGHT) {
        int& cur = (fin == proto::servo::LEFT) ? leftFin : rightFin;
        int target = constrain(delta ? cur + angle : angle,
                               proto::servo::FIN_MIN, proto::servo::FIN_MAX);
        cur = delta
                  ? target
                  : smoothStep(
                        cur, target);  // SET_ANGLE eases, one step per message
      } else
        return;  // BACK (tail) is reserved, not implemented
      break;
    }

    case proto::servo::SET_SPEED:
      if (p.argc < 1) return;
      speedSetting = smoothStep(
          speedSetting, constrain((int)p.args[0], 0, proto::servo::SPEED_MAX));
      break;

    case proto::servo::RESET:
      leftFin = rightFin = 0;
      wavePosition = 0;
      speedSetting = 1;
      break;

    default:
      return;
  }

  updateFins(false);
  updateTail(false);
  printServoState();
}

void tickTailWave() {
  if (updateActive ||
      speedSetting <= 1)  // servos stay still during a firmware update
    return;

  int amp = map(speedSetting, 0, 20, 10, 30);
  int speedDelay = map(speedSetting, 0, 20, 20, 5);

  if (millis() - lastWave >= (unsigned long)speedDelay) {
    lastWave = millis();
    wavePosition = constrain(wavePosition + waveDirection, -amp, amp);
    if (wavePosition >= amp) waveDirection = -1;
    if (wavePosition <= -amp) waveDirection = 1;
    updateTail(false);
  }
}

// ============================================================================================
// SENSOR   no sensor code yet. When a sensor is added: GET {sensor} -> reply
// DATA {sensor, values...} and report() its readings so they show on the
// transmitter.
// ============================================================================================
void handleSensor(const proto::Packet& p) {
  report("[SENSOR] no sensors implemented yet (opcode %u)", p.opcode);
}

// ============================================================================================
// UPDATE   firmware over the link
// ============================================================================================
void updateAck(uint8_t op) {
  sendReply(proto::Subsys::UPDATE, proto::update::ACK, 2, op,
            (int32_t)updateReceived);
}

void updateNack(uint8_t op, int32_t err) {
  sendReply(proto::Subsys::UPDATE, proto::update::NACK, 3, op, err,
            (int32_t)updateReceived);
}

void updateAbort() {
  if (updateActive) Update.abort();
  updateActive = false;
  updateSize = updateReceived = lastChunkLen = 0;
}

void handleUpdate(const proto::Packet& p) {
  lastUpdateActivity = millis();

  switch (p.opcode) {
    case proto::update::BEGIN:
      updateAbort();
      if (p.argc < 1 || p.args[0] <= 0) {
        updateNack(p.opcode, proto::update::ERR_BAD_ARGS);
        return;
      }
      updateSize = (uint32_t)p.args[0];
      Serial.printf("[RX] Update request: %lu bytes\n",
                    (unsigned long)updateSize);
      if (!Update.begin(updateSize)) {
        Serial.printf("[RX] Update.begin failed: %d\n", Update.getError());
        updateSize = 0;
        updateNack(p.opcode, proto::update::ERR_BEGIN_FAILED);
        return;
      }
      updateActive = true;
      updateReceived = 0;
      lastChunkLen = 0;
      updateAck(p.opcode);
      return;

    case proto::update::CHUNK: {
      if (!updateActive) {
        updateNack(p.opcode, proto::update::ERR_NOT_ACTIVE);
        return;
      }
      if (p.argc < 1 || p.bodyLen == 0) {
        updateNack(p.opcode, proto::update::ERR_BAD_ARGS);
        return;
      }
      uint32_t offset = (uint32_t)p.args[0];

      // Our ACK got lost and the chunk was resent: just ACK again.
      if (lastChunkLen && offset + lastChunkLen == updateReceived &&
          p.bodyLen == lastChunkLen) {
        updateAck(p.opcode);
        return;
      }
      // Out of order: tell the transmitter where we actually are.
      if (offset != updateReceived || updateReceived + p.bodyLen > updateSize) {
        updateNack(p.opcode, proto::update::ERR_BAD_OFFSET);
        return;
      }
      if (Update.write((uint8_t*)p.body, p.bodyLen) != p.bodyLen) {
        updateAbort();
        Serial.println("[RX] Flash write failed");
        updateNack(p.opcode, proto::update::ERR_WRITE_FAILED);
        return;
      }
      updateReceived += p.bodyLen;
      lastChunkLen = p.bodyLen;
      updateAck(p.opcode);
      return;
    }

    case proto::update::END:
      if (!updateActive) {
        updateNack(p.opcode, proto::update::ERR_NOT_ACTIVE);
        return;
      }
      if (updateReceived != updateSize) {
        updateNack(p.opcode, proto::update::ERR_INCOMPLETE);
        return;
      }
      if (Update.end(true)) {
        Serial.println("[RX] Update successful. Restarting...");
        updateActive = false;
        updateAck(p.opcode);
        Serial1.flush();
        delay(200);
        ESP.restart();
      } else {
        Serial.printf("[RX] Update finalisation failed: %d\n",
                      Update.getError());
        updateAbort();
        updateNack(p.opcode, proto::update::ERR_END_FAILED);
      }
      return;

    case proto::update::ABORT:
      updateAbort();
      updateAck(p.opcode);
      return;

    default:
      return;
  }
}

void tickUpdateTimeout() {
  if (updateActive && millis() - lastUpdateActivity > UPDATE_IDLE_ABORT_MS) {
    updateAbort();
    Serial.println("[RX] Update timed out, aborting");
  }
}

// ============================================================================================
// Dispatch: one packet in, call the right function. New subsystem = new case
// here.
// ============================================================================================
void handlePacket(const proto::Packet& p) {
  switch (p.subsys) {
    case proto::Subsys::SERVO:
      handleServo(p);
      break;
    case proto::Subsys::SENSOR:
      handleSensor(p);
      break;
    case proto::Subsys::UPDATE:
      handleUpdate(p);
      break;
    case proto::Subsys::CAMERA:
      cam::handle(p);
      break;
    default:
      Serial.printf("[RX] unknown subsys %u (opcode %u)\n", p.subsys, p.opcode);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("[RX] boot");

  Serial1.setRxBufferSize(1024);  // a firmware chunk frame is ~540 bytes; must
                                  // be set before begin()
  Serial1.setTxBufferSize(2048);
  Serial1.begin(115200, SERIAL_8N1, RX1_PIN, TX1_PIN);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  servoAttach(s1);
  servoAttach(s2);
  servoAttach(s3);

  cam::begin(Serial1, report);

  updateFins(true);  // all to neutral
  updateTail(true);

  Serial.println("[RX] Ready");
}

void loop() {
  while (Serial1.available() > 0) {
    int b = Serial1.read();
    if (b < 0) break;
    if (parser.feed((uint8_t)b, millis())) handlePacket(parser.packet());
  }

  cam::tick(updateActive);
  tickTailWave();
  tickUpdateTimeout();

  unsigned long now = millis();
  if (now - previousBlink >= BLINK_INTERVAL) {
    previousBlink = now;
    ledState = !ledState;
    digitalWrite(LED, ledState);
  }
}
