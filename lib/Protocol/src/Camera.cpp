// Camera on the receiver. See Camera.h for how main.cpp uses it.
//
// How a frame travels: take a JPEG from the camera, cut it into MAX_BODY pieces and send each as a
// CAMERA FRAME packet {frameId, offset, total}. The transmitter ACKs every piece once it has passed
// it on to the PC. At most WINDOW_CHUNKS pieces are un-ACKed at a time, so however slow the link
// or the PC connection is, nothing overflows. No ACK for a while = drop the frame and move on.
#include "Camera.h"
#include "esp_camera.h"

namespace cam
{
    namespace
    {
        // ---- Pins: the common ESP32-S3 camera board layout (same as the camera test) ----
        // GPIO 12 is a camera data line, so Serial1 TX must not use it.
        constexpr int PIN_PWDN = -1;
        constexpr int PIN_RESET = -1;
        constexpr int PIN_XCLK = 15;
        constexpr int PIN_SIOD = 4;
        constexpr int PIN_SIOC = 5;
        constexpr int PIN_Y9 = 16;
        constexpr int PIN_Y8 = 17;
        constexpr int PIN_Y7 = 18;
        constexpr int PIN_Y6 = 12;
        constexpr int PIN_Y5 = 10;
        constexpr int PIN_Y4 = 8;
        constexpr int PIN_Y3 = 9;
        constexpr int PIN_Y2 = 11;
        constexpr int PIN_VSYNC = 6;
        constexpr int PIN_HREF = 7;
        constexpr int PIN_PCLK = 13;

        // ---- Settings ----
        constexpr uint32_t XCLK_HZ = 10000000;                // 10MHz: reliable over the ribbon extension
        constexpr framesize_t DEFAULT_SIZE = FRAMESIZE_QVGA;  // 320x240
        constexpr int DEFAULT_QUALITY = 20;                   // 10..63, higher = smaller files
        constexpr size_t WINDOW_CHUNKS = 2;                   // pieces in flight before waiting for an ACK
        constexpr unsigned long ACK_TIMEOUT_MS = 1500;        // no ACK for this long = drop the frame
        constexpr int MAX_TIMEOUTS = 3;                       // frames in a row with no ACKs = stop streaming

        // ---- State ----
        HardwareSerial *link = nullptr;
        ReportFn logFn = nullptr;
        bool started = false;

        bool streaming = false;
        uint32_t intervalMs = 0;
        bool snapRequested = false;
        int timeoutsInRow = 0;

        camera_fb_t *fb = nullptr; // frame being sent (nullptr = none)
        uint32_t frameId = 0;
        uint32_t sent = 0;  // bytes of fb sent
        uint32_t acked = 0; // bytes of fb the transmitter has passed on
        unsigned long lastProgress = 0;
        unsigned long lastFrameStart = 0;

#define CAM_LOG(...)              \
    do                            \
    {                             \
        if (logFn)                \
            logFn(__VA_ARGS__);   \
    } while (0)

        const char *sizeName(int32_t size)
        {
            switch (size)
            {
            case proto::camera::SIZE_QQVGA:
                return "160x120";
            case proto::camera::SIZE_VGA:
                return "640x480";
            default:
                return "320x240";
            }
        }

        void dropFrame()
        {
            if (fb)
            {
                esp_camera_fb_return(fb);
                fb = nullptr;
            }
        }

        bool startFrame()
        {
            lastFrameStart = millis(); // set even on failure, so a broken camera isn't retried in a tight loop
            fb = esp_camera_fb_get();
            if (!fb)
            {
                CAM_LOG("[CAMERA] capture failed");
                return false;
            }
            frameId++;
            sent = acked = 0;
            lastProgress = millis();
            return true;
        }

        // Send pieces until the window is full or the whole frame is out.
        void sendChunks()
        {
            while (fb && sent < fb->len && sent - acked < WINDOW_CHUNKS * proto::MAX_BODY)
            {
                uint32_t len = min((uint32_t)proto::MAX_BODY, (uint32_t)(fb->len - sent));

                proto::Packet p;
                p.subsys = proto::Subsys::CAMERA;
                p.opcode = proto::camera::FRAME;
                p.argc = 3;
                p.args[0] = (int32_t)frameId;
                p.args[1] = (int32_t)sent;
                p.args[2] = (int32_t)fb->len;
                p.body = fb->buf + sent;
                p.bodyLen = (uint16_t)len;

                uint8_t buf[proto::MAX_FRAME];
                size_t n = proto::encode(p, buf, sizeof buf);
                if (n == 0)
                {
                    dropFrame();
                    return;
                }
                link->write(buf, n);
                sent += len;
            }
        }

        void onAck(uint32_t id, uint32_t bytes)
        {
            if (!fb || id != frameId || bytes <= acked || bytes > sent)
                return; // old frame, duplicate or nonsense: ignore
            acked = bytes;
            lastProgress = millis();
            timeoutsInRow = 0;
            if (acked == fb->len)
                dropFrame(); // whole frame delivered
        }

