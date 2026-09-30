#include "audio/VoiceMixer.h"
#include <algorithm>
namespace taiko {
void VoiceMixer::trigger(const Sample* s) noexcept {
    if (!s || s->pcm.empty())
        return;
    Voice* slot = nullptr;
    for (auto& v : voices_)
        if (!v.sound) {
            slot = &v;
            break;
        }
    if (!slot) {
        slot = &*std::min_element(voices_.begin(), voices_.end(),
                                  [](auto& a, auto& b) { return a.age < b.age; });
        ++stolen_;
    }
    *slot = {s, 0, nextAge_++};
}
bool VoiceMixer::mix(float* stereo, uint32_t frames, float volume) noexcept {
    std::fill_n(stereo, size_t(frames) * 2, 0.f);
    bool audible = false;
    for (auto& v : voices_)
        if (v.sound) {
            size_t count = std::min(size_t(frames), v.sound->frames() - v.frame);
            for (size_t i = 0; i < count * 2; ++i)
                stereo[i] += v.sound->pcm[v.frame * 2 + i];
            v.frame += count;
            if (v.frame == v.sound->frames())
                v.sound = nullptr;
        }
    for (size_t i = 0; i < size_t(frames) * 2; ++i) {
        stereo[i] = std::clamp(stereo[i] * volume, -1.f, 1.f);
        audible |= stereo[i] != 0;
    }
    return audible;
}
} // namespace taiko
