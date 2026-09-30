#pragma once
#include "audio/DeviceCatalog.h"
#include "audio/WasapiRenderer.h"
#include <deque>
#include <fstream>
namespace taiko {
struct Settings {
    std::wstring device;
    std::filesystem::path config;
    bool stable{};
};
struct Snapshot {
    std::wstring status = L"준비 중", details, error, bindings;
    Settings settings;
    std::vector<Device> devices;
    bool running{}, previewDon{}, previewKat{};
    uint64_t revision{};
};
enum class CommandType { Reload, Import, Device, Period, Refresh, Resume, Shutdown };
struct Command {
    CommandType type;
    std::wstring text;
    bool flag{};
};
class Controller {
    std::filesystem::path root_;
    std::thread worker_;
    Handle wake_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::mutex mutex_;
    std::deque<Command> commands_;
    Snapshot snapshot_;
    PlayQueue requests_;
    DiagnosticQueue diagnostics_;
    Counters counters_;
    std::atomic<float> volume_{.25f};
    std::atomic<uint32_t> deviceChanges_{};
    RawInput input_{requests_, counters_};
    WasapiRenderer renderer_{requests_, diagnostics_, counters_, volume_};
    Settings settings_;
    KeyConfig config_;
    std::shared_ptr<const SourceBank> sources_;
    uint32_t generation_{};
    bool desired_{true}, running_{};
    uint64_t revision_{};
    uint32_t activeRate_{};
    unsigned retryCount_{};
    int64_t retryAfter_{};
    std::string streamDetails_;
    std::ofstream records_, log_;
    uint64_t rows_{};
    std::wstring state_ = L"중지", error_;
    void run();
    void defaults();
    void save();
    void publish();
    void halt();
    void startStream(const char* reason);
    void load(const std::filesystem::path& file, bool import = false);
    void drainDiagnostics();
    void log(const std::string& text);

  public:
    explicit Controller(std::filesystem::path root) : root_(std::move(root)) {}
    ~Controller();
    void start() {
        worker_ = std::thread(&Controller::run, this);
    }
    void post(Command command);
    Snapshot snapshot() {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }
    void setVolume(int percent) {
        volume_.store(float(percent) / 100);
    }
    int volume() const {
        return static_cast<int>(volume_.load() * 100 + .5f);
    }
    void preview(uint32_t id) {
        input_.preview(id);
    }
    const std::filesystem::path& root() const {
        return root_;
    }
};
} // namespace taiko