        void applySetting(int32_t setting, int32_t value)
        {
            sensor_t *s = esp_camera_sensor_get();
            if (!s)
                return;
            dropFrame(); // a frame in flight was taken with the old setting

            switch (setting)
            {
            case proto::camera::SIZE:
            {
                framesize_t size = value == proto::camera::SIZE_QQVGA ? FRAMESIZE_QQVGA
                                   : value == proto::camera::SIZE_VGA ? FRAMESIZE_VGA
                                                                      : FRAMESIZE_QVGA;
                s->set_framesize(s, size);
                CAM_LOG("[CAMERA] size %s", sizeName(value));
                break;
            }
            case proto::camera::QUALITY:
                value = constrain(value, 10, 63);
                s->set_quality(s, value);
                CAM_LOG("[CAMERA] quality %ld", (long)value);
                break;
            case proto::camera::VFLIP:
                s->set_vflip(s, value ? 1 : 0);
                CAM_LOG("[CAMERA] vflip %d", value ? 1 : 0);
                break;
            case proto::camera::HMIRROR:
                s->set_hmirror(s, value ? 1 : 0);
                CAM_LOG("[CAMERA] mirror %d", value ? 1 : 0);
                break;
            default:
                break;
            }
        }
    }

    bool begin(HardwareSerial &linkSerial, ReportFn report)
    {
        link = &linkSerial;
        logFn = report;

        if (!psramFound())
        {
            CAM_LOG("[CAMERA] no PSRAM, camera disabled");
            return false;
        }

        camera_config_t c = {};
        // The camera clock uses PWM channel 6 / timer 3, clear of the servo channels 0, 2, 4.
        c.ledc_channel = LEDC_CHANNEL_6;
        c.ledc_timer = LEDC_TIMER_3;

        c.pin_pwdn = PIN_PWDN;
        c.pin_reset = PIN_RESET;
        c.pin_xclk = PIN_XCLK;
        c.pin_sccb_sda = PIN_SIOD;
        c.pin_sccb_scl = PIN_SIOC;
        c.pin_d7 = PIN_Y9;
        c.pin_d6 = PIN_Y8;
        c.pin_d5 = PIN_Y7;
        c.pin_d4 = PIN_Y6;
        c.pin_d3 = PIN_Y5;
        c.pin_d2 = PIN_Y4;
        c.pin_d1 = PIN_Y3;
        c.pin_d0 = PIN_Y2;
        c.pin_vsync = PIN_VSYNC;
        c.pin_href = PIN_HREF;
        c.pin_pclk = PIN_PCLK;

        c.xclk_freq_hz = XCLK_HZ;
        c.pixel_format = PIXFORMAT_JPEG;
        c.frame_size = FRAMESIZE_VGA; // buffers are sized at init, so start at the largest size SET allows
        c.jpeg_quality = DEFAULT_QUALITY;
        c.fb_count = 2;
        c.fb_location = CAMERA_FB_IN_PSRAM;
        c.grab_mode = CAMERA_GRAB_LATEST;

        if (esp_camera_init(&c) != ESP_OK)
        {
            CAM_LOG("[CAMERA] did not start (check the ribbon cable)");
            return false;
        }

        sensor_t *s = esp_camera_sensor_get();
        if (s)
            s->set_framesize(s, DEFAULT_SIZE); // then drop to the size we actually stream

        started = true;
        CAM_LOG("[CAMERA] ready");
        return true;
    }

    void handle(const proto::Packet &p)
    {
        if (p.opcode == proto::camera::ACK)
        {
            if (p.argc >= 2)
                onAck((uint32_t)p.args[0], (uint32_t)p.args[1]);
            return;
        }

        if (!started)
        {
            CAM_LOG("[CAMERA] not available (camera did not start)");
            return;
        }

        switch (p.opcode)
        {
        case proto::camera::START:
            streaming = true;
            intervalMs = (p.argc >= 1 && p.args[0] > 0) ? (uint32_t)p.args[0] : 0;
            timeoutsInRow = 0;
            CAM_LOG("[CAMERA] streaming");
            break;

        case proto::camera::STOP:
            streaming = false;
            snapRequested = false;
            dropFrame();
            CAM_LOG("[CAMERA] stopped");
            break;

        case proto::camera::SNAP:
            snapRequested = true;
            timeoutsInRow = 0;
            break;

        case proto::camera::SET:
            if (p.argc >= 2)
                applySetting(p.args[0], p.args[1]);
            break;

        default:
            break;
        }
    }

    void tick(bool paused)
    {
        if (!started)
            return;

        if (paused) // firmware update: keep the link clear
        {
            dropFrame();
            return;
        }

        if (fb) // a frame is on its way
        {
            if (millis() - lastProgress > ACK_TIMEOUT_MS)
            {
                dropFrame();
                if (++timeoutsInRow >= MAX_TIMEOUTS && streaming)
                {
                    streaming = false;
                    CAM_LOG("[CAMERA] stopped: transmitter is not answering");
                }
                return;
            }
            sendChunks();
            return;
        }

        bool due = snapRequested || (streaming && millis() - lastFrameStart >= intervalMs);
        if (due)
        {
            snapRequested = false;
            if (startFrame())
                sendChunks();
        }
    }

    bool ready()
    {
        return started;
    }
}