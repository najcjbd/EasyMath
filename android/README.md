# EasyMath · Android 版

界面 Java（原生 framework，零第三方依赖）+ WebView 里用 **KaTeX 离线渲染 LaTeX**；
数学核心是与桌面端**完全同一份** `../src/*.cpp`，经 JNI 调用。

| 项 | 值 |
|---|---|
| APK 体积 | 51 MB（内置 CPython 3.10 + SymPy 1.14 + mpmath + 三 ABI + KaTeX） |
| minSdk / targetSdk | 24（Android 7.0+）/ 36 |
| 包名 | `com.easymath.app` |
| 求解引擎 | **与桌面端相同**：内嵌 SymPy（Chaquopy），不可用时自动退回内置引擎 |

## 构建

```bash
cd android
JAVA_HOME=/usr/lib/jvm/java-21-openjdk-arm64 gradle --no-daemon assembleDebug
# 产物: app/build/outputs/apk/debug/app-debug.apk  (调试签名, 可直接安装)
```

## 本机（aarch64）特有的坑（已在配置里解决，换机器时注意）

1. **AGP 与 Gradle 必须配套**：本机 Gradle 9.3.1 → 用 **AGP 9.1.0**
   （AGP 9.2.1 要求 Gradle ≥ 9.4.1，AGP 9.3.1 要求 ≥ 9.5.0）。
2. **aapt2 只有 x86_64 版**（Google 不提供 Linux aarch64），本机跑不了：
   - `aapt2-launcher.c` 编译成 aarch64 ELF（`aapt2-arm64`），内部 `execv` box64 去跑真 aapt2；
   - AGP 会校验该文件必须是 ELF，**且文件名必须叫 `aapt2`** → 放在 `aapt2-tools/aapt2`；
   - `gradle.properties` 里 `android.aapt2FromMavenOverride=.../aapt2-tools/aapt2`。
   - （发行版自带的 aapt2 是 build-tools 29 的老版本，不认识 AGP 传的
     `--no-proguard-location-reference`，不能用。）
3. **NDK 路径**：本机目录叫 `android-ndk-r29`，而 AGP 按 `<sdk>/ndk/<版本号>` 查找 →
   需要 `ln -s android-ndk-r29 29.0.14206865`。注意该名字必须原先不存在，
   否则 `ln` 会把符号链接创建到目录*内部*（我就踩过这个坑）。
4. 本环境 NDK 的 `linux-x86_64` 工具链已被替换为 aarch64 原生二进制，可直接使用。

