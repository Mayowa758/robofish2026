#pragma once
#include "Protocol.h"

void handleUpdate(const proto::Packet &p); // call for every packet whose subsys is UPDATE
void tickUpdate();                         // call every loop(): aborts an update that has gone quiet
bool updateIsActive();                     // true while a firmware update is in progress