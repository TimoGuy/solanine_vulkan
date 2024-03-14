#pragma once

#include "pch.h"


namespace comim
{
    void init();
    void cleanup();
    void simulationTick();

    int32_t getCurrentBeat();

    void hurtRequest(const std::string& guid, void (*onSuccess)(), void (*onGotParried)());
}
