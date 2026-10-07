// EasyMath - 中断机制
// 长循环(数值求根/扫描/因子枚举/格式化)会周期性检查该标志;
// 桌面端由 Ctrl+C 触发, Android 端由界面上的"打断"按钮经 JNI 触发。
#pragma once

namespace em {

// 请求中断(信号处理函数里也安全: 只是写一个原子布尔)
void requestInterrupt();
// 清除中断(每次新的计算开始时调用)
void clearInterrupt();
bool interruptRequested();

// RAII: 作用域内自动清除中断标志
struct InterruptScope {
    InterruptScope() { clearInterrupt(); }
    ~InterruptScope() { clearInterrupt(); }
    InterruptScope(const InterruptScope &) = delete;
    InterruptScope &operator=(const InterruptScope &) = delete;
};

} // namespace em
