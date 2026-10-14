#pragma once
#include <Windows.h>
#include <Xinput.h>

namespace DualSenseBackend {
    using Logger = void (*)(const char*);
    using Resolver = void* (*)(const char*);
    using Wanted = bool (*)();
    using Observed = void (*)();
    using Process = bool (*)(void* device, XINPUT_GAMEPAD& pad);
    using Disconnect = void (*)(void* device);
    void Configure(Logger log, Wanted wanted, Observed observed, Process process, Disconnect disconnect);
    void Prepare(Resolver resolve);
    void Install(); // Worker only. Input observation also wakes native feature setup.
}
