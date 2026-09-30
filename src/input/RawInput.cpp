#include "input/RawInput.h"
namespace taiko {
static constexpr UINT SetMapping = WM_APP + 1, Preview = WM_APP + 2, Quit = WM_APP + 3;
void RawInput::start() {
    std::promise<std::string> p;
    auto ready = p.get_future();
    thread_ = std::thread(&RawInput::run, this, std::move(p));
    auto error = ready.get();
    if (!error.empty()) {
        thread_.join();
        throw std::runtime_error(error);
    }
}
void RawInput::stop() {
    if (thread_.joinable()) {
        if (window_)
            SendMessageW(window_, Quit, 0, 0);
        thread_.join();
        window_ = nullptr;
    }
}
void RawInput::configure(const std::array<uint32_t, 256>& keys, uint32_t generation, bool enabled) {
    Mapping m{keys, generation, enabled};
    if (window_)
        SendMessageW(window_, SetMapping, 0, reinterpret_cast<LPARAM>(&m));
}
void RawInput::preview(uint32_t sound) {
    if (window_)
        PostMessageW(window_, Preview, sound, 0);
}
void RawInput::emit(uint32_t sound, int64_t received) {
    if (enabled_ && sound && !queue_.push({sound, generation_, received, ++sequence_}))
        counters_.overflow.fetch_add(1, std::memory_order_relaxed);
}
LRESULT CALLBACK RawInput::procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<RawInput*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<RawInput*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(w, msg, wp, lp);
    if (msg == SetMapping) {
        auto& m = *reinterpret_cast<Mapping*>(lp);
        self->enabled_ = false;
        self->keys_ = m.keys;
        self->generation_ = m.generation;
        self->state_.clear();
        // Sent control messages may overtake queued input. Discard messages that
        // belong to the stopped stream before enabling a new generation.
        MSG old{};
        while (PeekMessageW(&old, w, WM_INPUT, WM_INPUT, PM_REMOVE))
            DefWindowProcW(w, old.message, old.wParam, old.lParam);
        while (PeekMessageW(&old, w, Preview, Preview, PM_REMOVE)) {
        }
        self->heldAtEnable_.fill(false);
        if (m.enabled)
            for (int key = 0; key < 256; ++key)
                self->heldAtEnable_[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
        self->enabled_ = m.enabled;
        return 0;
    }
    if (msg == Preview) {
        self->emit(static_cast<uint32_t>(wp), qpc());
        return 0;
    }
    if (msg == Quit) {
        self->enabled_ = false;
        DestroyWindow(w);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_INPUT_DEVICE_CHANGE && wp == GIDC_REMOVAL) {
        self->state_.remove(static_cast<uintptr_t>(lp));
        self->heldAtEnable_.fill(false);
    }
    if (msg == WM_INPUT) {
        const int64_t received = qpc();
        alignas(RAWINPUT) std::array<std::byte, sizeof(RAWINPUT)> storage{};
        UINT bytes = static_cast<UINT>(storage.size());
        auto count = GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, storage.data(), &bytes,
                                     sizeof(RAWINPUTHEADER));
        if (count != UINT(-1) && count >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) {
            auto& raw = *reinterpret_cast<const RAWINPUT*>(storage.data());
            if (raw.header.dwType == RIM_TYPEKEYBOARD && raw.data.keyboard.VKey < 255) {
                auto& k = raw.data.keyboard;
                USHORT key = k.VKey, physical = key;
                if (key == VK_SHIFT)
                    physical = static_cast<USHORT>(MapVirtualKeyW(k.MakeCode, MAPVK_VSC_TO_VK_EX));
                if (key == VK_CONTROL)
                    physical = (k.Flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
                if (key == VK_MENU)
                    physical = (k.Flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
                bool full = false;
                if (physical >= 255)
                    return DefWindowProcW(w, msg, wp, lp);
                if (k.Flags & RI_KEY_BREAK)
                    self->heldAtEnable_[physical] = false;
                if (self->state_.transition(reinterpret_cast<uintptr_t>(raw.header.hDevice), physical,
                                            k.Flags, &full) &&
                    !self->heldAtEnable_[physical]) {
                    auto sound = self->keys_[physical];
                    if (!sound)
                        sound = self->keys_[key];
                    self->emit(sound, received);
                }
                if (full)
                    self->counters_.inputDeviceOverflow.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Required cleanup for foreground WM_INPUT; no WM_KEYDOWN playback path.
        return DefWindowProcW(w, msg, wp, lp);
    }
    return DefWindowProcW(w, msg, wp, lp);
}
void RawInput::run(std::promise<std::string> ready) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    WNDCLASSW cls{};
    cls.lpfnWndProc = procedure;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"TaikoRawInput";
    RegisterClassW(&cls);
    window_ =
        CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, this);
    RAWINPUTDEVICE device{1, 6, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window_};
    if (!window_ || !RegisterRawInputDevices(&device, 1, sizeof(device))) {
        if (window_)
            DestroyWindow(window_);
        window_ = nullptr;
        ready.set_value("Raw Input registration failed");
        return;
    }
    ready.set_value({});
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = nullptr;
    RegisterRawInputDevices(&device, 1, sizeof(device));
}
} // namespace taiko
