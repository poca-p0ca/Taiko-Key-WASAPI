#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <objbase.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <windows.h>

namespace taiko {
inline std::wstring wide(const std::string& s) {
    if (s.empty())
        return {};
    int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring result(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), result.data(),
                        n);
    return result;
}
inline std::string utf8(const std::wstring& s) {
    if (s.empty())
        return {};
    int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n, nullptr, nullptr);
    return result;
}
inline int64_t qpc() noexcept {
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return v.QuadPart;
}
inline int64_t qpcFrequency() noexcept {
    LARGE_INTEGER v;
    QueryPerformanceFrequency(&v);
    return v.QuadPart;
}
class Handle {
    HANDLE h_{};

  public:
    explicit Handle(HANDLE h = nullptr) : h_(h) {}
    ~Handle() {
        if (h_)
            CloseHandle(h_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept {
        return h_;
    }
};
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComScope() {
        if (FAILED(hr))
            throw std::runtime_error("COM initialization failed");
    }
    ~ComScope() {
        CoUninitialize();
    }
};
struct PlayRequest {
    uint32_t sound{}, generation{};
    int64_t received{};
    uint64_t sequence{};
};
struct Counters {
    std::atomic<uint64_t> overflow{}, stolen{}, restarts{}, diagnosticDrops{}, renderOverruns{},
        inputDeviceOverflow{};
};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
} // namespace taiko
