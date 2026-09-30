#include "app/Controller.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <winternl.h>

namespace taiko {
Controller::~Controller() {
    if (worker_.joinable()) {
        post({CommandType::Shutdown, {}, {}});
        worker_.join();
    }
}
void Controller::post(Command command) {
    {
        std::lock_guard lock(mutex_);
        if (command.type == CommandType::Shutdown)
            commands_.clear();
        if (commands_.size() < 64)
            commands_.push_back(std::move(command));
    }
    SetEvent(wake_.get());
}
void Controller::log(const std::string& text) {
    if (log_) {
        log_ << qpc() << " " << text << "\n";
        log_.flush();
    }
}
void Controller::defaults() {
    settings_.config = root_ / L"KeyBind.ini";
    // Support replacing only the EXE in a 0.2.1 folder. Never consult AppData,
    // and never overwrite an existing portable INI or sound.
    auto legacy = root_ / L"assets";
    if (!std::filesystem::exists(settings_.config) && std::filesystem::exists(legacy / L"KeyBind.ini")) {
        std::filesystem::copy_file(legacy / L"KeyBind.ini", settings_.config);
    }
    for (auto name : {L"don.wav", L"kat.wav"}) {
        auto target = root_ / L"HitSounds" / name;
        auto source = legacy / L"HitSounds" / name;
        if (!std::filesystem::exists(target) && std::filesystem::exists(source)) {
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(source, target);
        }
    }
    std::ifstream in(root_ / L"AppSettings.ini", std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        auto split = line.find('=');
        if (split == std::string::npos)
            continue;
        auto key = line.substr(0, split), value = line.substr(split + 1);
        try {
            if (key == "device")
                settings_.device = wide(value);
            // Legacy absolute config= values are deliberately ignored.
            if (key == "stable")
                settings_.stable = value == "1";
            if (key == "volume")
                volume_.store(std::clamp(std::stof(value), 0.f, 1.f));
        } catch (...) { /* Preserve safe defaults for malformed application preferences. */
        }
    }
    if (!std::isfinite(volume_.load()))
        volume_.store(.25f);
}
void Controller::save() {
    auto path = root_ / L"AppSettings.ini", temp = root_ / L"AppSettings.tmp";
    std::ofstream out(temp, std::ios::binary);
    out << "device=" << utf8(settings_.device) << "\nstable=" << settings_.stable
        << "\nvolume=" << volume_.load() << "\n";
    out.close();
    if (!out || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        log("Could not persist application settings");
}
void Controller::publish() {
    std::wostringstream detail;
    detail << L"Last successful stream (may be stopped):\r\n"
           << wide(streamDetails_) << L"\r\n\r\nQueue overflow=" << counters_.overflow.load()
           << L"; voice replacements=" << counters_.stolen.load() << L"; restarts="
           << counters_.restarts.load() << L"\r\nRender deadline overruns=" << counters_.renderOverruns.load()
           << L" (not confirmed audible glitches)\r\nDiagnostic drops=" << counters_.diagnosticDrops.load()
           << L"; keyboard table overflow=" << counters_.inputDeviceOverflow.load() << L"\r\nLogs: "
           << root_.wstring() << L"\r\n"
           << error_;
    std::lock_guard lock(mutex_);
    snapshot_.status = state_;
    snapshot_.details = detail.str();
    snapshot_.error = error_;
    snapshot_.settings = settings_;
    snapshot_.bindings = wide(bindingsText(config_));
    snapshot_.running = running_;
    snapshot_.revision = ++revision_;
    snapshot_.previewDon =
        std::any_of(config_.sounds.begin(), config_.sounds.end(), [](const auto& s) { return s.id == 1; });
    snapshot_.previewKat =
        std::any_of(config_.sounds.begin(), config_.sounds.end(), [](const auto& s) { return s.id == 2; });
}
void Controller::halt() {
    input_.configure(config_.keys, ++generation_, false);
    renderer_.stop();
    running_ = false;
}
void Controller::startStream() {
    if (!sources_)
        throw std::runtime_error("Load a valid KeyBind.ini before starting");
    halt();
    state_ = L"음원 및 출력 준비 중";
    error_.clear();
    publish();
    try {
        auto info = renderer_.start(settings_.device, settings_.stable, sources_, generation_);
        activeRate_ = info.format.rate;
        retryAfter_ = 0;
        running_ = true;
        counters_.restarts.fetch_add(1);
        streamDetails_ = describeStream(info);
        log(streamDetails_);
        state_ = !info.client3       ? L"일반 공유로 대체"
                 : info.shortened    ? L"저지연 공유"
                 : info.currentKnown ? L"공유 — 주기 단축 없음"
                                     : L"공유 — 현재 주기 확인 불가";
        input_.configure(config_.keys, generation_, true);
    } catch (const std::exception& e) {
        std::string message = e.what();
        error_ = wide(message);
        log(message);
        state_ = message.find("device unavailable") != std::string::npos
                     ? L"장치 연결 대기"
                     : L"오류 — 출력을 시작하지 못했습니다";
    }
}
void Controller::load(const std::filesystem::path& file, bool import) {
    auto candidate = readConfig(file);
    auto bank = std::make_shared<SourceBank>(loadSources(candidate));
    if (running_ && activeRate_) {
        auto validated = convertBank(*bank, activeRate_);
        (void)validated;
    }
    if (import) {
        // Import is a copy, not a persistent reference to another computer/folder.
        // Validate and copy everything before replacing the live portable INI.
        auto relative = std::filesystem::path(L"HitSounds") / (L"imported-" + std::to_wstring(qpc()));
        auto destination = root_ / relative;
        if (!std::filesystem::create_directories(destination))
            throw std::runtime_error("Cannot create imported sound directory");
        std::ostringstream text;
        text << "// UTF-8; paths are relative to this KeyBind.ini.\n[Sound Set]\n";
        for (const auto& sound : candidate.sounds) {
            auto name = std::to_wstring(sound.id) + L".wav";
            std::filesystem::copy_file(sound.path, destination / name);
            text << sound.id << ": " << utf8((relative / name).generic_wstring()) << '\n';
        }
        text << "\n[KeyBind]\n";
        for (size_t key = 0; key < candidate.keys.size(); ++key)
            if (candidate.keys[key])
                for (const auto& [name, code] : keyNames())
                    if (code == key) {
                        text << name << ": " << candidate.keys[key] << '\n';
                        break;
                    }
        auto target = root_ / L"KeyBind.ini";
        auto imported = parseConfig(text.str(), target);
        auto temp = root_ / L"KeyBind.import.tmp";
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text.str();
        out.close();
        if (!out ||
            !MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot save imported KeyBind.ini beside the executable");
        candidate = std::move(imported);
    }
    // Decode every file before touching the current stream/configuration.
    halt();
    config_ = std::move(candidate);
    sources_ = std::move(bank);
    settings_.config = config_.file;
    error_.clear();
    save();
    if (desired_)
        startStream();
    else
        state_ = L"중지";
}
void Controller::drainDiagnostics() {
    DiagnosticRecord r{};
    for (size_t count = 0; count < 16384 && diagnostics_.pop(r); ++count) {
        if (rows_ >= 500000) {
            records_.close();
            auto path = root_ / L"timings.csv", old = root_ / L"timings.previous.csv";
            MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
            records_.open(path, std::ios::binary | std::ios::trunc);
            rows_ = 0;
        }
        if (!rows_)
            records_ << "kind,generation,qpc,padding_frames,written_frames,render_ticks,event_interval_ticks,"
                        "input_to_render_ticks,sequence\n";
        records_ << r.kind << ',' << r.generation << ',' << r.at << ',' << r.padding << ',' << r.written
                 << ',' << r.duration << ',' << r.interval << ',' << r.inputWait << ',' << r.sequence << '\n';
        ++rows_;
    }
    records_.flush();
}
void Controller::run() {
    try {
        ComScope com;
        defaults();
        std::error_code cleanupError;
        std::filesystem::remove(root_ / L"timings.previous.csv", cleanupError);
        log_.open(root_ / L"session.log", std::ios::binary | std::ios::trunc);
        records_.open(root_ / L"timings.csv", std::ios::binary | std::ios::trunc);
        log("TaikoKeyWASAPI 0.2.2; build=" __DATE__ " " __TIME__ "; x64; QPC frequency=" +
            std::to_string(qpcFrequency()));
        using VersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        auto versionFn =
            reinterpret_cast<VersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        if (versionFn) {
            RTL_OSVERSIONINFOW v{};
            v.dwOSVersionInfoSize = sizeof(v);
            if (versionFn(&v) == 0)
                log("Windows=" + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) +
                    " build " + std::to_string(v.dwBuildNumber));
        }
        if (!log_ || !records_)
            error_ = L"진단 로그 파일을 기록할 수 없습니다.";
        input_.start();
        try {
            load(settings_.config);
        } catch (const std::exception& e) {
            error_ = wide(e.what());
            state_ = L"설정 오류 — 파일을 수정하거나 가져오세요";
        }
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT enumResult =
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        ComPtr<DeviceNotifications> notify;
        auto subscribe = [&] {
            if (notify && enumerator)
                enumerator->UnregisterEndpointNotificationCallback(notify.Get());
            notify.Reset();
            if (enumerator) {
                notify.Attach(new DeviceNotifications(wake_.get(), deviceDirty_, settings_.device));
                if (FAILED(enumerator->RegisterEndpointNotificationCallback(notify.Get())))
                    log("Device notifications unavailable; use Refresh");
            }
        };
        if (FAILED(enumResult))
            log("Device enumerator unavailable; audio service may be stopped");
        subscribe();
        auto refresh = [&] {
            try {
                auto devices = enumerateDevices();
                std::lock_guard lock(mutex_);
                snapshot_.devices = std::move(devices);
            } catch (const std::exception& e) {
                log(e.what());
            }
        };
        refresh();
        publish();
        bool quit = false;
        while (!quit) {
            WaitForSingleObject(wake_.get(), 250);
            std::deque<Command> pending;
            {
                std::lock_guard lock(mutex_);
                pending.swap(commands_);
            }
            for (const auto& command : pending)
                try {
                    switch (command.type) {
                    case CommandType::Reload:
                        load(settings_.config);
                        break;
                    case CommandType::Import:
                        load(command.text, true);
                        break;
                    case CommandType::Device:
                        settings_.device = command.text;
                        subscribe();
                        save();
                        if (desired_)
                            startStream();
                        break;
                    case CommandType::Period:
                        settings_.stable = command.flag;
                        save();
                        if (desired_)
                            startStream();
                        break;
                    case CommandType::Refresh:
                        refresh();
                        if (desired_ && !running_)
                            startStream();
                        break;
                    case CommandType::Resume:
                        if (desired_)
                            startStream();
                        break;
                    case CommandType::Shutdown:
                        quit = true;
                        desired_ = false;
                        halt();
                        break;
                    }
                } catch (const std::exception& e) {
                    error_ = wide(e.what());
                    log(e.what());
                    if (!running_)
                        state_ = L"오류";
                }
            if (deviceDirty_.exchange(false) && !quit) {
                refresh();
                retryCount_ = 0;
                if (desired_)
                    startStream();
            }
            if (renderer_.failed() && running_) {
                error_ = wide(renderer_.error());
                log(renderer_.error());
                halt();
                state_ = L"장치 연결 대기";
                retryCount_ = 0;
                retryAfter_ = qpc() + qpcFrequency() * 2;
            }
            if (desired_ && !running_ && retryAfter_ && qpc() >= retryAfter_ && !quit) {
                startStream();
                ++retryCount_;
                retryAfter_ = !running_ && retryCount_ < 3 ? qpc() + qpcFrequency() * 2 : 0;
            }
            drainDiagnostics();
            publish();
        }
        if (notify && enumerator)
            enumerator->UnregisterEndpointNotificationCallback(notify.Get());
        input_.stop();
        save();
        drainDiagnostics();
        log("Final counters: overflow=" + std::to_string(counters_.overflow.load()) +
            " stolen=" + std::to_string(counters_.stolen.load()) +
            " restarts=" + std::to_string(counters_.restarts.load()) +
            " renderOverruns=" + std::to_string(counters_.renderOverruns.load()) +
            " diagnosticDrops=" + std::to_string(counters_.diagnosticDrops.load()));
    } catch (const std::exception& e) {
        error_ = wide(e.what());
        state_ = L"오류";
        halt();
        input_.stop();
        publish();
    }
}
} // namespace taiko
