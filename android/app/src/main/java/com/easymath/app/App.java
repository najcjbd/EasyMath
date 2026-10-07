package com.easymath.app;

import android.util.Log;

/**
 * 应用入口: 继承 Chaquopy 官方的 PyApplication, 它会用 AndroidPlatform 启动内嵌 Python。
 *
 * 为什么必须有这个类: Chaquopy 17 的 Python.getInstance() 在 Python 未启动时会拿
 * GenericPlatform 去启动, 而 Android 上这是非法的(抛 "Cannot use GenericPlatform on
 * Android"), 结果所有 Python 调用都失败 —— 真机上表现为"引擎一直是内置、没有根式通解、
 * 没有 mpmath 任意精度", 而且异常被 Java 侧吞掉只剩一句 "python returned null"。
 *
 * 这里额外兜住启动异常: 万一某台设备启动 Python 失败, 也只会退回内置引擎(并在
 * 「设置 → 后端自检」里显示原因), 而不是开屏崩溃。
 */
public class App extends com.chaquo.python.android.PyApplication {
    private static final String TAG = "EasyMath";

    /** Python 启动失败时的原因(供自检显示); 为空表示正常 */
    public static volatile String startupError = "";

    @Override
    public void onCreate() {
        try {
            super.onCreate(); // Python.start(new AndroidPlatform(this))
        } catch (Throwable t) {
            startupError = t.getClass().getSimpleName()
                    + (t.getMessage() == null ? "" : ": " + t.getMessage());
            Log.e(TAG, "内嵌 Python 启动失败, 将退回内置引擎", t);
        }
    }
}
