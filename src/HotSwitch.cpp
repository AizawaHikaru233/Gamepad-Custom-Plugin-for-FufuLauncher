#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include "MinHook.h"
#include "HotSwitch.h"
#include "DeviceActivity.h"
#include "InputSwitchGuard.h"

namespace {
    // Current native input-manager UI update. Its original finishes walking
    // input handlers before we request a device change, including menu pages.
    constexpr const char* UpdateSignature =
        "56 57 53 48 83 EC 20 48 89 CE 80 3D ? ? ? ? 00 0F 85 ? ? ? ? "
        "80 BE ? ? ? ? 00 74 08 48 83 C4 20 5B 5F 5E C3 "
        "80 3D ? ? ? ? 00 0F 85 ? ? ? ? 80 BE ? ? ? ? 00 74 ? "
        "48 89 F1 E8 ? ? ? ? 48 85 C0 74 ? 83 BE ? ? ? ? 00 75 ? "
        "8B 86 ? ? ? ? 83 F8 23 74 ? 83 F8 0B 75 ? B9 1B 00 00 00 E8 ? ? ? ? "
        "84 C0 74 ? 48 8B 05 ? ? ? ? 48 8B B8 ? ? ? ? 48 85 FF";
    // Whole native transaction: page eligibility, preparation, device/layout
    // changes, focus/cursor, menu completion and final device-change event.
    // Unlike a settings-page wrapper, its receiver is the real input manager.
    constexpr const char* SwitchDeviceSignature =
        "55 41 56 56 57 53 48 81 EC C0 00 00 00 48 8D AC 24 80 00 00 00 "
        "0F 29 75 30 48 C7 45 20 FE FF FF FF 45 89 C6 89 D3 48 89 CF "
        "80 3D ? ? ? ? 00 0F 84 ? ? ? ? 80 3D ? ? ? ? 00 "
        "48 8B 05 ? ? ? ? 0F 85 ? ? ? ? 48 8B 88 ? ? ? ? "
        "48 85 C9 0F 84 ? ? ? ? E8 ? ? ? ? 84 C0";
    // Decode native scene/blocking/deadline checks, without reading or changing
    // the saved main-page enableInputHotSwap preference.
    constexpr const char* EligibilitySignature =
        "56 48 83 EC 30 0F 29 74 24 20 48 89 CE 80 3D ? ? ? ? 00 "
        "0F 85 ? ? ? ? 80 3D ? ? ? ? 00 0F 85 ? ? ? ? "
        "80 BE ? ? ? ? 00 0F 84 ? ? ? ? 48 8B 0D ? ? ? ? "
        "80 B9 C7 00 00 00 00 0F 84 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? "
        "84 C0 0F 85 ? ? ? ? 48 8B 05 ? ? ? ? 48 8B 88 ? ? ? ? "
        "48 85 C9 0F 84 ? ? ? ? E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? "
        "80 B8 ? ? ? ? 00 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? "
        "48 83 B8 ? ? ? ? 00 0F 84 ? ? ? ? F3 0F 10 B6 ? ? ? ? "
        "E8 ? ? ? ? 0F 2E F0 0F 96 C0";
    using Update = void (WINAPI*)(void*, void*);
    using SwitchDevice = bool (WINAPI*)(void*, int, int, void*);
    using NativeTime = float (WINAPI*)();
    using BlockingState = bool (WINAPI*)(int, void*);
    enum class SwitchResult { Applied, Rejected, Failed };
    Update g_OriginalUpdate = nullptr;
    SwitchDevice g_SwitchDevice = nullptr;
    SwitchDevice g_OriginalSwitchDevice = nullptr;
    NativeTime g_NativeTime = nullptr;
    BlockingState g_BlockingState = nullptr;
    void* g_UpdateEntry = nullptr;
    void** g_ManagerSlot = nullptr;
    unsigned g_UiManagerOffset = 0, g_InputManagerOffset = 0;
    unsigned g_InputModeOffset = 0, g_UiModeOffset = 0, g_InputBlockedOffset = 0;
    unsigned g_InputSceneGateOffset = 0, g_SwitchDeadlineOffset = 0;
    int g_BlockingReason = 0;
    HotSwitch::Logger g_Log = nullptr;
    bool g_Diagnostics = false;
    unsigned g_RejectionTraces = 0;
    unsigned g_DeferralTraces = 0;
    std::atomic<bool> g_Enabled{ false }, g_Ready{ false };
    bool g_Prepared = false, g_InstallAttempted = false;
    DeviceActivity g_Activity;
    InputSwitchGuard g_SwitchGuard;
    GamepadActivity g_XInputPads;
    HWND g_Window = nullptr;
    bool g_WindowAttempted = false, g_MouseSeen = false, g_RawMouseSeen = false, g_FirstTick = true;
    LPARAM g_MousePosition = 0;
    ULONGLONG g_IgnoreMouseUntil = 0;
    std::atomic<ULONGLONG> g_LastAttemptTime{ 0 };
    std::uint64_t g_Handled = 0;
    thread_local bool g_Updating = false;

