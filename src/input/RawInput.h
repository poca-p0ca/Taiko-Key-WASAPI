#pragma once
#include "core/Common.h"
#include "core/SpscQueue.h"
#include "input/KeyState.h"
#include <future>
#include <thread>
namespace taiko {
using PlayQueue = SpscQueue<PlayRequest, 1024>;
// Posted to the capture window for each new key press: wParam is the virtual key used for bindings.
constexpr UINT KeyCapturedMessage = WM_APP + 40;
class RawInput {
    PlayQueue& queue_;
    Counters& counters_;
    std::thread thread_;
    std::atomic<HWND> window_{}, capture_{};
    KeyState state_;
    std::array<uint32_t, 256> keys_{};
    std::array<bool, 256> heldAtEnable_{};
    uint32_t generation_{};
    uint64_t sequence_{};
    bool enabled_{};
    struct Mapping {
        std::array<uint32_t, 256> keys;
        uint32_t generation;
        bool enabled;
    };
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    void run(std::promise<std::string> ready);
    void emit(uint32_t sound, int64_t received);

  public:
    RawInput(PlayQueue& q, Counters& c) : queue_(q), counters_(c) {}
    ~RawInput() {
        stop();
    }
    void start();
    void stop();
    void configure(const std::array<uint32_t, 256>& keys, uint32_t generation, bool enabled);
    void preview(uint32_t sound);
    // Reports key presses to target (nullptr stops). Playback is unaffected.
    void capture(HWND target) {
        capture_.store(target);
    }
};
} // namespace taiko
