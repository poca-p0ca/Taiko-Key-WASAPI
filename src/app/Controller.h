#pragma once
#include "audio/DeviceCatalog.h"
#include "audio/WasapiRenderer.h"
#include <deque>
#include <fstream>
#include <future>
namespace taiko {
struct Settings {
    std::wstring device;
    std::filesystem::path config;
    bool stable{}, diagnostics{};
};
struct Snapshot {
    std::wstring status = L"준비 중", details, error, bindings, warning;
    Settings settings;
    KeyConfig config; // the live configuration, for the Keybinds editor
    std::vector<Device> devices;
    bool running{}, previewDon{}, previewKat{};
    uint64_t revision{}, warningId{}; // warningId changes once per new low-latency fallback
};
enum class CommandType {
    Reload,
    Import,
    Bindings,
    Device,
    Period,
    Diagnostics,
    Save,
    Refresh,
    Resume,
    Shutdown
};
struct Command {
    CommandType type;
    std::wstring text;
    bool flag{};
};
class Controller {
    struct PendingBindings {
        KeyConfig config;
        std::promise<std::string> result;
    };
    std::filesystem::path root_;
    std::thread worker_;
    Handle wake_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::mutex mutex_;
    std::deque<Command> commands_;
    std::unique_ptr<PendingBindings> pendingBindings_;
    Snapshot snapshot_;
    PlayQueue requests_;
    DiagnosticQueue diagnostics_;
    Counters counters_;
    std::atomic<float> volume_{.25f};
    std::atomic<bool> recording_{};
    std::atomic<uint32_t> deviceChanges_{};
    RawInput input_{requests_, counters_};
    WasapiRenderer renderer_{requests_, diagnostics_, counters_, volume_, recording_};
    Settings settings_;
    KeyConfig config_;
    std::shared_ptr<const SourceBank> sources_;
    uint32_t generation_{};
    bool desired_{true}, running_{}, fallback_{};
    uint64_t revision_{};
    uint32_t activeRate_{};
    unsigned retryCount_{};
    int64_t retryAfter_{};
    std::string streamDetails_, lastFallback_;
    std::wstring warning_;
    uint64_t warningId_{};
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
    void storeBindings(KeyConfig config);
    void apply(KeyConfig config, std::shared_ptr<SourceBank> bank);
    std::filesystem::path portableSound(const std::filesystem::path& source);
    void writeConfigFile(const std::string& text, const std::filesystem::path& target);
    void setRecording(bool enabled);
    void drainDiagnostics();
    void log(const std::string& text);

  public:
    explicit Controller(std::filesystem::path root) : root_(std::move(root)) {}
    ~Controller() {
        shutdown();
    }
    void start() {
        worker_ = std::thread(&Controller::run, this);
    }
    // Stops audio/input and persists settings. Safe to call more than once.
    void shutdown();
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
    // Copies sounds from outside the app folder beside the EXE, writes KeyBind.ini and applies it.
    // The future yields an empty string on success, else the error message.
    std::future<std::string> saveBindings(KeyConfig config);
    void captureKeys(HWND target) {
        input_.capture(target);
    }
    const std::filesystem::path& root() const {
        return root_;
    }
};
} // namespace taiko
