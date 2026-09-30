#pragma once
#include "audio/PeriodPolicy.h"
#include "audio/SoundBank.h"
#include "diagnostics/Diagnostics.h"
#include "input/RawInput.h"
#include <future>
#include <mutex>
#include <thread>
namespace taiko {
struct StreamInfo {
    bool client3{}, currentKnown{}, shortened{}, mmcss{}, latencyKnown{};
    Periods periods;
    uint32_t requested{}, current{}, capacity{}, target{};
    OutputFormat format;
    std::wstring deviceId, name;
    std::string fallback;
    int64_t reportedLatency100ns{};
};
class WasapiRenderer {
    PlayQueue& requests_;
    DiagnosticQueue& diagnostics_;
    Counters& counters_;
    std::atomic<float>& volume_;
    Handle stop_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread thread_;
    std::atomic<bool> failed_{};
    std::mutex errorMutex_;
    std::string error_;
    void run(std::wstring device, bool stable, std::shared_ptr<const SourceBank> sources, uint32_t generation,
             std::promise<StreamInfo> ready);

  public:
    WasapiRenderer(PlayQueue& q, DiagnosticQueue& d, Counters& c, std::atomic<float>& v)
        : requests_(q), diagnostics_(d), counters_(c), volume_(v) {}
    ~WasapiRenderer() {
        stop();
    }
    StreamInfo start(std::wstring device, bool stable, std::shared_ptr<const SourceBank> sources,
                     uint32_t generation);
    void stop();
    bool failed() const {
        return failed_.load(std::memory_order_acquire);
    }
    std::string error() {
        std::lock_guard lock(errorMutex_);
        return error_;
    }
};
std::string describeStream(const StreamInfo& info);
} // namespace taiko
