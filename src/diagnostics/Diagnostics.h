#pragma once
#include "core/Common.h"
#include "core/SpscQueue.h"
namespace taiko {
// POD records only on the render thread. The controller writes CSV off-thread.
struct DiagnosticRecord {
    uint32_t kind{}, padding{}, written{}, generation{}; // 0=render, 1=input applied
    int64_t at{}, duration{}, interval{}, inputWait{};
    uint64_t sequence{};
};
using DiagnosticQueue = SpscQueue<DiagnosticRecord, 16384>;
} // namespace taiko
