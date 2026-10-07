package com.easymath.app;

import android.content.Context;
import android.util.Log;

/** 与 C++ 核心(桌面端同一份源码)的 JNI 接口 */
public final class NativeBridge {
    private static final String TAG = "EasyMath";
    /** 启动内嵌 Python 需要 Context(正常由 PyApplication 启动, 这里只做兜底) */
    private static Context appContext;

    static {
        System.loadLibrary("easymath");
    }

    private NativeBridge() {}

    /** 由 MainActivity 在 onCreate 里调用, 供"兜底启动 Python"使用 */
    public static void init(Context ctx) {
        if (ctx != null) appContext = ctx.getApplicationContext();
    }

    /** 确保 Chaquopy 的 Python 已启动(必须用 AndroidPlatform, 不能用默认的 GenericPlatform) */
    private static void ensurePythonStarted() {
        if (com.chaquo.python.Python.isStarted()) return;
        if (!App.startupError.isEmpty())
            throw new IllegalStateException("内嵌 Python 启动失败: " + App.startupError);
        if (appContext == null) throw new IllegalStateException("Python 未启动且没有 Context");
        com.chaquo.python.Python.start(
                new com.chaquo.python.android.AndroidPlatform(appContext));
    }

    public static native String version();

    /** 后端自检(等价桌面端 --engine-info): 有没有 SymPy、为什么退回内置、long double 位数 */
    public static native String info();

    /** 列出字体包里的字体(安卓"选择字体"对话框): CHOSEN/FONT/PACK 行 */
    public static native String fonts(String fontPack);

    /** 请求中断正在进行的计算 */
    public static native void cancel();

    /**
     * @param mode     lagrange | solve | eval | line | glyph
     * @param input    用户输入
     * @param decimals 小数位数
     * @param realOnly 只求实根
     * @param showSteps 显示步骤/中间信息
     * @param lang     zh | en
     * @param engine   auto | sympy | builtin
     * @param scientific auto | always | never
     * @param numericIneq auto | always | never (不等式数值解集策略)
     * @param constants 视为常量的字母(逗号分隔, 解方程)
     * @param derive    由解反推的表达式(逗号分隔, 解方程)
     * @return JSON: {ok, exitCode, error, title, mode, engine, engineVersion, engineError,
     *                sections:[{chan,title,lines:[{plain,latex}]}]}
     */
    public static native String run(String mode, String input, int decimals, boolean realOnly,
                                   boolean showSteps, String lang, String engine, String scientific,
                                   String numericIneq, String constants, String derive,
                                   String anchor, String fontPack, String strokes,
                                   int glyphSize, double glyphTol, String fontPick,
                                   String fontAxis, String fontInstance);

    /** 可变字体的轴/实例清单(机器可读), 供安卓端选择变体 */
    public static native String variations(String fontPack, String fontPick);

    /**
     * 由 C++ 侧回调: 在进程内用 Chaquopy 执行内嵌的 SymPy 桥脚本。
     * @param joinedArgs 参数用 \u001f 连接
     * @return 脚本的 stdout; 失败返回 null
     */
    public static String runPython(String joinedArgs) {
        try {
            ensurePythonStarted();
            com.chaquo.python.PyObject mod =
                    com.chaquo.python.Python.getInstance().getModule("easymath_engine");
            return mod.callAttr("run_main", joinedArgs).toString();
        } catch (Throwable t) {
            // 不要把原因吞掉: C++ 侧看到 \u0001 前缀会把它当成错误信息,
            // 于是「设置 → 后端自检」能直接显示真正的原因(便于真机定位)。
            Log.e(TAG, "runPython failed", t);
            return "\u0001" + describe(t);
        }
    }

    private static String describe(Throwable t) {
        StringBuilder b = new StringBuilder(t.getClass().getSimpleName());
        if (t.getMessage() != null) b.append(": ").append(t.getMessage());
        Throwable c = t.getCause();
        if (c != null && c != t) {
            b.append(" ← ").append(c.getClass().getSimpleName());
            if (c.getMessage() != null) b.append(": ").append(c.getMessage());
        }
        return b.toString();
    }
}
