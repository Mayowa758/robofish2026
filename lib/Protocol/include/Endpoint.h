#pragma once
/*
 * Endpoint = one end of the UART link. It owns the parser, dispatches each received packet to the
 * Module registered for its subsystem, and gives modules a way to send replies.
 *
 * Adding a feature = write a Module for its subsystem and call endpoint.add(module).
 */

#include <Arduino.h>
#include <initializer_list>
#include "Protocol.h"

class Endpoint;

class Module
{
public:
    virtual ~Module() {}
    virtual uint8_t subsys() const = 0;                              // which proto::Subsys this handles
    virtual void handle(const proto::Packet &p, Endpoint &link) = 0; // called once per received packet
    virtual void tick() {}                                           // called every Endpoint::poll()
};

class Endpoint
{
public:
    static constexpr size_t MAX_MODULES = 8;

    explicit Endpoint(Stream &link) : link_(link) {}

    bool add(Module &m);
    void poll(); // call every loop(): reads the link, dispatches packets, ticks modules

    bool send(const proto::Packet &p);
    bool send(uint8_t subsys, uint8_t opcode, std::initializer_list<int32_t> args = {},
              const uint8_t *body = nullptr, uint16_t bodyLen = 0);

    Stream &stream() { return link_; }

private:
    Stream &link_;
    proto::Parser parser_;
    Module *modules_[MAX_MODULES] = {nullptr};
    size_t count_ = 0;
};
