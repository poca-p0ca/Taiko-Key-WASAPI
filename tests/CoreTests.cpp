#include "audio/DeviceCatalog.h"
#include "audio/PeriodPolicy.h"
#include "audio/SoundBank.h"
#include "audio/VoiceMixer.h"
#include "core/SpscQueue.h"
#include "input/KeyState.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <ks.h>
#include <ksmedia.h>
#include <thread>

using namespace taiko;
static unsigned checks = 0;
static void require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> static void rejects(F action, const char* message) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, message);
}
static void expectSample(float actual, float expected, float tolerance = 1e-5f) {
    require(std::abs(actual - expected) <= tolerance, "Unexpected sample value");
}
static std::filesystem::path temp;
static const std::string valid = "[Sound Set]\n1: HitSounds/don.wav\n2: HitSounds/kat.wav\n[KeyBind]\nZ: "
                                 "2\nX: 1\nVK_OEM_PERIOD: 1\nVK_OEM_2: 2\n";
static void put16(std::vector<uint8_t>& b, uint16_t n) {
    b.push_back(uint8_t(n));
    b.push_back(uint8_t(n >> 8));
}
static void put32(std::vector<uint8_t>& b, uint32_t n) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(n >> (i * 8)));
}
static void tag(std::vector<uint8_t>& b, const char* s) {
    b.insert(b.end(), s, s + 4);
}
static std::vector<uint8_t> wave(unsigned bits, bool floating, unsigned channels, bool extensible = false,
                                 bool reverse = false) {
    std::vector<uint8_t> b;
    tag(b, "RIFF");
    put32(b, 0);
    tag(b, "WAVE");
    auto fmt = [&] {
        tag(b, "fmt ");
        put32(b, extensible ? 40 : 16);
        put16(b, extensible ? 0xfffe : floating ? 3 : 1);
        put16(b, uint16_t(channels));
        put32(b, 48000);
        put32(b, 48000 * channels * bits / 8);
        put16(b, uint16_t(channels * bits / 8));
        put16(b, uint16_t(bits));
        if (extensible) {
            put16(b, 22);
            put16(b, uint16_t(bits));
            put32(b, channels == 1 ? 4 : 3);
            put32(b, floating ? 3 : 1);
            put16(b, 0);
            put16(b, 0x10);
            put32(b, 0xaa000080);
            put32(b, 0x719b3800);
        }
    };
    auto data = [&] {
        tag(b, "data");
        put32(b, 4 * channels * bits / 8);
        for (int f = 0; f < 4; ++f)
            for (unsigned c = 0; c < channels; ++c) {
                float v = f == 1 ? .5f : f == 2 ? -.5f : 0;
                uint64_t n{};
                if (floating && bits == 64) {
                    double d = v;
                    memcpy(&n, &d, 8);
                } else if (floating)
                    memcpy(&n, &v, 4);
                else if (bits == 8)
                    n = static_cast<uint64_t>(128 + v * 128); // 8-bit WAV PCM is unsigned
                else
                    n = static_cast<uint64_t>(static_cast<int64_t>(double(v) * (uint64_t(1) << (bits - 1))));
                for (unsigned k = 0; k < bits / 8; ++k)
                    b.push_back(uint8_t(n >> (k * 8)));
            }
    };
    if (reverse)
        data();
    else
        fmt();
    tag(b, "JUNK");
    put32(b, 3);
    b.insert(b.end(), {1, 2, 3, 0});
    if (reverse)
        fmt();
    else
        data();
    auto size = uint32_t(b.size() - 8);
    for (int i = 0; i < 4; ++i)
        b[4 + i] = uint8_t(size >> (i * 8));
    return b;
}
static SourceBank decode(const std::vector<uint8_t>& bytes) {
    auto path = temp / L"타격 소리.wav";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    out.close();
    KeyConfig c;
    c.file = temp / L"테스트.ini";
    c.sounds.push_back({1, path, 7});
    return loadSources(c);
}
static void parserTests() {
    auto c = parseConfig("\xEF\xBB\xBF// comment\r\n" + valid, temp / L"설정 파일.ini");
    require(c.keys['Z'] == 2 && c.keys[VK_OEM_2] == 2, "Default compatibility mapping");
    require(c.sounds[0].path == temp / L"HitSounds/don.wav", "INI-relative path resolution");
    c = parseConfig("[Sound Set]\n1: 한글 폴더/소리.wav // hello\n[KeyBind]\nVK_NUMPAD0: 1\n9: 1\n",
                    temp / L"설정.ini");
    require(c.sounds[0].path == temp / L"한글 폴더/소리.wav", "Unicode path");
    rejects([&] { parseConfig(valid + "Z: 1\n", temp / L"x.ini"); }, "Duplicate key accepted");
    rejects([&] { parseConfig(valid + "Q: 3\n", temp / L"x.ini"); }, "Missing sound ID accepted");
    rejects([&] { parseConfig(valid + "UNKNOWN: 1\n", temp / L"x.ini"); }, "Unknown key accepted");
    rejects([&] { parseConfig(valid + "A: 1garbage\n", temp / L"x.ini"); }, "Malformed integer accepted");
    rejects([&] { parseConfig("[Sound Set]\n1: a\n1: b\n[KeyBind]\n", temp / L"x.ini"); },
            "Duplicate sound accepted");
    rejects([&] { parseConfig(valid + "A: 0\n", temp / L"x.ini"); }, "Zero sound accepted");
    rejects([&] { parseConfig(std::string("\xff") + valid, temp / L"x.ini"); }, "Invalid UTF-8 accepted");
    auto old = parseConfig(valid, temp / L"original.ini");
    try {
        auto next = parseConfig(valid + "Z: 1", temp / L"bad.ini");
        old = std::move(next);
    } catch (...) {
    }
    require(old.keys['Z'] == 2 && old.file.filename() == L"original.ini",
            "Failed parse destroyed previous config");
    try {
        decode({1, 2, 3});
    } catch (const std::exception& e) {
        require(std::string(e.what()).find(":7:") != std::string::npos, "Missing WAV source line");
    }
}
static void queueTests() {
    SpscQueue<uint64_t, 3> q;
    uint64_t x{};
    require(!q.pop(x), "Empty queue not empty");
    for (uint64_t i = 0; i < 3; ++i)
        require(q.push(i), "Queue usable capacity");
    require(!q.push(99), "Queue overflow accepted");
    for (uint64_t i = 0; i < 3; ++i) {
        require(q.pop(x), "Queue lost value");
        require(x == i, "Queue order");
    }
    SpscQueue<uint64_t, 1024> concurrent;
    constexpr uint64_t n = 200000;
    std::atomic<bool> ordered{true};
    std::thread producer([&] {
        for (uint64_t i = 0; i < n; ++i)
            while (!concurrent.push(i))
                std::this_thread::yield();
    });
    for (uint64_t i = 0; i < n; ++i) {
        while (!concurrent.pop(x))
            std::this_thread::yield();
        if (x != i)
            ordered = false;
    }
    producer.join();
    require(ordered, "SPSC concurrent ordering");
    require(!concurrent.pop(x), "SPSC extra value");
}
static void inputTests() {
    KeyState keys;
    require(keys.transition(1, 'Z', RI_KEY_E0), "Extended make lost");
    require(!keys.transition(1, 'Z', RI_KEY_E0), "Repeat not suppressed");
    require(!keys.transition(1, 'Z', RI_KEY_BREAK | RI_KEY_E0), "Break triggered");
    require(keys.transition(1, 'Z', 0), "Next make lost");
    require(keys.transition(2, 'Z', 0), "Device states not independent");
    keys.remove(1);
    require(keys.transition(1, 'Z', 0), "Device removal did not clear");
    require(!keys.transition(1, 255, 0), "Invalid key accepted");
    require(!keys.transition(1, 600, 0), "Out-of-range key accepted");
    SpscQueue<int, 1> q;
    q.push(1);
    keys.clear();
    if (keys.transition(1, 'X', 0))
        require(!q.push(2), "Overflow setup failed");
    keys.transition(1, 'X', RI_KEY_BREAK);
    int value;
    q.pop(value);
    require(keys.transition(1, 'X', 0) && q.push(3), "Overflow stuck key");
    keys.clear();
    for (uintptr_t i = 0; i < 32; ++i)
        require(keys.transition(i, 'A', 0), "Device table capacity");
    bool full = false;
    keys.transition(100, 'A', 0, &full);
    require(full, "Device table overflow missing");
}
static void mixerTests() {
    Sample hit{1, 48000, 2, {.4f, .2f, .3f, .1f}};
    VoiceMixer mixer;
    float out[16]{};
    require(!mixer.mix(out, 4, 1), "Empty mixer not silent");
    mixer.trigger(&hit);
    mixer.trigger(&hit);
    mixer.mix(out, 1, 1);
    expectSample(out[0], .8f);
    expectSample(out[1], .4f);
    mixer.trigger(&hit);
    mixer.mix(out, 2, 1);
    expectSample(out[0], 1.f);
    expectSample(out[1], .4f);
    expectSample(out[2], .3f);
    require(!mixer.mix(out, 2, 1), "Finished voices retained");
    mixer.trigger(&hit);
    mixer.mix(out, 1, .5f);
    expectSample(out[0], .2f);
    require(!mixer.mix(out, 1, 0), "Mute not silent");
    require(!mixer.mix(out, 1, 1), "Mute did not advance voices");
    for (int i = 0; i < 129; ++i)
        mixer.trigger(&hit);
    require(mixer.stolen() == 1, "Voice limit/replacement");
    mixer.mix(out, 2, 1);
    expectSample(out[0], 1);
    expectSample(out[1], 1);
    VoiceMixer four;
    for (int i = 0; i < 4; ++i)
        four.trigger(&hit);
    four.mix(out, 1, .25f);
    expectSample(out[0], .4f);
    expectSample(out[1], .2f);
}
static void waveTests() {
    for (unsigned bits : {8u, 16u, 24u, 32u})
        for (unsigned channels : {1u, 2u})
            for (bool ext : {false, true})
                for (bool reverse : {false, true}) {
                    SourceBank bank;
                    try {
                        bank = decode(wave(bits, false, channels, ext, reverse));
                    } catch (const std::exception& e) {
                        throw std::runtime_error(std::string(e.what()) + " bits=" + std::to_string(bits) +
                                                 " channels=" + std::to_string(channels) + " ext=" +
                                                 std::to_string(ext) + " reverse=" + std::to_string(reverse));
                    }
                    require(bank.sounds[0].frames() == 4, "Wrong WAV length");
                    // dr_wav maps unsigned 8-bit as u8/255*2-1, so 192 decodes to ~0.506.
                    expectSample(bank.sounds[0].pcm[channels], .5f, bits == 8 ? .01f : 1e-5f);
                }
    for (unsigned bits : {32u, 64u})
        for (bool ext : {false, true}) {
            auto bank = decode(wave(bits, true, 2, ext));
            expectSample(bank.sounds[0].pcm[2], .5f);
        }
    auto setRiffSize = [](std::vector<uint8_t>& b, uint32_t size) {
        for (int i = 0; i < 4; ++i)
            b[4 + i] = uint8_t(size >> (i * 8));
    };
    auto lenient = [&](std::vector<uint8_t> bytes, const char* message) {
        SourceBank bank;
        try {
            bank = decode(bytes);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string(message) + ": " + e.what());
        }
        require(bank.sounds[0].frames() == 4, message);
        expectSample(bank.sounds[0].pcm[1], .5f);
    };
    auto bytes = wave(16, false, 1);
    setRiffSize(bytes, 0);
    lenient(bytes, "Wrong RIFF size rejected");
    bytes = wave(16, false, 1);
    bytes.insert(bytes.end(), {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 0x7f});
    lenient(bytes, "Tag appended after RIFF body rejected");
    bytes = wave(16, false, 1);
    bytes.insert(bytes.end(), {'L', 'I', 'S', 'T', 3, 0, 0, 0, 'a', 'b', 'c'});
    setRiffSize(bytes, uint32_t(bytes.size() - 8));
    lenient(bytes, "Final odd chunk without pad byte rejected");
    bytes = wave(16, false, 1);
    for (size_t i = 12; i + 8 < bytes.size(); ++i)
        if (!memcmp(bytes.data() + i, "data", 4)) {
            uint32_t placeholder = 0xffffffff;
            memcpy(bytes.data() + i + 4, &placeholder, 4);
            break;
        }
    bytes.push_back(0x12); // half of a frame left by an interrupted stream writer
    lenient(bytes, "Streaming data-size placeholder rejected");
    bytes = wave(16, false, 1);
    bytes.pop_back();
    rejects([&] { decode(bytes); }, "Truncated WAV accepted");
    bytes = wave(16, false, 1);
    bytes[16] = 0xff;
    rejects([&] { decode(bytes); }, "Oversize chunk accepted");
    bytes = wave(32, true, 1);
    for (size_t i = 12; i + 8 < bytes.size(); ++i)
        if (!memcmp(bytes.data() + i, "data", 4)) {
            uint32_t nan = 0x7fc00000;
            memcpy(bytes.data() + i + 8, &nan, 4);
            break;
        }
    rejects([&] { decode(bytes); }, "NaN accepted");
    bytes = wave(32, false, 2, true);
    bytes[44] = 2;
    rejects([&] { decode(bytes); }, "Bad extensible GUID accepted");
    auto original = readConfig(std::filesystem::path(TAIKO_SOURCE_DIR) / L"assets/KeyBind.ini");
    auto bank = loadSources(original);
    require(bank.sounds.size() == 2, "Original WAVs not loadable");
}
static void resamplerTests() {
    SourceBank source;
    Sample s{1, 48000, 1, std::vector<float>(4800)};
    for (size_t i = 960; i < 1920; ++i)
        s.pcm[i] = .5f;
    source.sounds.push_back(s);
    for (uint32_t rate : {44100u, 48000u, 96000u, 8000u, 192000u}) {
        auto converted = convertBank(source, rate);
        auto& result = converted.sounds[0];
        require(result.frames() == rate / 10, "Resample output duration");
        size_t onset = 0;
        while (onset < result.frames() && std::abs(result.pcm[onset * 2]) < .1f)
            ++onset;
        if (std::abs(double(onset) - double(rate) * .02) > 8)
            throw std::runtime_error("Resampler onset=" + std::to_string(onset) + " expected=" +
                                     std::to_string(double(rate) * .02) + " rate=" + std::to_string(rate));
        for (size_t i = 0; i < result.frames(); ++i) {
            require(std::isfinite(result.pcm[i * 2]) && std::abs(result.pcm[i * 2]) <= 1, "Resampler range");
            expectSample(result.pcm[i * 2], result.pcm[i * 2 + 1]);
        }
    }
}
static void formatTests() {
    WAVEFORMATEXTENSIBLE w{};
    w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    w.Format.cbSize = 22;
    w.Format.nChannels = 6;
    w.Format.nSamplesPerSec = 48000;
    w.Format.wBitsPerSample = 32;
    w.Format.nBlockAlign = 24;
    w.Samples.wValidBitsPerSample = 32;
    w.dwChannelMask = 0x3f;
    w.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    auto f = inspectFormat(&w.Format);
    float stereo[] = {.5f, -.5f};
    float out[6]{};
    encodeOutput(stereo, out, 1, f);
    expectSample(out[0], .5f);
    expectSample(out[1], -.5f);
    for (int i = 2; i < 6; ++i)
        expectSample(out[i], 0);
    w.dwChannelMask = 0;
    rejects([&] { inspectFormat(&w.Format); }, "Ambiguous multichannel accepted");
    w.dwChannelMask = 0x3f;
    w.Samples.wValidBitsPerSample = 24;
    rejects([&] { inspectFormat(&w.Format); }, "Invalid float bits accepted");
    w.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    f = inspectFormat(&w.Format);
    uint32_t ints[6]{};
    encodeOutput(stereo, ints, 1, f);
    require(ints[0] == 0x40000000 && ints[1] == 0xc0000000, "24 valid bits in 32-bit container");
    OutputFormat mono{48000, 1, 4, 16, 16, 2, false, 0, 0};
    int16_t value = 1;
    encodeOutput(stereo, &value, 1, mono);
    require(value == 0, "Mono downmix");
    OutputFormat pcm24{48000, 2, 3, 24, 24, 6, false, 0, 1};
    uint8_t packed[6]{};
    float limit[] = {1, -1};
    encodeOutput(limit, packed, 1, pcm24);
    require(packed[0] == 0xff && packed[2] == 0x7f && packed[3] == 0 && packed[5] == 0x80,
            "Packed PCM clipping");
}
static void periodTests() {
    require(choosePeriod({448, 4, 48, 448}, false) == 48, "Minimum selection");
    require(choosePeriod({448, 4, 49, 448}, false) == 52, "Fundamental alignment");
    require(choosePeriod({448, 4, 48, 448}, true) == 448, "Stable selection");
    require(choosePeriod({480, 48, 480, 480}, false) == 480, "Minimum equals default");
    rejects([] { choosePeriod({100, 0, 1, 100}, false); }, "Zero fundamental accepted");
    rejects([] { choosePeriod({100, 64, 90, 100}, false); }, "Impossible period accepted");
    require(framesToWrite(512, 16, 64) == 48, "Filled capacity instead of target");
    require(framesToWrite(512, 80, 64) == 0, "Overfilled target");
    require(framesToWrite(32, 0, 64) == 32, "Capacity exceeded");
    rejects([] { framesToWrite(32, 33, 64); }, "Invalid padding accepted");
}
static void notificationTests() {
    Handle wake(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    require(wake.get() != nullptr, "Notification event creation");
    std::atomic<uint32_t> changes{};
    DeviceNotifications selected(wake.get(), changes, L"usb");
    PROPERTYKEY driverProperty{};
    driverProperty.pid = 1234;
    for (int i = 0; i < 1000; ++i)
        selected.OnPropertyValueChanged(L"usb", driverProperty);
    require(changes.load() == 0, "Driver property storm requested a restart");
    require(WaitForSingleObject(wake.get(), 0) == WAIT_TIMEOUT, "Property storm woke controller");
    selected.OnPropertyValueChanged(L"usb", PKEY_Device_FriendlyName);
    require(changes.exchange(0) == DeviceListChanged, "Rename must only refresh picker");
    selected.OnDeviceStateChanged(L"other", DEVICE_STATE_ACTIVE);
    require(changes.exchange(0) == (DeviceListChanged | DeviceAvailabilityChanged),
            "Unrelated device interrupted selected output");
    selected.OnDeviceRemoved(L"usb");
    require(changes.exchange(0) == (DeviceListChanged | DeviceAvailabilityChanged | SelectedDeviceChanged),
            "Selected removal did not request reconnect");
    selected.OnDeviceAdded(L"usb");
    selected.OnPropertyValueChanged(L"usb", PKEY_Device_FriendlyName);
    require(changes.exchange(0) == (DeviceListChanged | DeviceAvailabilityChanged | SelectedDeviceChanged),
            "Metadata overwrote pending reconnect");
    selected.OnDefaultDeviceChanged(eRender, eConsole, L"other");
    require(changes.load() == 0, "Default change interrupted explicitly selected output");
    DeviceNotifications followDefault(wake.get(), changes, L"");
    followDefault.OnPropertyValueChanged(L"usb", driverProperty);
    followDefault.OnDefaultDeviceChanged(eCapture, eConsole, L"mic");
    followDefault.OnDefaultDeviceChanged(eRender, eCommunications, L"usb");
    require(changes.load() == 0, "Unrelated notifications interrupted default output");
    followDefault.OnDeviceStateChanged(L"mic", DEVICE_STATE_ACTIVE);
    require(changes.exchange(0) == (DeviceListChanged | DeviceAvailabilityChanged),
            "Unrelated availability restarted healthy default output");
    followDefault.OnDefaultDeviceChanged(eRender, eConsole, L"usb");
    require(changes.exchange(0) == (DeviceListChanged | DefaultOutputChanged),
            "Default output switch did not request reconnect");
    followDefault.OnDefaultDeviceChanged(eRender, eConsole, nullptr);
    require(changes.exchange(0) == (DeviceListChanged | DefaultOutputChanged),
            "Default output removal was lost");
}
int main() {
    temp = std::filesystem::temp_directory_path() /
           (L"TaikoCoreTests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(temp);
    int failed = 0;
    for (auto [name, test] : std::vector<std::pair<const char*, std::function<void()>>>{
             {"config", parserTests},
             {"SPSC queue", queueTests},
             {"input state", inputTests},
             {"voice mixer", mixerTests},
             {"WAV", waveTests},
             {"offline resampler", resamplerTests},
             {"output format", formatTests},
             {"device notifications", notificationTests},
             {"period/render policy", periodTests}}) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    }
    std::filesystem::remove_all(temp);
    std::cout << checks << " checks; " << failed << " failed groups\n";
    return failed ? 1 : 0;
}
