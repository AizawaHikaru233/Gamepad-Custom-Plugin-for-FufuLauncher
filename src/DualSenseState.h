#pragma once
#include <Windows.h>
#include <Xinput.h>
#include <array>

// XInput is only a common value format here; these values are never sent to an
// Xbox API. The game's DualSense HID driver owns the actual device and reports.
constexpr WORD DualSenseTouchpadMask = 0x0400;
struct DualSenseState {
    std::array<bool, 15> buttons{};
    std::array<int, 6> axes{}; // LX, LY, RX, RY, L2, R2, raw range 0..255.
    int hat = 8; // Sony hat nibble: 0..7 clockwise from up, 8..15 neutral.
    static constexpr std::array<WORD, 15> ButtonMasks{{
        XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, 0, 0,
        XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB, 0,
        DualSenseTouchpadMask, 0
    }};
    static constexpr std::array<WORD, 9> Hats{{
        XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT,
        XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_UP, 0
    }};
    bool IsValid() const {
        if (hat < 0 || hat > 15) return false;
        for (const int value : axes) if (value < 0 || value > 255) return false;
        return true;
    }
    static SHORT Stick(int value, bool invert = false) {
        // Both center bytes are neutral in the shared signed input format.
        const int scaled = value < 127 ? (value - 127) * 32767 / 127 :
            value > 128 ? (value - 128) * 32767 / 127 : 0;
        return static_cast<SHORT>(invert ? -scaled : scaled);
    }
    XINPUT_GAMEPAD ToGamepad() const {
        XINPUT_GAMEPAD pad{};
        for (std::size_t index = 0; index < buttons.size(); ++index)
            if (buttons[index]) pad.wButtons |= ButtonMasks[index];
        pad.wButtons |= Hats[hat < 8 ? hat : 8];
        pad.bLeftTrigger = static_cast<BYTE>(axes[4]);
        pad.bRightTrigger = static_cast<BYTE>(axes[5]);
        pad.sThumbLX = Stick(axes[0]); pad.sThumbLY = Stick(axes[1], true);
        pad.sThumbRX = Stick(axes[2]); pad.sThumbRY = Stick(axes[3], true);
        return pad;
    }
    void ApplyRoutedPad(const XINPUT_GAMEPAD& pad) {
        const auto previous = ToGamepad();
        for (std::size_t index = 0; index < buttons.size(); ++index)
            if (ButtonMasks[index]) buttons[index] = (pad.wButtons & ButtonMasks[index]) != 0;
        if (pad.bLeftTrigger != previous.bLeftTrigger) {
            axes[4] = pad.bLeftTrigger;
            if (!pad.bLeftTrigger) buttons[6] = false;
        }
        if (pad.bRightTrigger != previous.bRightTrigger) {
            axes[5] = pad.bRightTrigger;
            if (!pad.bRightTrigger) buttons[7] = false;
        }
        constexpr WORD dpad = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
            XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT;
        if ((pad.wButtons & dpad) != (previous.wButtons & dpad)) {
            for (std::size_t index = 0; index < Hats.size(); ++index)
                if (Hats[index] == (pad.wButtons & dpad)) { hat = static_cast<int>(index); break; }
        }
    }
};
