// Observe physical keyboard input and endpoint loopback, without modifying either player.
#include "audio/DeviceCatalog.h"
#include "audio/SoundBank.h"
#include "input/RawInput.h"
#include <algorithm>
#include <audioclient.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

using namespace taiko;
namespace {
HANDLE interruptEvent{};
BOOL WINAPI interrupted(DWORD) {
    return SetEvent(interruptEvent);
}
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream s;
        s << operation << ": 0x" << std::hex << static_cast<uint32_t>(hr);
        throw std::runtime_error(s.str());
    }
}
struct FormatOwner {
    WAVEFORMATEX* p{};
    ~FormatOwner() {
        CoTaskMemFree(p);
    }
};
struct Packet {
    uint64_t first{}, position{}, time100ns{};
    int64_t arrival{};
    uint32_t frames{}, flags{};
};
double readSample(const BYTE* data, const OutputFormat& f, uint32_t frame, int channel) {
    const BYTE* p = data + size_t(frame) * f.blockAlign + size_t(channel) * f.bits / 8;
    if (f.floating) {
        float v;
        memcpy(&v, p, 4);
        return std::isfinite(v) ? v : 0;
    }
    uint32_t raw = 0;
    for (unsigned i = 0; i < f.bits / 8; ++i)
        raw |= uint32_t(p[i]) << (i * 8);
    int32_t signedValue = std::bit_cast<int32_t>(raw << (32 - f.bits));
    return double(signedValue) / 2147483648.0;
}
std::string currentPeriod(IAudioClient* client) {
    ComPtr<IAudioClient3> c3;
    FormatOwner w;
    UINT32 frames{};
    if (FAILED(client->QueryInterface(IID_PPV_ARGS(&c3))) ||
        FAILED(c3->GetCurrentSharedModeEnginePeriod(&w.p, &frames)))
        return "unknown";
    return std::to_string(frames) + " frames at " + std::to_string(w.p->nSamplesPerSec) + " Hz";
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        std::wstring label = L"trial", deviceId, key = L"S";
        std::filesystem::path destination;
        unsigned seconds = 120;
        bool list = false;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--list") {
                list = true;
                continue;
            }
            if (arg == L"--help") {
                std::cout << "--list | --out NEW_FOLDER [--label A1] [--key S] [--seconds 120] [--device "
                             "ENDPOINT_ID]\n";
                return 0;
            }
            if (i + 1 >= argc)
                throw std::runtime_error("Missing option value");
            auto value = argv[++i];
            if (arg == L"--out")
                destination = value;
            else if (arg == L"--label")
                label = value;
            else if (arg == L"--key")
                key = value;
            else if (arg == L"--seconds")
                seconds = static_cast<unsigned>(std::stoul(value));
            else if (arg == L"--device")
                deviceId = value;
            else
                throw std::runtime_error("Unknown option");
        }
        ComScope com;
        if (list) {
            for (const auto& d : enumerateDevices())
                std::cout << utf8(d.name) << "\n" << utf8(d.id) << "\n\n";
            return 0;
        }
        if (destination.empty() || seconds < 5 || seconds > 300)
            throw std::runtime_error("Specify a NEW --out folder and --seconds 5..300");
        auto found = keyNames().find(utf8(key));
        if (found == keyNames().end())
            throw std::runtime_error("Unknown --key name");
        if (std::filesystem::exists(destination))
            throw std::runtime_error("Output folder already exists; choose a new run name");
        std::filesystem::create_directories(destination);
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)),
              "Enumerator");
        ComPtr<IMMDevice> device;
        check(deviceId.empty() ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)
                               : enumerator->GetDevice(deviceId.c_str(), &device),
              "Select endpoint");
        LPWSTR id{};
        check(device->GetId(&id), "Endpoint ID");
        deviceId = id;
        CoTaskMemFree(id);
        ComPtr<IAudioClient> client;
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(client.GetAddressOf())),
              "Activate loopback");
        FormatOwner wave;
        check(client->GetMixFormat(&wave.p), "Capture format");
        const auto format = inspectFormat(wave.p);
        const auto before = currentPeriod(client.Get());
        // Standard shared loopback: do not request a shortened engine period.
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0,
                                 wave.p, nullptr),
              "Initialize shared loopback");
        Handle event(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
            cancel(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.get() || !cancel.get())
            throw std::runtime_error("Cannot create capture events");
        check(client->SetEventHandle(event.get()), "Set capture event");
        ComPtr<IAudioCaptureClient> capture;
        check(client->GetService(IID_PPV_ARGS(&capture)), "Capture service");
        size_t capacity = size_t(format.rate) * (seconds + 3);
        if (capacity * 2 * sizeof(float) > 256 * 1024 * 1024)
            throw std::runtime_error("Capture exceeds 256 MiB; reduce --seconds");
        std::vector<float> samples(capacity * 2);
        std::vector<Packet> packets;
        packets.reserve(size_t(seconds + 3) * 2000);
        std::vector<PlayRequest> keys;
        keys.reserve(8192);
        auto queue = std::make_unique<PlayQueue>();
        Counters counters;
        RawInput input(*queue, counters);
        input.start();
        std::array<uint32_t, 256> mapping{};
        mapping[found->second] = 1;
        input.configure(mapping, 1, true);
        interruptEvent = cancel.get();
        SetConsoleCtrlHandler(interrupted, TRUE);
        struct HandlerGuard {
            ~HandlerGuard() {
                SetConsoleCtrlHandler(interrupted, FALSE);
            }
        } handler;
        std::cout << "Target: " << utf8(deviceName(device.Get())) << "\nRun: " << utf8(label) << "; key "
                  << utf8(key) << "\n"
                  << "Keep ONLY the target player running. Pause other audio. Focus this console.\n"
                  << "Press the key naturally, about once per second. Do not hold or mash.\n"
                  << "Recording " << seconds
                  << " seconds; Ctrl+C stops and saves. No synthetic keys are sent.\n"
                  << std::flush;
        int64_t start = qpc(), frequency = qpcFrequency();
        check(client->Start(), "Start loopback");
        uint64_t used = 0, discontinuities = 0, timestampErrors = 0;
        bool full = false;
        unsigned lastCount = 0;
        struct StopGuard {
            IAudioClient* c;
            ~StopGuard() {
                c->Stop();
            }
        } stop{client.Get()};
        HANDLE events[]{cancel.get(), event.get()};
        while (qpc() - start < int64_t(seconds) * frequency && !full) {
            DWORD wait = WaitForMultipleObjects(2, events, FALSE, 100);
            if (wait == WAIT_OBJECT_0)
                break;
            if (wait == WAIT_FAILED)
                throw std::runtime_error("Capture wait failed");
            PlayRequest k;
            while (queue->pop(k)) {
                if (keys.size() == keys.capacity()) {
                    full = true;
                    break;
                }
                keys.push_back(k);
            }
            UINT32 available{};
            check(capture->GetNextPacketSize(&available), "Packet size");
            while (available && !full) {
                BYTE* data{};
                UINT32 frames{};
                DWORD flags{};
                UINT64 position{}, time{};
                check(capture->GetBuffer(&data, &frames, &flags, &position, &time), "Capture buffer");
                const auto arrival = qpc();
                if (used + frames > capacity || packets.size() == packets.capacity()) {
                    capture->ReleaseBuffer(frames);
                    full = true;
                    break;
                }
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                    ++discontinuities;
                if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                    ++timestampErrors;
                packets.push_back({used, position, time, arrival, frames, flags});
                for (UINT32 frame = 0; frame < frames; ++frame) {
                    bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    samples[(used + frame) * 2] =
                        silent ? 0 : static_cast<float>(readSample(data, format, frame, format.left));
                    samples[(used + frame) * 2 + 1] =
                        silent ? 0 : static_cast<float>(readSample(data, format, frame, format.right));
                }
                used += frames;
                check(capture->ReleaseBuffer(frames), "Release capture buffer");
                check(capture->GetNextPacketSize(&available), "Next packet");
            }
            if (keys.size() / 10 > lastCount) {
                lastCount = static_cast<unsigned>(keys.size() / 10);
                std::cout << keys.size() << " key presses captured\n" << std::flush;
            }
        }
        input.configure(mapping, 2, false);
        input.stop();
        check(client->Stop(), "Stop capture");
        PlayRequest k;
        while (queue->pop(k))
            keys.push_back(k);
        auto after = currentPeriod(client.Get());
        std::ofstream pcm(destination / L"stereo.f32", std::ios::binary);
        pcm.write(reinterpret_cast<char*>(samples.data()),
                  static_cast<std::streamsize>(used * 2 * sizeof(float)));
        pcm.close();
        std::ofstream packetFile(destination / L"packets.csv");
        packetFile << "first_frame,frames,device_position,qpc_100ns,arrival_qpc,flags\n";
        for (auto& p : packets)
            packetFile << p.first << ',' << p.frames << ',' << p.position << ',' << p.time100ns << ','
                       << p.arrival << ',' << p.flags << '\n';
        packetFile.close();
        std::ofstream inputFile(destination / L"keys.csv");
        inputFile << "sequence,qpc\n";
        for (auto& keyEvent : keys)
            inputFile << keyEvent.sequence << ',' << keyEvent.received << '\n';
        inputFile.close();
        std::ofstream meta(destination / L"metadata.txt", std::ios::binary);
        meta << "format_version=1\nlabel=" << utf8(label) << "\nendpoint=" << utf8(deviceId)
             << "\nname=" << utf8(deviceName(device.Get())) << "\nkey=" << utf8(key)
             << "\nsample_rate=" << format.rate << "\nchannels=2\nqpc_frequency=" << frequency
             << "\nstart_qpc=" << start << "\nperiod_before=" << before << "\nperiod_after=" << after
             << "\nkey_count=" << keys.size() << "\nframes=" << used
             << "\ninput_overflow=" << counters.overflow.load() << "\ndiscontinuities=" << discontinuities
             << "\ntimestamp_errors=" << timestampErrors << "\ncapacity_reached=" << full
             << "\nmeasurement=independent Raw Input receipt to shared endpoint loopback sample timestamp\n";
        meta.close();
        if (!pcm || !packetFile || !inputFile || !meta)
            throw std::runtime_error("Failed to save one or more capture files");
        std::cout << "Saved " << keys.size() << " key presses and " << used << " frames in "
                  << utf8(destination.wstring()) << "\n";
        return full ? 2 : 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