    void Log(const char* text) { if (g_Log) g_Log(text); }
    const unsigned char* Relative(const unsigned char* displacement) {
        return displacement + 4 + *reinterpret_cast<const int*>(displacement);
    }
    const unsigned char* CallTarget(const unsigned char* call) {
        return call[0] == 0xE8 ? Relative(call + 1) : nullptr;
    }
    bool Focused() {
        DWORD pid = 0;
        const HWND window = GetForegroundWindow();
        return window && GetWindowThreadProcessId(window, &pid) && pid == GetCurrentProcessId();
    }
    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM detail,
        UINT_PTR id, DWORD_PTR) {
        if (g_Enabled.load(std::memory_order_relaxed)) {
            const auto now = GetTickCount64();
            switch (message) {
            case WM_KILLFOCUS:
                g_Activity.Clear();
                g_MouseSeen = false;
                break;
            case WM_SETFOCUS:
                g_Activity.Clear();
                g_MouseSeen = false;
                g_RawMouseSeen = false;
                g_IgnoreMouseUntil = now + 500;
                break;
            case WM_INPUT: {
                // Observe the game's registration; never register/replace a
                // raw-input target from this DLL. Continue the original message
                // chain so the game reads the packet and performs cleanup.
                RAWINPUT raw{};
                UINT size = sizeof(raw);
                if (GET_RAWINPUT_CODE_WPARAM(key) == RIM_INPUT && Focused() &&
                    GetRawInputData(reinterpret_cast<HRAWINPUT>(detail), RID_INPUT,
                        &raw, &size, sizeof(RAWINPUTHEADER)) == sizeof(raw) &&
                    raw.header.dwType == RIM_TYPEMOUSE) {
                    if (!(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) g_RawMouseSeen = true;
                    if (IsRawMouseActivity(raw.data.mouse))
                        g_Activity.Notify(InputSource::Mouse, now);
                }
                break;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if (!(detail & (1LL << 30)) && Focused())
                    g_Activity.Notify(InputSource::Keyboard, now);
                break;
            case WM_MOUSEMOVE:
                if (!g_RawMouseSeen && g_MouseSeen && detail != g_MousePosition &&
                    now >= g_IgnoreMouseUntil && Focused())
                    g_Activity.Notify(InputSource::Mouse, now);
                g_MouseSeen = true;
                g_MousePosition = detail;
                break;
            case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
            case WM_XBUTTONDOWN: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                if (Focused()) g_Activity.Notify(InputSource::Mouse, now);
                break;
            }
        }
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, WindowProc, id);
            g_Window = nullptr;
            g_Enabled.store(false, std::memory_order_release);
        }
        return DefSubclassProc(window, message, key, detail);
    }
    BOOL CALLBACK FindWindow(HWND window, LPARAM data) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(window)) return TRUE;
        wchar_t name[64]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        if (wcscmp(name, L"UnityWndClass")) return TRUE;
        *reinterpret_cast<HWND*>(data) = window;
        return FALSE;
    }
    bool InstallWindowMonitor() {
        if (g_Window) return true;
        if (g_WindowAttempted) return false;
        g_WindowAttempted = true;
        HWND window = nullptr;
        EnumWindows(FindWindow, reinterpret_cast<LPARAM>(&window));
        // SetWindowSubclass must execute on the window's owning thread.
        if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
            !SetWindowSubclass(window, WindowProc, reinterpret_cast<UINT_PTR>(&g_Activity), 0)) {
            Log("Hot switch window/thread check failed; feature disabled, no cross-thread subclass");
            g_Enabled.store(false, std::memory_order_release);
            return false;
        }
        g_Window = window;
        Log("Hot switch window input monitor installed on game UI thread");
        return true;
    }
    bool ReadManagers(void* instance, void*& ui, int& inputMode, int& uiMode, bool requireActive = false) {
        __try {
            const auto root = static_cast<unsigned char*>(*g_ManagerSlot);
            if (!root || !instance || *reinterpret_cast<void**>(root + g_InputManagerOffset) != instance) return false;
            // The native UI update itself returns immediately when this flag
            // is set. Its post-update hook must honor the same input pause.
            if (requireActive && (*(static_cast<unsigned char*>(instance) + g_InputBlockedOffset) ||
                !*(static_cast<unsigned char*>(instance) + g_InputSceneGateOffset))) return false;
            ui = *reinterpret_cast<void**>(root + g_UiManagerOffset);
            if (!ui) return false;
            inputMode = *reinterpret_cast<int*>(static_cast<unsigned char*>(instance) + g_InputModeOffset);
            uiMode = *reinterpret_cast<int*>(static_cast<unsigned char*>(ui) + g_UiModeOffset);
            return (inputMode == 2 || inputMode == 3) && (uiMode == 1 || uiMode == 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    int ReportException(EXCEPTION_POINTERS* exception) {
        char text[768]{};
        int written = sprintf_s(text, "Hot switch native exception; code=0x%08lX; address=%p",
            exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress);
        // Capture a bounded stack before SEH unwinds the failed native call.
        // Diagnostic only, once on failure; never inspect pages or retry it.
        if (g_Diagnostics && written > 0) {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            void* frames[16]{};
            const auto count = CaptureStackBackTrace(0, ARRAYSIZE(frames), frames, nullptr);
            std::size_t used = static_cast<std::size_t>(written);
            for (unsigned index = 0; index < count && used + 48 < sizeof(text); ++index) {
                const auto address = reinterpret_cast<std::uintptr_t>(frames[index]);
                const bool game = address >= base && address - base < nt->OptionalHeader.SizeOfImage;
                written = sprintf_s(text + used, sizeof(text) - used, "; %s0x%llX",
                    game ? "game+" : "", static_cast<unsigned long long>(game ? address - base : address));
                if (written < 0) break;
                used += static_cast<std::size_t>(written);
            }
        }
        Log(text);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    bool WINAPI HookSwitchDevice(void* instance, int layout, int device, void* methodInfo) {
        g_SwitchGuard.Begin();
        bool result = false;
        __try {
            result = g_OriginalSwitchDevice(instance, layout, device, methodInfo);
            // The game's own automatic switch also starts our debounce period.
            if (result) g_LastAttemptTime.store(GetTickCount64(), std::memory_order_relaxed);
        }
        __finally { g_SwitchGuard.End(); }
        return result;
    }
    bool CanSwitchNow(void* instance, int inputMode, int uiMode) {
        __try {
            const auto notBefore = *reinterpret_cast<const float*>(
                static_cast<const unsigned char*>(instance) + g_SwitchDeadlineOffset);
            return g_SwitchGuard.CanStart(inputMode, uiMode, g_NativeTime(), notBefore) &&
                !g_BlockingState(g_BlockingReason, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    SwitchResult Apply(void* instance, int mode) {
        __try {
            const int uiMode = mode == 3 ? 2 : 1;
            // A normal false return is the native page guard rejecting this
            // request before changing modes. Do not disable or force the UI.
            if (!g_SwitchDevice(instance, uiMode, mode, nullptr)) return SwitchResult::Rejected;
            void* ui = nullptr;
            int actualInput = 0, actualUi = 0;
            return ReadManagers(instance, ui, actualInput, actualUi) && actualInput == mode && actualUi == uiMode
                ? SwitchResult::Applied : SwitchResult::Failed;
        }
        __except (ReportException(GetExceptionInformation())) { return SwitchResult::Failed; }
    }
    void WINAPI HookUpdate(void* instance, void* methodInfo) {
        if (g_Updating || g_SwitchGuard.IsActive()) { g_OriginalUpdate(instance, methodInfo); return; }
        g_Updating = true;
        __try { g_OriginalUpdate(instance, methodInfo); }
        __finally { g_Updating = false; }
        if (!g_Enabled.load(std::memory_order_relaxed) || g_SwitchGuard.IsActive() || !InstallWindowMonitor()) return;
        const auto now = GetTickCount64();
        const auto token = g_Activity.Peek(now);
        if (!token || token == g_Handled) return;
        void* ui = nullptr;
        int current = 0, uiMode = 0;
        if (!ReadManagers(instance, ui, current, uiMode, true)) return;
        if (g_FirstTick) {
            char text[160]{};
            sprintf_s(text, "Hot switch native UI update observed; thread=%lu; input mode=%d; UI mode=%d",
                GetCurrentThreadId(), current, uiMode);
            Log(text);
            g_FirstTick = false;
        }
        const int requested = DeviceActivity::NativeMode(token);
        if (current == requested && uiMode == (requested == 3 ? 2 : 1)) { g_Handled = token; return; }
        const auto lastAttempt = g_LastAttemptTime.load(std::memory_order_relaxed);
        if (now < lastAttempt || now - lastAttempt < 500) return;
        if (!CanSwitchNow(instance, current, uiMode)) {
            if (g_Diagnostics && g_DeferralTraces < 8) {
                ++g_DeferralTraces;
                Log("Hot switch deferred by native transition/deadline/blocking guard; no additional transaction");
            }
            return;
        }
        g_Handled = token;
        if (!Focused()) return;
        // Cursor recentering during the native switch must not count as mouse use.
        g_IgnoreMouseUntil = now + 500;
        g_LastAttemptTime.store(now, std::memory_order_relaxed);
        g_Updating = true;
        const auto result = Apply(instance, requested);
        g_Updating = false;
        if (result == SwitchResult::Rejected) {
            if (g_Diagnostics && g_RejectionTraces < 8) {
                ++g_RejectionTraces;
                Log("Hot switch rejected by native page guard; request consumed, feature remains enabled; no forced retry");
            }
            return;
        }
        if (result == SwitchResult::Failed) {
            Log("Hot switch native call failed or modes mismatched; feature disabled, no repeated retry");
            g_Enabled.store(false, std::memory_order_release);
            return;
        }
        char text[160]{};
        sprintf_s(text, "Hot switch whole transaction applied %d -> %d; source=%u; UI thread=%lu", current, requested,
            static_cast<unsigned>(DeviceActivity::Source(token)), GetCurrentThreadId());
        Log(text);
    }
}

void HotSwitch::Configure(Logger log, bool enabled, bool diagnostics) {
    g_Log = log; g_Enabled.store(enabled); g_Diagnostics = diagnostics;
}
bool HotSwitch::IsEnabled() { return g_Enabled.load(std::memory_order_relaxed); }
bool HotSwitch::IsReady() { return g_Ready.load(std::memory_order_acquire); }
void HotSwitch::ObserveXInput(unsigned index, const XINPUT_GAMEPAD& pad) {
    if (IsEnabled() && g_XInputPads.Sample(index, pad)) NotifyGamepadActivity();
}
void HotSwitch::NotifyGamepadActivity() {
    if (IsEnabled() && Focused()) g_Activity.Notify(InputSource::Gamepad, GetTickCount64());
}
void HotSwitch::DisconnectXInput(unsigned index) { g_XInputPads.Disconnect(index); }

void HotSwitch::Prepare(Resolver resolve) {
    if (!IsEnabled() || g_Prepared) return;
    g_Prepared = true;
    g_UpdateEntry = resolve(UpdateSignature);
    g_SwitchDevice = reinterpret_cast<SwitchDevice>(resolve(SwitchDeviceSignature));
    const auto eligibility = static_cast<const unsigned char*>(resolve(EligibilitySignature));
    if (!g_UpdateEntry || !g_SwitchDevice || !eligibility) {
        Log("Hot switch signature absent/ambiguous; feature disabled, no fallback scan");
        g_Enabled.store(false);
        return;
    }
    // Decode and cross-check the real managers/mode fields once. The nested
    // input setter is inspected, not resolved separately or invoked by us.
    const auto update = static_cast<const unsigned char*>(g_UpdateEntry);
    const auto native = reinterpret_cast<const unsigned char*>(g_SwitchDevice);
    const auto inputSetter = CallTarget(native + 0x3D1);
    g_ManagerSlot = reinterpret_cast<void**>(const_cast<unsigned char*>(Relative(update + 0x75)));
    g_UiManagerOffset = *reinterpret_cast<const unsigned*>(update + 0x7C);
    g_InputModeOffset = *reinterpret_cast<const unsigned*>(update + 0x4D);
    g_InputBlockedOffset = *reinterpret_cast<const unsigned*>(update + 0x19);
    g_InputSceneGateOffset = *reinterpret_cast<const unsigned*>(eligibility + 0x29);
    g_SwitchDeadlineOffset = *reinterpret_cast<const unsigned*>(eligibility + 0xA5);
    g_BlockingReason = *reinterpret_cast<const int*>(eligibility + 0x49);
    g_BlockingState = reinterpret_cast<BlockingState>(const_cast<unsigned char*>(CallTarget(eligibility + 0x4D)));
    g_NativeTime = reinterpret_cast<NativeTime>(const_cast<unsigned char*>(CallTarget(eligibility + 0xA9)));
    if (Relative(native + 0x40) != reinterpret_cast<const unsigned char*>(g_ManagerSlot) ||
        *reinterpret_cast<const unsigned*>(native + 0x4D) != g_UiManagerOffset ||
        native[0x2F6] != 0x39 || native[0x2F7] != 0x86 ||
        native[0x301] != 0x89 || native[0x302] != 0x86 ||
        native[0x4D6] != 0x48 || native[0x4D7] != 0x8B || native[0x4D8] != 0x80 ||
        native[0x4E2] != 0x83 || native[0x4E3] != 0xB8 ||
        *reinterpret_cast<const unsigned*>(native + 0x4E4) != g_InputModeOffset ||
        native[0x3CE] != 0x41 || native[0x3CF] != 0xB0 || native[0x3D0] != 0x01 ||
        !inputSetter || inputSetter[0x1C] != 0x8B || inputSetter[0x1D] != 0x86 ||
        *reinterpret_cast<const unsigned*>(inputSetter + 0x1E) != g_InputModeOffset ||
        eligibility[0x27] != 0x80 || eligibility[0x28] != 0xBE || eligibility[0x48] != 0xB9 ||
        Relative(eligibility + 0x5D) != reinterpret_cast<const unsigned char*>(g_ManagerSlot) ||
        *reinterpret_cast<const unsigned*>(eligibility + 0x96) != g_UiManagerOffset ||
        eligibility[0xA1] != 0xF3 || eligibility[0xA2] != 0x0F ||
        eligibility[0xA3] != 0x10 || eligibility[0xA4] != 0xB6 ||
        !g_BlockingState || !g_NativeTime || CallTarget(native + 0x628) !=
            reinterpret_cast<const unsigned char*>(g_NativeTime)) {
        Log("Hot switch whole-transaction body cross-check failed; feature disabled");
        g_Enabled.store(false);
        return;
    }
    g_InputManagerOffset = *reinterpret_cast<const unsigned*>(native + 0x4D9);
    g_UiModeOffset = *reinterpret_cast<const unsigned*>(native + 0x2F8);
    if (g_UiModeOffset != *reinterpret_cast<const unsigned*>(native + 0x303) ||
        !g_InputBlockedOffset || g_InputBlockedOffset >= 0x1000 ||
        !g_InputSceneGateOffset || g_InputSceneGateOffset >= 0x1000 ||
        !g_SwitchDeadlineOffset || g_SwitchDeadlineOffset >= 0x1000 || g_SwitchDeadlineOffset % sizeof(float) ||
        g_BlockingReason <= 0 ||
        !g_InputModeOffset || g_InputModeOffset >= 0x1000 || g_InputModeOffset % sizeof(int) ||
        !g_UiModeOffset || g_UiModeOffset >= 0x1000 || g_UiModeOffset % sizeof(int) ||
        !g_UiManagerOffset || g_UiManagerOffset >= 0x40000 || g_UiManagerOffset % sizeof(void*) ||
        !g_InputManagerOffset || g_InputManagerOffset >= 0x40000 || g_InputManagerOffset % sizeof(void*)) {
        Log("Hot switch manager/mode field check failed; feature disabled");
        g_Enabled.store(false);
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    char text[256]{};
    sprintf_s(text, "Hot switch entries resolved once; update=0x%llX; whole transaction=0x%llX; no split refresh calls",
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_UpdateEntry) - base),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_SwitchDevice) - base));
    Log(text);
}
void HotSwitch::Install() {
    if (!IsEnabled() || !g_Prepared || g_InstallAttempted) return;
    g_InstallAttempted = true;
    if (MH_CreateHook(reinterpret_cast<void*>(g_SwitchDevice), reinterpret_cast<void*>(&HookSwitchDevice),
        reinterpret_cast<void**>(&g_OriginalSwitchDevice)) != MH_OK ||
        MH_CreateHook(g_UpdateEntry, reinterpret_cast<void*>(&HookUpdate),
            reinterpret_cast<void**>(&g_OriginalUpdate)) != MH_OK ||
        MH_EnableHook(reinterpret_cast<void*>(g_SwitchDevice)) != MH_OK || MH_EnableHook(g_UpdateEntry) != MH_OK) {
        if (g_OriginalUpdate) {
            MH_DisableHook(g_UpdateEntry);
            MH_RemoveHook(g_UpdateEntry);
        }
        if (g_OriginalSwitchDevice) {
            MH_DisableHook(reinterpret_cast<void*>(g_SwitchDevice));
            MH_RemoveHook(reinterpret_cast<void*>(g_SwitchDevice));
        }
        Log("Hot switch transaction/update hooks failed; feature disabled");
        g_Enabled.store(false);
        return;
    }
    g_Ready.store(true, std::memory_order_release);
    Log("Global hot switch trial ready; whole transaction with shared native guard/deadline; no page polling");
}
