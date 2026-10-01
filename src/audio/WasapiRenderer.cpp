#include "audio/WasapiRenderer.h"
#include "audio/DeviceCatalog.h"
#include "audio/VoiceMixer.h"
#include <audioclient.h>
#include <avrt.h>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace taiko {
static std::string hexHr(HRESULT hr) {
    std::ostringstream out;
    out << "0x" << std::hex << static_cast<uint32_t>(hr);
    return out.str();
}
static void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        throw std::runtime_error(std::string(what) + " " + hexHr(hr));
}
struct WaveOwner {
    WAVEFORMATEX* p{};
    ~WaveOwner() {
        CoTaskMemFree(p);
    }
    void reset() {
        CoTaskMemFree(p);
        p = nullptr;
    }
};
struct Mmcss {
    HANDLE handle{};
    Mmcss() {
        DWORD index{};
        handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
        if (!handle)
            handle = AvSetMmThreadCharacteristicsW(L"Audio", &index);
        if (handle)
            AvSetMmThreadPriority(handle, AVRT_PRIORITY_HIGH);
    }
    ~Mmcss() {
        if (handle)
            AvRevertMmThreadCharacteristics(handle);
    }
};
StreamInfo WasapiRenderer::start(std::wstring device, bool stable, std::shared_ptr<const SourceBank> sources,
                                 uint32_t generation) {
    stop();
    failed_.store(false);
    ResetEvent(stop_.get());
    std::promise<StreamInfo> p;
    auto ready = p.get_future();
    thread_ = std::thread(&WasapiRenderer::run, this, std::move(device), stable, std::move(sources),
                          generation, std::move(p));
    try {
        return ready.get();
    } catch (...) {
        stop();
        throw;
    }
}
void WasapiRenderer::stop() {
    if (thread_.joinable()) {
        SetEvent(stop_.get());
        thread_.join();
    }
    requests_.discard();
}
void WasapiRenderer::run(std::wstring selected, bool stable, std::shared_ptr<const SourceBank> sources,
                         uint32_t generation, std::promise<StreamInfo> ready) {
    bool promised = false;
    try {
        ComScope com;
        StreamInfo info;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)),
              "Device enumerator");
        ComPtr<IMMDevice> device;
        check(selected.empty() ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)
                               : enumerator->GetDevice(selected.c_str(), &device),
              "Output device unavailable");
        DWORD state{};
        check(device->GetState(&state), "Device state");
        if (!(state & DEVICE_STATE_ACTIVE))
            throw std::runtime_error("Output device unavailable (inactive)");
        info.name = deviceName(device.Get());
        LPWSTR id{};
        check(device->GetId(&id), "Device ID");
        info.deviceId = id;
        CoTaskMemFree(id);
        ComPtr<IAudioClient> client;
        ComPtr<IAudioClient3> client3;
        WaveOwner format;
        auto fresh = [&] {
            client3.Reset();
            client.Reset();
            format.reset();
            check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(client.GetAddressOf())),
                  "Activate audio client");
            check(client->GetMixFormat(&format.p), "Get mix format");
            ComPtr<IAudioClient2> c2;
            if (SUCCEEDED(client.As(&c2))) {
                AudioClientProperties properties{};
                properties.cbSize = sizeof(properties);
                properties.bIsOffload = FALSE;
                properties.eCategory = AudioCategory_GameEffects;
                properties.Options = AUDCLNT_STREAMOPTIONS_NONE;
                c2->SetClientProperties(&properties);
            }
        };
        fresh();
        HRESULT low = client.As(&client3);
        if (SUCCEEDED(low)) {
            low =
                client3->GetSharedModeEnginePeriod(format.p, &info.periods.normal, &info.periods.fundamental,
                                                   &info.periods.minimum, &info.periods.maximum);
            if (SUCCEEDED(low)) {
                try {
                    info.requested = choosePeriod(info.periods, stable);
                } catch (...) {
                    low = E_INVALIDARG;
                }
                if (SUCCEEDED(low))
                    low = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                               info.requested, format.p, nullptr);
            }
            if (low == AUDCLNT_E_ENGINE_PERIODICITY_LOCKED || low == AUDCLNT_E_ENGINE_FORMAT_LOCKED) {
                WaveOwner current;
                UINT32 period{};
                if (SUCCEEDED(client3->GetCurrentSharedModeEnginePeriod(&current.p, &period))) {
                    // A bounded retry on a newly activated object, using the engine's reported format.
                    info.lockedRetry = true;
                    fresh();
                    format.reset();
                    format.p = current.p;
                    current.p = nullptr;
                    low = client.As(&client3);
                    if (SUCCEEDED(low))
                        low = client3->GetSharedModeEnginePeriod(
                            format.p, &info.periods.normal, &info.periods.fundamental, &info.periods.minimum,
                            &info.periods.maximum);
                    if (SUCCEEDED(low) && info.periods.fundamental && period >= info.periods.minimum &&
                        period <= info.periods.maximum && period % info.periods.fundamental == 0) {
                        info.requested = period;
                        low = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period,
                                                                   format.p, nullptr);
                    } else
                        low = AUDCLNT_E_INVALID_DEVICE_PERIOD;
                }
            }
        }
        info.client3 = SUCCEEDED(low);
        if (!info.client3) {
            info.lowLatencyResult = low;
            info.fallback = "IAudioClient3 initialization: " + hexHr(low);
            fresh();
            check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0,
                                     format.p, nullptr),
                  "Standard shared initialization");
            REFERENCE_TIME normal{}, minimum{};
            check(client->GetDevicePeriod(&normal, &minimum), "Default device period");
            info.requested =
                static_cast<uint32_t>((uint64_t(normal) * format.p->nSamplesPerSec + 9999999) / 10000000);
        } else {
            WaveOwner actual;
            UINT32 period{};
            if (SUCCEEDED(client3->GetCurrentSharedModeEnginePeriod(&actual.p, &period))) {
                info.current = period;
                info.currentKnown = true;
                auto actualFormat = inspectFormat(actual.p), requestedFormat = inspectFormat(format.p);
                if (actualFormat.rate != requestedFormat.rate ||
                    actualFormat.channels != requestedFormat.channels ||
                    actualFormat.bits != requestedFormat.bits ||
                    actualFormat.validBits != requestedFormat.validBits ||
                    actualFormat.mask != requestedFormat.mask ||
                    actualFormat.floating != requestedFormat.floating)
                    throw std::runtime_error("Engine format changed during initialization");
            }
        }
        info.format = inspectFormat(format.p);
        info.shortened = info.client3 && info.currentKnown && info.current < info.periods.normal;
        check(client->GetBufferSize(&info.capacity), "Endpoint capacity");
        uint32_t period = info.currentKnown ? info.current : info.requested;
        if (!period || !info.capacity || info.capacity > info.format.rate * 2)
            throw std::runtime_error("Invalid endpoint capacity/period");
        info.target =
            static_cast<uint32_t>(std::min<uint64_t>(info.capacity, uint64_t(period) * (stable ? 2 : 1)));
        REFERENCE_TIME latency{};
        if (SUCCEEDED(client->GetStreamLatency(&latency))) {
            info.reportedLatency100ns = latency;
            info.latencyKnown = true;
        }
        Handle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!event.get())
            throw std::runtime_error("Audio event creation failed");
        check(client->SetEventHandle(event.get()), "Audio event handle");
        ComPtr<IAudioRenderClient> render;
        check(client->GetService(IID_PPV_ARGS(&render)), "Render service");
        // Loading/conversion completes on a loader thread before the real-time loop starts.
        auto load = std::async(std::launch::async,
                               [sources, rate = info.format.rate] { return convertBank(*sources, rate); });
        SoundBank bank = load.get();
        std::vector<float> scratch(size_t(info.capacity) * 2);
        VoiceMixer mixer;
        BYTE* initial{};
        check(render->GetBuffer(info.target, &initial), "Prime buffer");
        check(render->ReleaseBuffer(info.target, AUDCLNT_BUFFERFLAGS_SILENT), "Prime silence");
        Mmcss mmcss;
        info.mmcss = mmcss.handle != nullptr;
        check(client->Start(), "Start shared stream");
        struct Stopper {
            IAudioClient* c;
            ~Stopper() {
                c->Stop();
            }
        } stopper{client.Get()};
        ready.set_value(info);
        promised = true;
        HANDLE waits[]{stop_.get(), event.get()};
        int64_t previous = 0, frequency = qpcFrequency();
        uint64_t previousStolen = 0;
        HRESULT renderError = S_OK;
        const char* failure = "Render";
        while (true) {
            DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 2000);
            if (wait == WAIT_OBJECT_0)
                break;
            if (wait != WAIT_OBJECT_0 + 1) {
                renderError = HRESULT_FROM_WIN32(wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
                failure = "Audio event wait";
                break;
            }
            const int64_t at = qpc();
            UINT32 padding{};
            if (FAILED(renderError = client->GetCurrentPadding(&padding))) {
                failure = "GetCurrentPadding";
                break;
            }
            if (padding > info.capacity) {
                renderError = E_UNEXPECTED;
                failure = "Padding exceeds capacity";
                break;
            }
            uint32_t frames =
                std::min(info.capacity - padding, info.target > padding ? info.target - padding : 0);
            if (frames) {
                BYTE* output{};
                if (FAILED(renderError = render->GetBuffer(frames, &output))) {
                    failure = "GetBuffer";
                    break;
                }
                PlayRequest request{};
                const bool recording = recording_.load(std::memory_order_relaxed);
                for (size_t count = 0; count < 256 && requests_.pop(request); ++count)
                    if (request.generation == generation) {
                        mixer.trigger(bank.find(request.sound));
                        if (!recording)
                            continue;
                        DiagnosticRecord record{1,
                                                padding,
                                                frames,
                                                generation,
                                                at,
                                                0,
                                                0,
                                                qpc() - request.received,
                                                request.sequence};
                        if (!diagnostics_.push(record))
                            counters_.diagnosticDrops.fetch_add(1, std::memory_order_relaxed);
                    }
                bool audible = mixer.mix(scratch.data(), frames, volume_.load(std::memory_order_relaxed));
                if (audible)
                    encodeOutput(scratch.data(), output, frames, info.format);
                renderError = render->ReleaseBuffer(frames, audible ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);
                if (FAILED(renderError)) {
                    failure = "ReleaseBuffer";
                    break;
                }
            }
            int64_t duration = qpc() - at, interval = previous ? at - previous : 0;
            previous = at;
            if (duration > int64_t(period) * frequency / info.format.rate)
                counters_.renderOverruns.fetch_add(1, std::memory_order_relaxed);
            auto stolen = mixer.stolen();
            counters_.stolen.fetch_add(stolen - previousStolen, std::memory_order_relaxed);
            previousStolen = stolen;
            if (recording_.load(std::memory_order_relaxed) &&
                !diagnostics_.push({0, padding, frames, generation, at, duration, interval, 0, 0}))
                counters_.diagnosticDrops.fetch_add(1, std::memory_order_relaxed);
        }
        check(renderError, failure);
        // All bank destruction, COM release and MMCSS cleanup occur outside the render loop.
    } catch (const std::exception& e) {
        {
            std::lock_guard lock(errorMutex_);
            error_ = e.what();
        }
        failed_.store(true, std::memory_order_release);
        if (!promised)
            ready.set_exception(std::current_exception());
    }
}
namespace {
struct FallbackCause {
    const char* log;
    const wchar_t* alert;
};
FallbackCause fallbackCause(const StreamInfo& i) {
    switch (i.lowLatencyResult) {
    case E_NOINTERFACE:
    case E_NOTIMPL:
        return {"IAudioClient3 is not available",
                L"Windows 오디오 엔진에서 저지연 공유 모드(IAudioClient3)를 사용할 수 없습니다."};
    case AUDCLNT_E_ENGINE_PERIODICITY_LOCKED:
        return {"another stream locked the engine period",
                L"다른 프로그램이 이 장치의 오디오 엔진 주기를 고정하고 있습니다."};
    case AUDCLNT_E_ENGINE_FORMAT_LOCKED:
        return {"another stream locked the engine format",
                L"다른 프로그램이 이 장치의 오디오 엔진 포맷을 고정하고 있습니다."};
    case AUDCLNT_E_INVALID_DEVICE_PERIOD:
        if (i.lockedRetry)
            return {"the period locked by another stream is not usable",
                    L"다른 프로그램이 고정한 엔진 주기로는 저지연 스트림을 열 수 없습니다."};
        return {"the device rejected the requested period", L"장치가 요청한 엔진 주기를 거부했습니다."};
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
        return {"the device rejected the shared mix format",
                L"장치가 저지연 스트림의 출력 포맷을 거부했습니다."};
    case E_INVALIDARG:
        return {"invalid period range or arguments",
                L"장치가 보고한 엔진 주기 범위가 올바르지 않거나 요청이 거부되었습니다."};
    default:
        return {"IAudioClient3 initialization failed", L"저지연 스트림 초기화가 실패했습니다."};
    }
}
double periodMs(const StreamInfo& i) {
    return i.format.rate ? double(i.requested) * 1000 / i.format.rate : 0;
}
} // namespace
std::string lowLatencyFailureLog(const StreamInfo& i) {
    std::ostringstream o;
    o << "LOW-LATENCY UNAVAILABLE: " << fallbackCause(i).log << " (" << hexHr(i.lowLatencyResult)
      << "); device=" << utf8(i.name) << "; playing in standard shared mode, engine period " << i.requested
      << " frames (" << std::fixed << std::setprecision(3) << periodMs(i) << " ms)";
    return o.str();
}
std::wstring lowLatencyFailureAlert(const StreamInfo& i) {
    std::wostringstream o;
    o << L"저지연 모드를 사용할 수 없었습니다.\n\n장치: " << i.name << L"\n원인: " << fallbackCause(i).alert
      << L" (" << wide(hexHr(i.lowLatencyResult)) << L")\n현재: 일반 공유 모드로 재생 중, 엔진 주기 "
      << i.requested << L" frames (" << std::fixed << std::setprecision(3) << periodMs(i)
      << L" ms)\n\n재생은 계속되지만 저지연 모드보다 지연이 길 수 있습니다. "
         L"다른 오디오 프로그램을 종료하거나 출력 장치를 바꾼 뒤 Settings에서 장치 새로고침을 눌러 보세요. "
         L"자세한 내용은 Settings의 진단 내용과 session.log에 있습니다.";
    return o.str();
}
std::string describeStream(const StreamInfo& i) {
    std::ostringstream o;
    o << "device=" << utf8(i.name) << "\r\nid=" << utf8(i.deviceId)
      << "\r\npath=" << (i.client3 ? "IAudioClient3" : "IAudioClient shared fallback")
      << "\r\nformat=" << i.format.rate << " Hz, " << i.format.channels << " ch, "
      << (i.format.floating ? "float" : "PCM") << i.format.bits << " valid=" << i.format.validBits
      << " mask=" << i.format.mask << "\r\nperiod frames default/fundamental/min/max=" << i.periods.normal
      << "/" << i.periods.fundamental << "/" << i.periods.minimum << "/" << i.periods.maximum
      << "\r\nrequested=" << i.requested
      << ", current=" << (i.currentKnown ? std::to_string(i.current) : "unknown")
      << "\r\ncapacity=" << i.capacity << ", target queued=" << i.target << " frames (" << std::fixed
      << std::setprecision(3) << double(i.target) * 1000 / i.format.rate << " ms of audio)"
      << "\r\ndriver reported stream latency="
      << (i.latencyKnown ? std::to_string(double(i.reportedLatency100ns) / 10000) + " ms" : "unknown")
      << "; MMCSS=" << (i.mmcss ? "registered" : "unavailable") << "\r\n"
      << i.fallback
      << "\r\nEngine/buffer durations and driver reports are NOT physical input-to-output latency.";
    return o.str();
}
} // namespace taiko
