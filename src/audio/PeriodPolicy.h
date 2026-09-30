#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace taiko {
struct Periods {
    uint32_t normal{}, fundamental{}, minimum{}, maximum{};
};
inline uint32_t choosePeriod(Periods p, bool stable) {
    if (!p.fundamental || !p.minimum || p.maximum < p.minimum || p.normal < p.minimum || p.normal > p.maximum)
        throw std::runtime_error("Invalid device period range");
    uint64_t wanted = stable ? p.normal : p.minimum;
    wanted = ((wanted + p.fundamental - 1) / p.fundamental) * p.fundamental;
    if (wanted > p.maximum)
        throw std::runtime_error("No valid engine period");
    return static_cast<uint32_t>(wanted);
}
inline uint32_t framesToWrite(uint32_t capacity, uint32_t padding, uint32_t target) {
    if (padding > capacity || !capacity)
        throw std::runtime_error("Invalid endpoint padding/capacity");
    return std::min(capacity - padding, target > padding ? target - padding : 0);
}
} // namespace taiko
