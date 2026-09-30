#pragma once
#include "config/KeyConfig.h"
#include <mmreg.h>
#include <vector>
namespace taiko {
struct Sample {
    uint32_t id{}, rate{}, channels{};
    std::vector<float> pcm;
    size_t frames() const {
        return pcm.size() / channels;
    }
};
struct SourceBank {
    std::vector<Sample> sounds;
};
struct SoundBank {
    std::vector<Sample> sounds;
    const Sample* find(uint32_t id) const noexcept {
        for (auto& s : sounds)
            if (s.id == id)
                return &s;
        return nullptr;
    }
};
SourceBank loadSources(const KeyConfig& config);
SoundBank convertBank(const SourceBank& source, uint32_t rate);
struct OutputFormat {
    uint32_t rate{}, channels{}, mask{};
    uint16_t bits{}, validBits{}, blockAlign{};
    bool floating{};
    int left{}, right{};
};
OutputFormat inspectFormat(const WAVEFORMATEX* format);
void encodeOutput(const float* stereo, void* output, uint32_t frames, const OutputFormat& f) noexcept;
} // namespace taiko