## 安装到手机

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```
或把 APK 拷进手机点击安装（需在系统设置里允许"安装未知来源应用"）。

## 界面与外观

界面是**纯代码搭建**（不依赖 AppCompat / Material，构建快），但按 Material 的视觉语言手绘：

* **配色**：单一主色(蓝) + 冷灰底 + 白色卡片；`res/values/colors.xml`（浅色）与
  `res/values-night/colors.xml`（深色）分开，系统切深色时自动跟随（含结果页里的公式）。
* **主题**：`res/values/themes.xml` / `values-night/themes.xml` 定义 `Theme.EasyMath`，
  状态栏与导航栏跟随背景色，无 ActionBar（自己画头部）。
* **形状**：卡片 16dp 圆角 + 2dp 投影；输入框 12dp 圆角（去掉了默认下划线）；
  模式选择是胶囊 chip（选中 = 主色淡底 + 主色描边）；符号键是 10dp 圆角键帽；
  主按钮实心、次按钮描边，禁用态明显变浅。
* **结果页**：样式在 `assets/result.css`，WebView 与预览截图**共用同一份**；
  长公式（高次多项式、嵌套根式）可横向滚动不裁切；有友好的空状态（点示例即填入输入框）。
* **系统栏适配**：targetSdk 36 是强制 edge-to-edge，界面用 `WindowInsets` 自行留出
  状态栏/导航栏/输入法的高度，不会被挡住。

### 外观预览

`docs/` 下有 5 张预览图（`ui-preview-light/dark.png` 是整屏、`ui-result-light/dark.png` 是结果页、
`ui-empty-light.png` 是空状态）。**诚实说明**：

* 结果页两张是**真实渲染**——用的是应用自己的 `assets/result.css` + 真正的 KaTeX 资源，
  只是用无头 Firefox 代替了 WebView 来截图，所以与手机上一致；
* 整屏两张是**HTML 模拟的版式示意**（同样的颜色/尺寸/圆角），不是真机截图：
  本机既没有 `/dev/kvm` 也没有真机，Android 布局无法在此运行，真机上的字体度量、
  系统栏高度可能与示意图略有差异。

## 界面功能

- 五个模式：拉格朗日插值 / 解方程 / 求值·开方 / 过原点直线 / **字形（文字/手绘 → 函数）**
- 字形模式：输入文字（中英数字、符号、角标都行），或点「手绘」在 1024×1024 的方格里手写
  （多指、可撤销/清空，按**笔画中心线**还原）；「导入字体包」选一个 .zip/.ttf（拷进应用私有目录），「选择字体」可从包里挑一个
  （默认就是你那套 vivo Sans 的 Regular，清单由 C++ 侧列出）；
  左下角坐标可写 `(10,20)` / `10 20` / `10,20`，前缀 `:` 左右镜像；结果里带自包含 SVG 预览。
  字体包解析、曲线拟合、SVG 生成都在 C++ 侧（与桌面端同一份代码），APK 内不额外依赖字体库。
  设置里可调「字号」（对应桌面端 `--size`）与「拟合容差」（对应 `--fit-tol`）。
- 顶部标题栏 + 空状态示例（点示例直接填入输入框）
- 数学符号快捷键盘：`√ ∛ ⁴ ² ⁿ ⁻ × ÷ π ° ∠ ( ) ^ = ,` 以及 `x y`
- 结果区用 KaTeX 渲染 LaTeX，也可切"纯文本"
- 小数位可选 4~20；可显示步骤；输入框不带自动纠错/首字母大写（数学输入更省心）
- 结果卡片带引擎徽标（SymPy / 内置），状态行有彩色圆点（进行中/成功/错误）
- 「保存HTML」写入应用私有目录；「分享」把结果文本发给任意 App
  （保存时把已渲染的结果连同样式/字体一起内联，离线单独打开也能正常显示公式）
- 解方程卡片里可填「常数（不求解的字母）」（如 `a,b,c`）与「求值（由解反推）」（如 `abc,xy`）；
  解的**分组**与组内**比例**会作为附加信息跟在整段结果最后（`分组(取决于 c): a, b` / `比例: a : b = ...`）
- 滚动：表单整体滚动、结果卡片内部单独滚动（自绘 `PageScrollView` 做手势分流），
  长解不会把上面的表单顶出屏幕
- **「打 断」按钮**：长计算（高次求根、超高精度求值、SymPy 求解）跑起来后随时可点，
  立即恢复界面可用并按请求编号作废在途结果；同时请求 C++ 内核停止计算。
  内嵌 SymPy 是**同进程**调用，没有官方的线程中断 API，因此正在执行的 Python 求解
  无法强杀（点完最多等一两秒收尾），但内置引擎的长循环、MPFR 求根/求值、
  以及尚未启动的 Python 调用都会立刻停止。

## 内嵌 SymPy（功能对齐桌面端）

用 [Chaquopy](https://chaquo.com/chaquopy/) 把 CPython + SymPy + mpmath 打进 APK：

```groovy
plugins { id 'com.chaquo.python' }              // 17.0.0
android.defaultConfig.python {
    buildPython '/usr/bin/python3'
    pip { install 'sympy' }
}
```

- Python 桥模块由 `tools/gen_python_module.sh` 从桌面端二进制导出
  （`EasyMath --dump-engine-script`），确保**脚本只有一份来源**，不会两边漂移；
  导出时把末尾的 `main()` 包进 `if __name__ == "__main__"`，并追加 `run_main(joined_args)` 入口。
- C++ 侧通过 JNI 回调 `NativeBridge.runPython()` → Chaquopy 在**进程内**执行，
  不需要 `python3` 可执行文件（Android 上也 exec 不了）；
  这套注入点在 `src/engine.hpp` 的 `setPythonRunner()`，桌面端仍走 fork/exec，
  所以两端行为一致、桌面测试全部照旧通过。
- 于是手机上也有：精确根式（`x⁴=5 → ±5^(1/4)`）、三角一般解集
  （`sin x=½ → 2nπ+π/6`）、多元精确解、因式分解、`--decimals` 到 200 位。
- **注意**：Chaquopy 在进程内跑 Python，没有桌面端那 15 秒超时保护，
  极端复杂的方程可能让界面卡住（目前未加中断机制）。

### 一个构建坑

Chaquopy 17 与 AGP 9.1 **兼容**，但它的 `python { }` 扩展在 **Kotlin DSL 下解析不到**
（`Unresolved reference 'python'`）→ **app 模块改用 Groovy DSL**（`build.gradle`）即可，
动态派发能找到该扩展。根模块仍可用 `.kts`。

## 已知限制 / 下一步

- 未编 GMP/MPFR：C++ 核心用自带大整数（任意精度，超大数较慢），
  高精度数值求根走 SymPy/mpmath 而非 MPFR —— 结果精度等价，路径不同。
  如需与桌面端逐字节一致，可再用 NDK 交叉编译 GMP/MPFR/MPC 三个库。
- 本机没有 `/dev/kvm` 也没有真机，**APK 只验证到"构建成功 + 静态检查"**，
  运行时需要你在手机上试（首次用 SymPy 会有 1~3 秒 Python 启动）。
- 桌面端的精度/打断测试无法直接在手机上跑，但核心是同一份 `src/*.cpp`；
  桌面端 `tests/test_precision.py`(674 项) 与 `tests/test_interrupt.py`(10 项) 全绿。
