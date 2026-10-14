#pragma once
#include <Windows.h>
#include <Xinput.h>

namespace DualSenseHidBackend {
    using Logger = void (*)(const char*);
    using Wanted = bool (*)();
    using Observed = void (*)();
    using Process = bool (*)(void* device, XINPUT_GAMEPAD& pad);
    using Disconnect = void (*)(void* device);
    void Configure(Logger log, Wanted wanted, Observed observed, Process process, Disconnect disconnect);
    void Install(); // Already-loaded hid.dll/user32.dll only; never acquires a device or registers input.
}
