#include "app/Controller.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <winternl.h>

namespace taiko {
void Controller::shutdown() {
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
            if (key == "diagnostics")
                settings_.diagnostics = value == "1";
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
        << "\ndiagnostics=" << settings_.diagnostics << "\nvolume=" << volume_.load() << "\n";
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
    snapshot_.config = config_;
    snapshot_.bindings = wide(bindingsText(config_));
    snapshot_.running = running_;
    snapshot_.warning = warning_;
    snapshot_.warningId = warningId_;
    snapshot_.revision = ++revision_;
    snapshot_.previewDon =
        std::any_of(config_.sounds.begin(), config_.sounds.end(), [](const auto& s) { return s.id == 1; });
    snapshot_.previewKat =
        std::any_of(config_.sounds.begin(), config_.sounds.end(), [](const auto& s) { return s.id == 2; });
}
void Controller::halt() {
    input_.configure(config_.keys, ++generation_, false);
    renderer_.stop();
    running_ = fallback_ = false;
}
void Controller::startStream(const char* reason) {
    if (!sources_)
        throw std::runtime_error("Load a valid KeyBind.ini before starting");
    halt();
    state_ = L"음원 및 출력 준비 중";
    error_.clear();
    publish();
    log(std::string("Opening output; reason=") + reason +
        "; selected=" + (settings_.device.empty() ? std::string("Default") : utf8(settings_.device)));
    try {
        auto info = renderer_.start(settings_.device, settings_.stable, sources_, generation_);
        activeRate_ = info.format.rate;
        retryAfter_ = 0;
        running_ = true;
        counters_.restarts.fetch_add(1);
        streamDetails_ = describeStream(info);
        log(streamDetails_);
        fallback_ = !info.client3;
        if (fallback_) {
            log(lowLatencyFailureLog(info));
            // Alert once per device and cause; reconnects with the same result only log.
            auto key = utf8(info.deviceId) + "|" + std::to_string(info.lowLatencyResult);
            if (key != lastFallback_) {
                lastFallback_ = key;
                warning_ = lowLatencyFailureAlert(info);
                ++warningId_;
            }
        } else
            lastFallback_.clear();
        state_ = !info.client3       ? L"저지연 사용 불가 — 일반 공유 모드"
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
        auto destination = root_ / L"HitSounds" / (L"imported-" + std::to_wstring(qpc()));
        if (!std::filesystem::create_directories(destination))
            throw std::runtime_error("Cannot create imported sound directory");
        for (auto& sound : candidate.sounds) {
            auto copy = destination / (std::to_wstring(sound.id) + L".wav");
            std::filesystem::copy_file(sound.path, copy);
            sound.path = copy;
        }
        candidate.file = root_ / L"KeyBind.ini";
        auto text = configText(candidate);
        auto imported = parseConfig(text, candidate.file);
        writeConfigFile(text, imported.file);
        candidate = std::move(imported);
    }
    apply(std::move(candidate), std::move(bank));
}
void Controller::storeBindings(KeyConfig config) {
    config.file = root_ / L"KeyBind.ini";
    for (auto& sound : config.sounds)
        sound.path = portableSound(sound.path);
    auto text = configText(config);
    auto candidate = parseConfig(text, config.file);
    auto bank = std::make_shared<SourceBank>(loadSources(candidate));
    if (running_ && activeRate_) {
        auto validated = convertBank(*bank, activeRate_);
        (void)validated;
    }
    writeConfigFile(text, candidate.file);
    log("Keybinds saved from the editor");
    apply(std::move(candidate), std::move(bank));
}
void Controller::apply(KeyConfig config, std::shared_ptr<SourceBank> bank) {
    // Every file is decoded before touching the current stream/configuration.
    halt();
    config_ = std::move(config);
    sources_ = std::move(bank);
    settings_.config = config_.file;
    error_.clear();
    save();
    if (desired_)
        startStream("configuration loaded");
    else
        state_ = L"중지";
}
static bool sameContents(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (std::filesystem::file_size(a) != std::filesystem::file_size(b))
        return false;
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    return std::equal(std::istreambuf_iterator<char>(x), {}, std::istreambuf_iterator<char>(y));
}
std::filesystem::path Controller::portableSound(const std::filesystem::path& source) {
    auto path = std::filesystem::absolute(source).lexically_normal();
    auto relative = path.lexically_relative(root_);
    if (!relative.empty() && *relative.begin() != L"..")
        return path; // already beside the EXE
    // Keep the app folder self-contained: copy, reusing an identical file of the same name.
    auto folder = root_ / L"HitSounds";
    std::filesystem::create_directories(folder);
    for (int n = 1; n < 1000; ++n) {
        auto name =
            n == 1 ? path.filename().wstring()
                   : path.stem().wstring() + L" (" + std::to_wstring(n) + L")" + path.extension().wstring();
        auto target = folder / name;
        if (!std::filesystem::exists(target)) {
            std::filesystem::copy_file(path, target);
            return target;
        }
        if (sameContents(path, target))
            return target;
    }
    throw std::runtime_error("Too many sounds named " + utf8(path.filename().wstring()) + " in HitSounds");
}
void Controller::writeConfigFile(const std::string& text, const std::filesystem::path& target) {
    auto temp = root_ / L"KeyBind.ini.tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out ||
        !MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot save KeyBind.ini beside the executable");
}
std::future<std::string> Controller::saveBindings(KeyConfig config) {
    auto job = std::make_unique<PendingBindings>();
    job->config = std::move(config);
    auto result = job->result.get_future();
    {
        std::lock_guard lock(mutex_);
        if (pendingBindings_)
            pendingBindings_->result.set_value("Superseded by a newer save");
        pendingBindings_ = std::move(job);
    }
    post({CommandType::Bindings, {}, {}});
    return result;
}
void Controller::setRecording(bool enabled) {
    settings_.diagnostics = enabled;
    auto path = root_ / L"timings.csv";
    if (!enabled) {
        recording_.store(false, std::memory_order_relaxed);
        records_.close();
        diagnostics_.discard();
        return;
    }
    if (recording_.load(std::memory_order_relaxed))
        return;
    // Each enable starts a fresh file so the CSV header stays at the top.
    std::error_code ignored;
    std::filesystem::remove(root_ / L"timings.previous.csv", ignored);
    diagnostics_.discard();
    records_.clear();
    records_.open(path, std::ios::binary | std::ios::trunc);
    rows_ = 0;
    if (!records_) {
        // Leave the option off so the checkbox shows that nothing is being recorded.
        records_.close();
        settings_.diagnostics = false;
        log("Cannot open timings.csv; timing records stay off");
        return;
    }
    recording_.store(true, std::memory_order_relaxed);
}
void Controller::drainDiagnostics() {
    DiagnosticRecord r{};
    if (!records_.is_open()) {
        diagnostics_.discard();
        return;
    }
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
        log_.open(root_ / L"session.log", std::ios::binary | std::ios::trunc);
        log("TaikoKeyWASAPI " TAIKO_VERSION "; build=" __DATE__ " " __TIME__ "; x64; QPC frequency=" +
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
        if (!log_)
            error_ = L"session.log를 기록할 수 없습니다. 실행 파일 폴더의 쓰기 권한을 확인하세요.";
        if (settings_.diagnostics)
            setRecording(true);
        else {
            // Timing records are opt-in; remove files left by versions that always recorded.
            std::error_code ignored;
            std::filesystem::remove(root_ / L"timings.csv", ignored);
            std::filesystem::remove(root_ / L"timings.previous.csv", ignored);
        }
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
                notify.Attach(new DeviceNotifications(wake_.get(), deviceChanges_, settings_.device));
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
                    case CommandType::Bindings: {
                        std::unique_ptr<PendingBindings> job;
                        {
                            std::lock_guard lock(mutex_);
                            job.swap(pendingBindings_);
                        }
                        if (!job)
                            break;
                        std::string failure;
                        try {
                            storeBindings(std::move(job->config));
                        } catch (const std::exception& e) {
                            failure = e.what();
                            log("Keybinds not saved: " + failure);
                        }
                        // The editor reloads from the snapshot as soon as the result arrives.
                        publish();
                        job->result.set_value(failure);
                        break;
                    }
                    case CommandType::Device:
                        settings_.device = command.text;
                        subscribe();
                        save();
                        if (desired_)
                            startStream("device selected");
                        break;
                    case CommandType::Period:
                        settings_.stable = command.flag;
                        save();
                        if (desired_)
                            startStream("period selected");
                        break;
                    case CommandType::Diagnostics:
                        setRecording(command.flag);
                        save();
                        break;
                    case CommandType::Save:
                        save();
                        break;
                    case CommandType::Refresh:
                        refresh();
                        // Also retry the low-latency path after a fallback, e.g. once another app released
                        // the device.
                        if (desired_ && (!running_ || fallback_))
                            startStream(running_ ? "manual refresh after low-latency fallback"
                                                 : "manual refresh");
                        break;
                    case CommandType::Resume:
                        if (desired_)
                            startStream("resume");
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
            if (auto changes = deviceChanges_.exchange(0); changes && !quit) {
                refresh();
                // Metadata/list refreshes must not interrupt a healthy stream.
                if (desired_ && sources_ &&
                    ((!running_ && (changes & DeviceAvailabilityChanged)) ||
                     (changes & (SelectedDeviceChanged | DefaultOutputChanged)))) {
                    retryCount_ = 0;
                    startStream(changes & DefaultOutputChanged ? "default output changed"
                                                               : "device availability changed");
                }
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
                startStream("retry after output error");
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
