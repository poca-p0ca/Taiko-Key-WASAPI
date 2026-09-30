#pragma once
#include "audio/SoundBank.h"
namespace taiko {
class VoiceMixer {
    struct Voice {
        const Sample* sound{};
        size_t frame{};
        uint64_t age{};
    };
    std::array<Voice, 128> voices_{};
    uint64_t nextAge_{}, stolen_{};

  public:
    void trigger(const Sample* sample) noexcept;
    bool mix(float* stereo, uint32_t frames, float volume) noexcept;
    uint64_t stolen() const noexcept {
        return stolen_;
    }
};
} // namespace taiko
