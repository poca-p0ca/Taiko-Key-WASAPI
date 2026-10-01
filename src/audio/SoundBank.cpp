#include "audio/SoundBank.h"
#include "audio/Converters.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <ks.h>
#include <ksmedia.h>
#include <limits>

namespace taiko {
static uint32_t u32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) |
           (uint32_t(b[p + 3]) << 24);
}
static uint16_t u16(const std::vector<uint8_t>& b, size_t p) {
    return uint16_t(b[p]) | uint16_t(b[p + 1] << 8);
}
struct RiffLayout {
    size_t fmt{}, fmtSize{}, data{}, dataSize{};
};
// Validates fmt/data and returns where they are. The RIFF size field is advisory:
// some editors write it wrong, append tags after the RIFF body or omit the final
// pad byte, so chunks are checked against the real file size instead.
static RiffLayout validateRiff(const std::vector<uint8_t>& b) {
    if (b.size() < 12 || memcmp(b.data(), "RIFF", 4) || memcmp(b.data() + 8, "WAVE", 4))
        throw std::runtime_error("Expected RIFF/WAVE");
    const size_t end = b.size();
    RiffLayout layout;
    bool fmt = false, data = false;
    uint16_t align = 0;
    for (size_t p = 12; p < end;) {
        if (end - p < 8) {
            if (fmt && data)
                break; // trailing bytes after the last chunk
            throw std::runtime_error("Truncated chunk header");
        }
        uint32_t n = u32(b, p + 4);
        size_t start = p + 8;
        bool isData = !memcmp(b.data() + p, "data", 4);
        // Streaming writers can leave a 0xFFFFFFFF placeholder on the final data chunk.
        if (isData && fmt && !data && n == 0xffffffff)
            n = static_cast<uint32_t>(end - start);
        if (n > end - start) {
            if (fmt && data)
                break; // unrelated trailing data, e.g. tags appended after the RIFF body
            throw std::runtime_error("Truncated chunk/padding");
        }
        if (!memcmp(b.data() + p, "fmt ", 4)) {
            if (fmt || n < 16)
                throw std::runtime_error("Duplicate or short fmt chunk");
            fmt = true;
            auto tag = u16(b, start), ch = u16(b, start + 2), bits = u16(b, start + 14);
            align = u16(b, start + 12);
            auto rate = u32(b, start + 4);
            if (tag == 0xfffe) {
                if (n < 40 || u16(b, start + 16) < 22 || uint32_t(u16(b, start + 16)) + 18 > n)
                    throw std::runtime_error("Invalid extensible fmt");
                uint16_t valid = u16(b, start + 18);
                if (valid == 0 || valid > bits)
                    throw std::runtime_error("Invalid WAV valid bits");
                tag = u16(b, start + 24);
                const uint8_t guidTail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
                if (memcmp(b.data() + start + 26, guidTail, 14))
                    throw std::runtime_error("Unsupported WAV subformat GUID");
                uint32_t mask = u32(b, start + 20);
                if (mask &&
                    mask != (ch == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)))
                    throw std::runtime_error("Unsupported WAV speaker layout");
            }
            if ((ch != 1 && ch != 2) || rate < 8000 || rate > 384000 || (tag != 1 && tag != 3) ||
                (tag == 3 ? (bits != 32 && bits != 64)
                          : (bits != 8 && bits != 16 && bits != 24 && bits != 32)) ||
                align != ch * (bits / 8) || u32(b, start + 8) != rate * align)
                throw std::runtime_error("Unsupported or inconsistent WAV format");
            layout.fmt = start;
            layout.fmtSize = n;
        }
        if (isData) {
            if (data || !n)
                throw std::runtime_error("Duplicate/empty data chunk");
            data = true;
            layout.data = start;
            layout.dataSize = n;
        }
        p = start + n + (n & 1); // may pass end when the final odd chunk lacks its pad byte
    }
    if (!fmt || !data || !align)
        throw std::runtime_error("Missing WAV fmt or data chunk");
    // A partially written final frame cannot be played; drop it.
    layout.dataSize -= layout.dataSize % align;
    if (!layout.dataSize)
        throw std::runtime_error("WAV data shorter than one frame");
    return layout;
}
// dr_wav's non-metadata mode stops at data and trusts the header sizes. Hand it a
// minimal RIFF holding only the validated fmt and data chunks.
static std::vector<uint8_t> canonicalRiff(const std::vector<uint8_t>& b, const RiffLayout& l) {
    std::vector<uint8_t> out;
    out.reserve(28 + l.fmtSize + 1 + l.dataSize);
    auto put = [&](const void* p, size_t n) {
        auto* bytes = static_cast<const uint8_t*>(p);
        out.insert(out.end(), bytes, bytes + n);
    };
    auto put32 = [&](size_t v) {
        uint8_t le[4]{uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        put(le, 4);
    };
    put("RIFF", 4);
    put32(4 + 8 + l.fmtSize + (l.fmtSize & 1) + 8 + l.dataSize + (l.dataSize & 1));
    put("WAVEfmt ", 8);
    put32(l.fmtSize);
    put(b.data() + l.fmt, l.fmtSize);
    if (l.fmtSize & 1)
        out.push_back(0);
    put("data", 4);
    put32(l.dataSize);
    put(b.data() + l.data, l.dataSize);
    if (l.dataSize & 1)
        out.push_back(0);
    return out;
}
SourceBank loadSources(const KeyConfig& config) {
    SourceBank bank;
    size_t total = 0;
    for (const auto& spec : config.sounds)
        try {
            auto size = std::filesystem::file_size(spec.path);
            if (size > 64 * 1024 * 1024)
                throw std::runtime_error("WAV exceeds 64 MiB");
            std::ifstream file(spec.path, std::ios::binary);
            if (!file)
                throw std::runtime_error("Cannot open WAV");
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
                throw std::runtime_error("Cannot read WAV");
            bytes = canonicalRiff(bytes, validateRiff(bytes));
            drwav decoder{};
            if (!drwav_init_memory(&decoder, bytes.data(), bytes.size(), nullptr))
                throw std::runtime_error("WAV decoder rejected file");
            struct Guard {
                drwav* d;
                ~Guard() {
                    drwav_uninit(d);
                }
            } guard{&decoder};
            if (decoder.totalPCMFrameCount > uint64_t(decoder.sampleRate) * 30 || !decoder.totalPCMFrameCount)
                throw std::runtime_error("WAV must be 0..30 seconds");
            Sample s{spec.id, decoder.sampleRate, decoder.channels, {}};
            total += static_cast<size_t>(decoder.totalPCMFrameCount) * s.channels * sizeof(float);
            if (total > 256 * 1024 * 1024)
                throw std::runtime_error("Decoded sound bank exceeds 256 MiB");
            s.pcm.resize(static_cast<size_t>(decoder.totalPCMFrameCount) * s.channels);
            if (drwav_read_pcm_frames_f32(&decoder, decoder.totalPCMFrameCount, s.pcm.data()) !=
                decoder.totalPCMFrameCount)
                throw std::runtime_error("Truncated PCM");
            for (float& v : s.pcm) {
                if (!std::isfinite(v))
                    throw std::runtime_error("NaN/Inf WAV sample");
                v = std::clamp(v, -1.f, 1.f);
            }
            bank.sounds.push_back(std::move(s));
        } catch (const std::exception& e) {
            throw std::runtime_error(utf8(config.file.wstring()) + ":" + std::to_string(spec.line) + ": " +
                                     utf8(spec.path.wstring()) + ": " + e.what());
        }
    return bank;
}
SoundBank convertBank(const SourceBank& source, uint32_t rate) {
    if (rate < 8000 || rate > 384000)
        throw std::runtime_error("Unsupported output sample rate");
    SoundBank out;
    size_t total = 0;
    for (const auto& s : source.sounds) {
        Sample dest{s.id, rate, 2, {}};
        size_t wanted = static_cast<size_t>((uint64_t(s.frames()) * rate + s.rate - 1) / s.rate);
        total += wanted * 2 * sizeof(float);
        if (total > 512 * 1024 * 1024)
            throw std::runtime_error("Converted bank exceeds 512 MiB");
        std::vector<float> stereo(s.frames() * 2);
        for (size_t f = 0; f < s.frames(); ++f) {
            stereo[f * 2] = s.pcm[f * s.channels];
            stereo[f * 2 + 1] = s.pcm[f * s.channels + s.channels - 1];
        }
        if (rate == s.rate)
            dest.pcm = std::move(stereo);
        else {
            auto cfg = ma_resampler_config_init(ma_format_f32, 2, s.rate, rate, ma_resample_algorithm_linear);
            cfg.linear.lpfOrder = 4;
            ma_resampler r{};
            if (ma_resampler_init(&cfg, nullptr, &r) != MA_SUCCESS)
                throw std::runtime_error("Cannot initialize offline resampler");
            struct Guard {
                ma_resampler* r;
                ~Guard() {
                    ma_resampler_uninit(r, nullptr);
                }
            } guard{&r};
            // Remove the interpolator's one-input-frame startup delay only.
            // IIR filter 'latency' is not a constant group delay; subtracting the
            // library's full estimate can advance a transient into leading silence.
            ma_uint64 delay = rate / s.rate;
            // Flush with zeros; preserve source duration and its leading silence.
            size_t padded = stereo.size() + static_cast<size_t>(ma_resampler_get_input_latency(&r) + 64) * 2;
            stereo.resize(padded, 0);
            std::vector<float> temp((wanted + static_cast<size_t>(delay) + 64) * 2);
            ma_uint64 input = stereo.size() / 2, output = temp.size() / 2;
            if (ma_resampler_process_pcm_frames(&r, stereo.data(), &input, temp.data(), &output) !=
                    MA_SUCCESS ||
                output < wanted + delay)
                throw std::runtime_error("Incomplete offline resampling");
            dest.pcm.assign(temp.begin() + static_cast<ptrdiff_t>(delay * 2),
                            temp.begin() + static_cast<ptrdiff_t>((delay + wanted) * 2));
            for (float& v : dest.pcm) {
                if (!std::isfinite(v))
                    throw std::runtime_error("Invalid resampled value");
                v = std::clamp(v, -1.f, 1.f);
            }
        }
        out.sounds.push_back(std::move(dest));
    }
    return out;
}
OutputFormat inspectFormat(const WAVEFORMATEX* w) {
    OutputFormat f{w->nSamplesPerSec,
                   w->nChannels,
                   0,
                   w->wBitsPerSample,
                   w->wBitsPerSample,
                   w->nBlockAlign,
                   false,
                   0,
                   1};
    WORD tag = w->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (w->cbSize < 22)
            throw std::runtime_error("Short endpoint extensible format");
        auto* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(w);
        f.mask = x->dwChannelMask;
        f.validBits = x->Samples.wValidBitsPerSample;
        if (IsEqualGUID(x->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
            tag = WAVE_FORMAT_PCM;
        else if (IsEqualGUID(x->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            tag = WAVE_FORMAT_IEEE_FLOAT;
        else
            throw std::runtime_error("Unsupported endpoint subformat");
    }
    f.floating = tag == WAVE_FORMAT_IEEE_FLOAT;
    if ((tag != WAVE_FORMAT_PCM && !f.floating) || f.channels < 1 || f.channels > 32 || f.rate < 8000 ||
        f.rate > 384000 ||
        (f.floating ? (f.bits != 32 || f.validBits != 32) : (f.bits != 16 && f.bits != 24 && f.bits != 32)) ||
        !f.validBits || f.validBits > f.bits || f.blockAlign != f.channels * (f.bits / 8))
        throw std::runtime_error("Unsupported endpoint format");
    if (f.mask && std::popcount(f.mask) != static_cast<int>(f.channels))
        throw std::runtime_error("Inconsistent speaker mask");
    if (f.channels == 1)
        f.left = f.right = 0;
    else if (f.mask) {
        if (!(f.mask & SPEAKER_FRONT_LEFT) || !(f.mask & SPEAKER_FRONT_RIGHT))
            throw std::runtime_error("Endpoint lacks Front L/R channels");
        f.left = std::popcount(f.mask & (SPEAKER_FRONT_LEFT - 1));
        f.right = std::popcount(f.mask & (SPEAKER_FRONT_RIGHT - 1));
    } else if (f.channels > 2)
        throw std::runtime_error("Multichannel endpoint requires a speaker mask");
    return f;
}
void encodeOutput(const float* stereo, void* output, uint32_t frames, const OutputFormat& f) noexcept {
    if (f.floating && f.channels == 2 && f.left == 0 && f.right == 1) {
        auto* bytes = static_cast<uint8_t*>(output);
        for (size_t i = 0; i < size_t(frames) * 2; ++i) {
            float v = std::clamp(stereo[i], -1.f, 1.f);
            memcpy(bytes + i * 4, &v, 4);
        }
        return;
    }
    auto* dst = static_cast<uint8_t*>(output);
    memset(dst, 0, size_t(frames) * f.blockAlign);
    for (uint32_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < f.channels; ++c) {
            float v = 0;
            if (f.channels == 1)
                v = (stereo[i * 2] + stereo[i * 2 + 1]) * .5f;
            else if (static_cast<int>(c) == f.left)
                v = stereo[i * 2];
            else if (static_cast<int>(c) == f.right)
                v = stereo[i * 2 + 1];
            v = std::clamp(v, -1.f, 1.f);
            uint8_t* p = dst + size_t(i) * f.blockAlign + c * (f.bits / 8);
            if (f.floating)
                memcpy(p, &v, 4);
            else {
                int64_t scale = int64_t(1) << (f.validBits - 1);
                auto integer = std::clamp(static_cast<int64_t>(std::llround(double(v) * double(scale))),
                                          -scale, scale - 1);
                uint32_t bits = static_cast<uint32_t>(integer) << (f.bits - f.validBits);
                for (unsigned b = 0; b < f.bits / 8; ++b)
                    p[b] = static_cast<uint8_t>(bits >> (b * 8));
            }
        }
}
} // namespace taiko
