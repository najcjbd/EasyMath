package com.easymath.app;

import android.app.Application;
import android.util.Log;

/**
 * 应用入口: 普通 Application(不再继承 Chaquopy 的 PyApplication)。
 *
 * 原因(1.62.3 修复): PyApplication.onCreate() 会**原生启动 libpython**。在某些设备/系统
 * (例如 iQOO 15 / 新 Android)上这个原生启动会直接崩(SIGSEGV/SIGABRT), Java 的 try/catch
 * 接不住, 表现为"打开先白屏、随后闪退", 而且连崩溃处理器都来不及安装。
 *
 * 现在改为**懒启动**: 打开 App 完全不碰 Python, 只有真正需要 SymPy 的运算才由
 * NativeBridge.ensurePythonStarted() 启动; 启动失败只会退回内置引擎(并在后端自检里显示原因),
 * 不会再让开屏崩掉。
 */
public class App extends Application {
    private static final String TAG = "EasyMath";

    /** Python 启动失败时的原因(供自检显示); 为空表示正常或尚未启动 */
    public static volatile String startupError = "";

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "App.onCreate: Python 懒启动, 开屏不碰 libpython");
    }
}
