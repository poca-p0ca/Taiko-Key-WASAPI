#pragma once
#include "core/Common.h"
namespace taiko {
class KeyState {
    struct Keyboard {
        uintptr_t device{};
        bool used{};
        std::array<bool, 256> down{};
    };
    std::array<Keyboard, 32> devices_{};

  public:
    void clear() noexcept {
        devices_ = {};
    }
    void remove(uintptr_t device) noexcept {
        for (auto& d : devices_)
            if (d.used && d.device == device)
                d = {};
    }
    // Key state is updated even if the downstream queue rejects this press.
    bool transition(uintptr_t device, uint16_t key, uint16_t flags, bool* full = nullptr) noexcept {
        if (key >= 255)
            return false;
        Keyboard* found = nullptr;
        for (auto& d : devices_)
            if (d.used && d.device == device) {
                found = &d;
                break;
            }
        if (!found)
            for (auto& d : devices_)
                if (!d.used) {
                    d.used = true;
                    d.device = device;
                    found = &d;
                    break;
                }
        if (!found) {
            if (full)
                *full = true;
            return false;
        }
        bool was = found->down[key];
        bool down = (flags & RI_KEY_BREAK) == 0;
        found->down[key] = down;
        return down && !was;
    }
};
} // namespace taiko
