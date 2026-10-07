#include "interrupt.hpp"

#include <atomic>

namespace em {

namespace {
std::atomic<bool> g_interrupted{false};
}

void requestInterrupt() { g_interrupted.store(true, std::memory_order_relaxed); }
void clearInterrupt() { g_interrupted.store(false, std::memory_order_relaxed); }
bool interruptRequested() { return g_interrupted.load(std::memory_order_relaxed); }

} // namespace em
