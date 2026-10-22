#pragma once
// Receiver side of UPDATE_*: writes incoming firmware chunks to flash. Register with the Endpoint:
//   static UpdateModule updater;  rx.add(updater);
#include "Endpoint.h"

class UpdateModule : public Module
{
public:
    uint8_t subsys() const override { return proto::Subsys::UPDATE; }
    void handle(const proto::Packet &p, Endpoint &link) override;
    void tick() override; // aborts an update that has gone quiet
    bool active() const { return active_; }

private:
    void ack(Endpoint &link, uint8_t op);
    void nack(Endpoint &link, uint8_t op, int32_t err);
    void abortUpdate();

    bool active_ = false;
    uint32_t size_ = 0;
    uint32_t received_ = 0;
    uint32_t lastChunkLen_ = 0;
    uint32_t lastActivityMs_ = 0;
};
