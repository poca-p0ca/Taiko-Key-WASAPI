#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
namespace taiko {
template <class T, size_t Capacity> class SpscQueue {
    static_assert(Capacity > 0 && std::is_trivially_copyable_v<T>);
    std::array<T, Capacity + 1> values_{};
    alignas(64) std::atomic<size_t> write_{};
    alignas(64) std::atomic<size_t> read_{};

  public:
    bool push(const T& value) noexcept {
        auto pos = write_.load(std::memory_order_relaxed);
        auto next = (pos + 1) % values_.size();
        if (next == read_.load(std::memory_order_acquire))
            return false;
        values_[pos] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        auto pos = read_.load(std::memory_order_relaxed);
        if (pos == write_.load(std::memory_order_acquire))
            return false;
        value = values_[pos];
        read_.store((pos + 1) % values_.size(), std::memory_order_release);
        return true;
    }
    void discard() noexcept {
        T value{};
        while (pop(value)) {
        }
    }
};
} // namespace taiko
