#pragma once
#include <Windows.h>
#include <hidusage.h>
#include <hidpi.h>
#include <array>
#include <cstring>
#include "DualSenseState.h"

// Windows HID's decoded data list, not the raw HID report. Each layout comes
// from the driver's button/value capabilities for one report ID.
struct DualSenseHidLayout {
    enum class Kind { Other, Button, Axis, Hat };
    struct Field { Kind kind = Kind::Other; unsigned index = 0; };
    static constexpr USHORT Missing = 0xFFFF;
    std::array<Field, 256> fields{};
    std::array<USHORT, 15> buttons{};
    unsigned buttonMask = 0, axisMask = 0;
    bool hat = false;
    DualSenseHidLayout() { buttons.fill(Missing); }
    bool Ready() const { return (buttonMask & 0x3FFF) == 0x3FFF && axisMask == 0x3F && hat; }
};

struct DualSenseHidState {
    static constexpr ULONG MaximumData = 256;
    DualSenseState state;

    bool Read(const HIDP_DATA* data, ULONG count, const DualSenseHidLayout& layout) {
        if (!data || count > MaximumData || !layout.Ready()) return false;
        state = {};
        unsigned axes = 0;
        bool hat = false;
        for (ULONG i = 0; i < count; ++i) {
            if (data[i].DataIndex >= layout.fields.size()) continue;
            const auto field = layout.fields[data[i].DataIndex];
            switch (field.kind) {
            case DualSenseHidLayout::Kind::Button:
                state.buttons[field.index] = data[i].On != FALSE;
                break;
            case DualSenseHidLayout::Kind::Axis:
                if (data[i].RawValue > 255) return false;
                state.axes[field.index] = static_cast<int>(data[i].RawValue);
                axes |= 1u << field.index;
                break;
            case DualSenseHidLayout::Kind::Hat:
                if (data[i].RawValue > 15) return false;
                state.hat = static_cast<int>(data[i].RawValue);
                hat = true;
                break;
            default: break;
            }
        }
        return axes == 0x3F && hat && state.IsValid();
    }

    bool WriteChanges(HIDP_DATA* data, ULONG& count, ULONG capacity,
        const DualSenseHidLayout& layout, const XINPUT_GAMEPAD& routed) const {
        if (!data || count > MaximumData || count > capacity) return false;
        auto mapped = state;
        mapped.ApplyRoutedPad(routed);
        std::array<HIDP_DATA, MaximumData> output{};
        std::array<bool, 15> present{};
        ULONG used = 0;
        for (ULONG i = 0; i < count; ++i) {
            auto item = data[i];
            if (item.DataIndex < layout.fields.size()) {
                const auto field = layout.fields[item.DataIndex];
                if (field.kind == DualSenseHidLayout::Kind::Button) {
                    present[field.index] = true;
                    if (mapped.buttons[field.index] != state.buttons[field.index]) {
                        if (!mapped.buttons[field.index]) continue; // GetData's pressed-button list.
                        item.On = TRUE;
                    }
                } else if (field.kind == DualSenseHidLayout::Kind::Axis &&
                    mapped.axes[field.index] != state.axes[field.index]) {
                    item.RawValue = static_cast<ULONG>(mapped.axes[field.index]);
                } else if (field.kind == DualSenseHidLayout::Kind::Hat && mapped.hat != state.hat) {
                    item.RawValue = static_cast<ULONG>(mapped.hat);
                }
            }
            output[used++] = item;
        }
        for (unsigned i = 0; i < mapped.buttons.size(); ++i) {
            if (present[i] || !mapped.buttons[i] || mapped.buttons[i] == state.buttons[i]) continue;
            if (layout.buttons[i] == DualSenseHidLayout::Missing || used >= MaximumData || used >= capacity)
                return false; // No partial writes when a carrier cannot fit.
            output[used].DataIndex = layout.buttons[i];
            output[used++].On = TRUE;
        }
        if (used > capacity) return false;
        std::memcpy(data, output.data(), used * sizeof(HIDP_DATA));
        count = used;
        return true;
    }

    // Only changed button, trigger and hat fields. SDK writes these into a
    // private report copy; vendor fields, sticks and touch/gyro data are excluded.
    ULONG ReportChanges(HIDP_DATA* changes, const DualSenseHidLayout& layout,
        const XINPUT_GAMEPAD& routed) const {
        auto mapped = state;
        mapped.ApplyRoutedPad(routed);
        ULONG count = 0;
        for (unsigned index = 0; index < layout.fields.size(); ++index) {
            const auto field = layout.fields[index];
            HIDP_DATA change{};
            change.DataIndex = static_cast<USHORT>(index);
            if (field.kind == DualSenseHidLayout::Kind::Button &&
                mapped.buttons[field.index] != state.buttons[field.index])
                change.On = mapped.buttons[field.index] ? TRUE : FALSE;
            else if (field.kind == DualSenseHidLayout::Kind::Axis &&
                mapped.axes[field.index] != state.axes[field.index])
                change.RawValue = static_cast<ULONG>(mapped.axes[field.index]);
            else if (field.kind == DualSenseHidLayout::Kind::Hat && mapped.hat != state.hat)
                change.RawValue = static_cast<ULONG>(mapped.hat);
            else continue;
            changes[count++] = change;
        }
        return count;
    }
};
