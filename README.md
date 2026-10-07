# EasyMath

[![Release](https://img.shields.io/github/v/release/najcjbd/EasyMath)](https://github.com/najcjbd/EasyMath/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-green)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20Android-blue)]()

**EasyMath** 是一个用 C++17 写的数学工具：**解方程、拉格朗日插值、求值/开方、过原点直线拟合，
以及最特别的一项——把字体里的字形轮廓还原成函数表达式**（"字形 → 函数"）。
可以在电脑（Windows/Linux，命令行 + 图形预览）和**安卓手机**（APK，带公式渲染）上运行，界面中英双语。

## 它有什么用

| 场景 | 它能做什么 |
|---|---|
| **做题/验算** | 解一元方程、方程组；给出**解题步骤**（移项、判别式、求根公式、因式分解、韦达定理、消元回代） |
| **方程有精确根时给精确值** | `2^x=8` → `x = 3`；`x^2=2` → `x = ±√2`；`1/3` 精确有理数，不会退化成 `0.333…` |
| **要很多位小数** | `--decimals 1000` 能给出**逐位正确**的 π、√2、e（有 mpmath/MPFR 后端时 1000 位与真值完全一致） |
| **拟合/插值** | 拉格朗日插值、过原点直线拟合、多项式与分段函数 |
| **把字体变成函数** | 读取 TrueType/OTF 字体，把字形轮廓还原成函数（`--glyph 中`），可看每个字形的函数个数与包围盒 |
| **字体包管理** | 导入一整个 `.zip` 字体包，正确显示**中文/英文名字**（UTF-8/GBK 都能认），支持可变字体按字重取字形（`--font-axis wght=850`） |
| **排版一致性** | 处理字体的**字距调整（GPOS kern）与连字（GSUB，如 `AC`→一个字形）**，让"文字 → 函数"更贴近真实排版 |
| **复平面** | `|z-1|=2` → 圆；`|z-1|=|z-i|` → 垂直平分线 |

## 快速开始

**安卓**：到 [Releases](https://github.com/najcjbd/EasyMath/releases/latest) 下载最新的
`EasyMath-<version>-android-sympy.apk` 安装（含 SymPy，可给高精度无理值）。

**Windows**：下载 `EasyMath-<version>-windows.tar.gz`，解压后直接运行 `EasyMath.exe`。

**自己编译（Linux/macOS）**：

```bash
git clone https://github.com/najcjbd/EasyMath.git && cd EasyMath
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4 && sudo cmake --install build      # 装到 /opt/EasyMath/bin/EasyMath
```

可选依赖（有则自动启用，没有就用内置实现）：`libgmp-dev`、`libmpfr-dev`、`libmpc-dev`、`python3-sympy`。

**几个例子**：

```bash
EasyMath --solve "x^2-2=0"          # 精确根 ±√2（含解题步骤）
EasyMath --solve "2^x=8"            # x = 3（超越方程的有理根识别）
EasyMath --eval "pi" --decimals 100 # 100 位 π（位数逐位正确）
EasyMath --glyph "中"               # 字形的轮廓 → 函数
EasyMath --font-list                # 看字体包里的字体（含中文名）
```

## 仓库分支

| 分支 | 内容 |
|---|---|
| **`main`**（默认） | **电脑版**：C++ 源码、CMake/Makefile 构建、测试套件、Windows 打包脚本 |
| **`android`** | **安卓版**：Android/Gradle 工程、JNI 桥、WebView 公式渲染、APK 构建配置 |

> 发行版（APK/Windows 压缩包）在 [Releases](https://github.com/najcjbd/EasyMath/releases) 里；仓库只放源码。

## 许可

[MIT](LICENSE) © 2026 najcjbd

---

# EasyMath

一个 C++17 编写的终端数学工具，支持 **双语界面（中文 / English）**、**交互与非交互两种用法**、
**全量可配置的输出通道**，并且对数学输入做了大量"智能归一化"。

数值与求解都可以**复用现成的成熟库**，也可以只用**零依赖的内置实现**：

| 层 | 现成库 | 内置替代 | 差别 |
|---|---|---|---|
| 大整数 / 有理数 | **GMP**（libgmp-dev） | 自带 base-1e9 实现 | 位数无上限，超大系数不再退化为近似 |
| 高精度浮点 / 复数 | **MPFR + MPC** | `long double` | 小数位可到几百位（`--decimals 200`） |
| 符号求解 | **SymPy / mpmath**（pip） | 内置求解器 | 精确根式、一般解集、精确多元解、因式分解 |

三者都是**可选依赖**：编译/运行时会自动探测，缺了就自动退回内置实现，功能仍然完整。

| 引擎 | 依赖 | 能力 |
|---|---|---|
| `builtin` | 无 | 有理根定理 + Durand–Kerner 数值求根、精确线性方程组、代入消元、数值扫描 |
| `sympy` | `python3` + `sympy`（可选安装） | 精确根式/复根、含 `n ∈ ℤ` 的一般解集、精确多元解、因式分解、任意精度小数 |
| `auto`（默认） | — | 有 SymPy 就用，没有或解不出就自动回退到内置引擎 |

```
 1) 拉格朗日插值   2) 解方程   3) 求值/开方   4) 过原点直线
 5) 配置           6) 帮助     0) 退出
```

---

## 0. 拿到源码包后怎么用（三步）

```bash
tar xzf EasyMath-1.0.0-src.tar.gz && cd EasyMath-1.0.0
./install.sh                 # 一键: 查依赖 → 编译 → 安装到 /opt/EasyMath → 自检
export PATH=/opt/EasyMath/bin:$PATH
EasyMath --help              # 或看 QUICKSTART.md
```

不想装到 /opt 就 `./install.sh ~/easymath`；也可以只编译不安装：

```bash
make            # 等价于 cmake 配置 + 编译 (产物在 build/EasyMath)
make test       # 跑端到端测试
make install    # 安装 (PREFIX=/opt/EasyMath 可覆盖)
```

支持 Linux / macOS / Windows(MSYS2 或 mingw 交叉编译)；GMP/MPFR/SymPy 都是可选的，
没有会自动降级，`install.sh` 会把探测结果显示出来。

## 0.1 重新打包发行版

```bash
bash packaging/build-windows.sh                 # 交叉编译 Windows 版(需要 mingw-w64)
bash packaging/make-dist.sh 1.0.0               # 源码包 + Windows 包 + Android 包 + SHA256SUMS
```

`make-dist.sh` 会重新生成 `dist/` 下的四个文件，并把校验和写进 `dist/SHA256SUMS.txt`；
源码包是自包含的（解包后 `cmake -S . -B b && cmake --build b` 即可，已在干净目录验证过）。

## 1. 构建

```bash
# 方式一: CMake
cmake -S . -B build && cmake --build build -j
./build/EasyMath --help

# 方式二: 直接编译
g++ -std=c++17 -O2 -I src src/*.cpp -o EasyMath

# 可选但推荐: 装上高精度与符号引擎
apt-get install -y libgmp-dev libmpfr-dev libmpc-dev   # GMP + MPFR + MPC
pip install sympy                                      # 符号引擎

# CMake 会自动探测并启用, 配置阶段会打印启用了哪些后端:
#   EasyMath: GMP 已启用 (/usr/lib/aarch64-linux-gnu/libgmpxx.so)
#   EasyMath: MPFR/MPC 已启用 (...)
./EasyMath --engine-info      # 查看各后端状态
```

不想用 CMake 时，直接编译也可以（把宏和库加上即启用）：

```bash
# 全内置(零依赖)
g++ -std=c++17 -O2 -I src src/*.cpp -o EasyMath

# 启用 GMP + MPFR/MPC
g++ -std=c++17 -O2 -I src -DEASYMATH_USE_GMP -DEASYMATH_USE_MPFR src/*.cpp \
    -lgmpxx -lgmp -lmpfr -lmpc -o EasyMath
```

外部引擎是**运行时可选**的: 不装 SymPy 时程序功能完整(仅高次方程的根为数值近似、>15 位小数不可用)。

### 1.1 安装到系统目录

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/EasyMath
cmake --build build -j8
cmake --install build            # 需要写权限(否则加 sudo)
```

安装后的目录结构：

```
/opt/EasyMath/
├── bin/EasyMath                      # 可执行文件
├── include/EasyMath/*.hpp            # 头文件(便于自行编译验证)
└── share/
    ├── doc/EasyMath/                 # README、配置示例、Windows 构建脚本
    └── EasyMath/tests/               # 单元测试与三个测试工具
```

想直接敲 `EasyMath` 就把 bin 加进 PATH：

```bash
export PATH=/opt/EasyMath/bin:$PATH        # 或写入 ~/.bashrc
# 或者: ln -s /opt/EasyMath/bin/EasyMath /usr/local/bin/EasyMath
```

配置示例在 `/opt/EasyMath/share/doc/EasyMath/easymath.conf.example`，
复制成 `~/.easymath.conf` 即可生效。

### 1.2 Windows 构建

源码已跨平台（文件层用 `std::filesystem`，引擎桥在 Windows 上用 `CreateProcess` + 匿名管道，信号用 `signal`）。
用 mingw-w64 交叉编译（零依赖，内置数值后端）：

```bash
apt install mingw-w64
./packaging/build-windows.sh          # 产出 EasyMath-x86_64-w64-mingw32.exe 等
# 或手动:
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -I src src/*.cpp -static -o EasyMath.exe
```

想在 Windows 上也用 GMP/MPFR，从 MSYS2（`pacman -S mingw-w64-x86_64-gmp
mingw-w64-x86_64-mpfr mingw-w64-x86_64-mpc`）或 vcpkg 装好后，加上
`-DEASYMATH_USE_GMP -DEASYMATH_USE_MPFR` 与对应 `-l` 库即可；CMake 在 MSVC 下同样能用
（已处理 MSVC 不支持 `%Lf/%Le` 的问题）。

## 2. 四种模式速览

| 模式 | 交互菜单 | 非交互后缀 | 说明 |
|---|---|---|---|
| 拉格朗日插值 | `1` | `-l, --lagrange` | 由若干点求出函数 P(x) |
| 解方程 | `2` | `-s, --solve` | 高次、复数、多元、方程组 |
| 求值 / 开方 | `3` | `-e, --eval` | 5!，6⁸，√6，5x6 … |
| 过原点直线 | `4` | `-L, --line` | 由与 x 轴夹角得到直线 |

```bash
./EasyMath --lagrange "x=1,y=3 x=2,y=5 x=3,y=9"
./EasyMath --solve "x^4=5"
./EasyMath --solve "y=5x, y=6z, x=2z"
./EasyMath --eval "5! + 6^8"
./EasyMath --line "45°"
```

输出示例（拉格朗日）：

```
输入: x=1,y=3 x=2,y=5 x=3,y=9
数据点: (1, 3), (2, 5), (3, 9)
拉格朗日形式: P(x) = Σ y_i · ∏ (x - x_j) / (x_i - x_j)
P(x) = 3·(x - 2)(x - 3)/((1 - 2)(1 - 3)) + 5·(x - 1)(x - 3)/((2 - 1)(2 - 3)) + 9·(x - 1)(x - 2)/((3 - 1)(3 - 2))
P(x) = x^2 - x + 3
简写: P(x) = x² - x + 3
y = x^2 - x + 3
y = x^{2} - x + 3
P(1) = 3  ✓   P(2) = 5  ✓   P(3) = 9  ✓
```

---

## 3. 输入语法（四种模式通用）

### 3.1 角标 / 下标

| 写法 | 含义 |
|---|---|
| `x⁴`、`x^4`、`x^{4}` | x 的 4 次幂 |
| `x₁`、`x_1`、`x1`、`x[1]` | 名为 `x1` 的变量（插值中表示第 1 个点） |
| `5x`、`x5`、`x_5`、`x^5` | 插值模式下表示"第 5 个点的 x" |

### 3.2 运算符与符号

- 乘法：`*`、`×`、`·`、`⋅`、`∗`、`∘`、以及**隐式乘法** `2x`、`3(4+5)`、`2√3`
- 除法：`/`、`÷`、`∕`、`⁄`
- 其它：加减号全角/Unicode 变体、`＝`、`≠≥≤`、`π`、`∠`、`∏`、`∑`、`√`、`∛`、`∜`、`%`、`!`
- 全角字符、数学字母变体（`𝐱`、`𝒙`、`𝑥`、`𝕩` 均识别为 `x`；`𝜶` → `alpha`）
- 中文词：`根号`、`平方根`、`立方根`、`绝对值`、`正弦/余弦/正切`、`圆周率`、`度`、`乘以`、`除以`、`等于` 等
- **LaTeX 子集**：`\frac{}{}`、`\sqrt[]{}`、`\times`、`\cdot`、`\pi`、`\theta`、`x^{4}`、`\begin{cases}…\end{cases}`、`\left( \right)` 等

### 3.3 关键歧义规则（自动判断，不会把未知数当乘号）

- `x` / `y` 出现在**两个数字之间**且当前上下文没有该未知量时（例如求值模式的 `5x6`）才当作乘号 → `30`；
  在含未知量的上下文（解方程、插值）中 `x`、`5x`、`x1` 一律是变量/点编号。
- `2y` = `2*y`，`xy` = `x*y`（未声明的多字母名按单字母乘积理解），
  而 `x1`、`theta`、`pi` 等是**整体名字**（允许角标变量与希腊字母变量）。

### 3.4 分隔符

默认分隔符为**与数学无关的标点**：空格 `,` `;` `:` `@` `#` `$` `?` `~` `` ` `` `'` `"` `\` `|` `、` `，` `；` …；
`|` 特殊一点：**成对的 `|…|` 是绝对值**（`|-3| = 3`，可嵌套 `||-3||`），
只有**出现在式子中间的落单 `|`** 才当分隔符（`x=1|y=2` 仍是两项）；写在**开头却没配尾**的 `|`
会明确报错"没有配对的收尾"，不再静默按分隔符处理。
`.` 只有不在两个数字之间时才作为分隔符（`1.5` 是小数）。也可自定义：

```bash
./EasyMath --solve "y=5x@y=6z@x=2z" --sep "@"
```

---

## 4. 模式细节

### 4.1 拉格朗日插值

支持的写法（可混用且顺序任意）：

```
x=1,y=3 x=2,y=5 x=3,y=9         # 自动编号
x1=1,y1=3 x2=2,y2=5             # 显式编号
5x=1 5y=3                       # 先写序号
x⁴=5 y⁴=7                       # 上标当序号
x_4=5 y_4=7
P1=(1,3) (2,5) [3,9]            # 点对/命名点
```

- 点数不足、坐标矛盾（同一个 x 对应不同 y）、坐标名不认识 → **给出明确错误并安全退出**（不会卡死）。
- 缺坐标的点在有 ≥2 个完整点时会被忽略并给出提示。
- 输出：拉格朗日形式、化简多项式 `P(x)=…`、`y=…`、纯文本、LaTeX、可选验算。

### 4.2 解方程

- 单变量：1 次 → 精确有理根；2 次 → 精确根式（含复数 `±(√3/2)i` 形式）；高次 → 有理根定理 + 降次 + Durand–Kerner 数值求根（复根、重根）。
- 多方程：`y=5x, y=6z, x=2z` → 精确高斯消元，唯一解 / 无解 / 无穷多解（参数化，如 `x = 2t, y = 6t, z = t`）。
- 非线性：代入消元 + 多项式求根，例如 `x+y=5, x*y=6` → `x=3,y=2` 与 `x=2,y=3`；
  `x^2+y^2=25, y=x` → `x = ±5√2/2, y = ±5√2/2`。
- 只给关系式（`y=x^4`）→ 输出变量关系 + 自由变量说明。
- 非多项式（如 `sin(x)=0.5`）→ 区间数值扫描 + 二分，给出近似根并说明。
- `--real` 只求实根，`--complex` 允许复根（默认允许）。
- **关系约束**：`a>b>c>0, a=b+c, 1/a+1/b=1/c` —— 不等式 / 非零约束（`≠`）可以和等式混着写，
  链式写法（`a>b>c>0`）会自动展开成两两关系；解里带上取值范围说明。
- **常数（不求解的字母）**：`--const a,b,c` 表示只解 `x`，`a/b/c` 当常量（例如解 `a*x^2+b*x+c=0`）。
- **求值（由解反推）**：`--derive abc, xy` 在解出未知量后顺便算出这些表达式。
  「已知 `ab=1, a+b=3`，求 `a⁵+b⁵`」这类题就归在**解方程区**（不用另开区）：

  ```text
  $ EasyMath --solve "a*b=1, a+b=3" --derive "a^5+b^5"
      求值: a^5 + b^5 = 123
  ```
- **函数定义（方程+函数类）**：可以直接在输入里写 `f(x)=…`，它不参与求解，而是把后面的
  `f(…)` 展开——**归在解方程区**，不需要另开区（安卓解方程输入框直接这么写就行）：

  ```text
  $ EasyMath --solve "f(x)=x^2-3, f(x)=0"        →  x = -√(3) 或 x = √(3)
  $ EasyMath --solve "g(t)=2t+1, g(t)=7"         →  t = 3
  $ EasyMath --solve "f(x)=x^2-3, f(x)=0, x>0"   →  x = √(3)   （配合约束）
  $ EasyMath --eval  "f(x)=x^2-3, f(2)"          →  2^2 - 3 = 1
  ```

  同名只认**第一次**定义（后面的 `f(x)=0` 是调用）；只给定义不给方程会明确报错。
- **解题步骤**：`--show step` 给课堂过程（一元一次：整理/移项/系数化 1；一元二次：判别式 Δ、
  符号判断、求根公式、因式分解、韦达定理），默认 SymPy 引擎与内置引擎都给；高次不乱写公式。
- **分组与比例**：解里如果还有没确定数值的量，会按"依赖谁"分组，并在同组内给出比值：

  ```text
  $ EasyMath --solve "abc=m, xy=n"                      # a 与 n 依赖不同的量 → 两桌
      分组(取决于 b, c, m): a
      分组(取决于 x, y): n

  $ EasyMath --solve "a>b>c>0, a=b+c, 1/a+1/b=1/c"      # a、b 同取决于 c → 一桌 + 比例
      分组(取决于 c): a, b
      比例: a : b = (√(5) + 3)/2 : (1 + √(5))/2

  $ EasyMath --solve "a=2t, b=3t, c=5t" --const t
      比例: a : b : c = 2 : 3 : 5

  $ EasyMath --solve "a=3u, b=3u, c=6u" --const u          # 纯数值比自动约分
      比例: a : b : c = 1 : 1 : 2

  $ EasyMath --solve "a=t^2, b=t, c=4t" --const t        # 约不掉就提公因子并如实标注
      比例(含未确定量): a : b : c = t : 1 : 4
  ```

  全是确定数值时不打印分组行；比值本身含未确定量时标成「比例(含未确定量)」，
  不会伪装成数值比。分组只是**附加信息**：它走独立的输出通道 `group`，永远排在
  最后，前面那段输出和没有这个功能时逐字一致；不想要就 `--hide group`。
  多解时每条会标 `解 N 分组(...)` / `解 N 比例: ...`。

### 4.2.1 字形模式（文字 → 函数）

```bash
./EasyMath --glyph "你好" --at "(0,0)" --save        # 默认用 ~/Math/vivo_Sans.zip
./EasyMath --glyph "Ai" --font-pack myfont.ttf --fit-tol 2 --size 500
./EasyMath --glyph "中" --at ":(100,50)"             # ':' = 左右镜像
```

- 输出：每个字形若干**参数方程** `x(t), y(t)`（t∈[0,1]，系数能精确就精确），
  x 单调的段再给一条显式 `y = f(x), x∈[xa,xb]`；方块里那句「函数 N 个(字体自带分段 M)」告诉你省了多少。
- 字体：**默认就用 vivo Sans（你提供的 `vivo_Sans.zip` 里那个 `vivoSans-Regular.ttf`，约 7.5MB，中英文都全）**，
  不依赖"包里恰好哪个文件最大"。想换：`--font-list` 先列出包里的字体（会标出默认是哪个），
  再 `--font <序号>` 或 `--font <名字子串>`（同家族里优先选 Regular）。macOS 压缩产生的
  `__MACOSX/._*` 垃圾项会被自动过滤，不占序号。
- 拟合容差 `--fit-tol`（字体单位，默认 0.5）越大函数越少、越"圆滑"；越小越贴合原字。
  无论怎么调，都保证**不比字体自带分段更多**，且偏差不超过容差（否则退回原始分段）。
- 预览：`--save` 除了 HTML/Markdown 还会写出同名 `.svg`，HTML 里也内嵌了 SVG。
- 位置：`--at "(x,x)"` 或写在文字末尾；写成 `(x,x)`、`(x , x)`、`(x x)`、`x x`、`x,x` 都认；
  前缀 `:` 表示左右镜像。锚点是整幅文字的**左下角**。
- 不支持：CFF/OTF 轮廓、字距调整(kerning)、连字；可变字体取默认实例。

### 4.2.2 绝对值与取整写法

`|x|`（绝对值，可嵌套）、`⌊x⌋`（向下取整）、`⌈x⌉`（向上取整）都能直接用，也可以写函数形式
`abs(x)` / `floor(x)` / `ceil(x)`：

```text
$ EasyMath --eval "|-3|+|2|"     →  abs(-3) + abs(2) = 5
$ EasyMath --eval "|2^3-10|"     →  abs(2^3 - 10) = 2
$ EasyMath --solve "|x-1|>2"     →  解集: (-∞, -1) ∪ (3, +∞)
$ EasyMath --eval "⌈1.5⌉"        →  ceil(3/2) = 2
```

### 4.3 求值 / 开方

`5!`、`6⁸`、`√6`、`5x6`、`1/3`、`\frac{1}{2}+\sqrt{6}`、`sin(30°)`、`gcd(12,18)` …
同时给出**精确值**（分数 / 根式）、**小数**（有限小数、循环小数 `0.(3)`）与**近似值**（默认 8 位，可用 `--decimals` 修改）。

### 4.4 过原点直线

输入与 x 轴的夹角：`45°`、`45度`、`45dgree`、`45degree`、`45deg`、`135`、`-45`、`0.5rad`、`$(0.7854)$`…

| 角度 | 输出 |
|---|---|
| 0° | `y = 0` |
| 30° | `y = (√3/3)x` |
| 45° | `y = x` |
| 60° | `y = √3x` |
| 90° | `x = 0`（垂直于 x 轴，斜率不存在） |
| 135° | `y = -x` |
| 150° | `y = (-√3/3)x` |

其它角度用近似值：`y ≈ 0.75355405x`（位数可配 `--decimals`）。
`--deg` / `--rad` 可指定缺省单位（默认角度制）。

---

## 5. 数值后端（GMP / MPFR · 更精确的结果）

```bash
./EasyMath --engine-info
# 符号引擎: SymPy 1.14.0
#   engine   = auto  (auto/builtin/sympy)
# 大整数后端: GMP  (bigint=auto)
# 高精度浮点: MPFR 4.1.0 / MPC 1.2.1  (hpfloat=auto, precision=0)
```

| 配置项 | 取值 | 说明 |
|---|---|---|
| `bigint` | `auto` / `gmp` / `builtin` | 大整数后端。**编译期**决定，`auto` 表示"编进来就用" |
| `hpfloat` | `auto` / `mpfr` / `sympy` / `builtin` | 高精度浮点来源，**运行期**可切换 |
| `precision` | 位数（0=自动） | MPFR 二进制精度；0 表示按 `--decimals` 自动换算（如 30 位小数 → 约 140 bit） |

命令行同名选项：`--bigint=… --hpfloat=… --precision=…`。

**精度/规模上的实际差别**（都是同一份代码，只换后端）：

| 场景 | 内置后端 | GMP / MPFR |
|---|---|---|
| `--eval "√2" --decimals 40` | `1.4142135623730950488016887242096979843472`（第 35 位起错） | `1.4142135623730950488016887242096980785697`（40 位全对） |
| `--solve "x^2=10^200"`（401 位整数开方） | 溢出，只能给近似 | `x = ±10^100`（精确） |
| `--solve "x^4=5" --decimals 30` | 15~19 位有效 | `±1.495348781221220541911898994141`（30 位） |
| `--lagrange` 大系数 | 受限于因子枚举上限 | 因子枚举放宽到 30 位数字 |
| `--line 37 --decimals 40` | long double 上限 | mpmath/MPFR 任意位 |

### 5.0.1 科学计数法

数值输出默认策略 `scientific = auto`：

| 情况 | 输出 |
|---|---|
| **精确值**（整数、分数、有限/循环小数） | 永远定点，不做科学计数法 —— `2^200` 给 61 位精确整数，`1/3` 给 `0.(3)` |
| 近似值且 \|v\| ≥ 10^`sciThreshold`（默认 12） | 科学计数法：`exp(100) ≈ 2.68811714e+43` |
| 近似值且 \|v\| 小到定点会显示成 0 | 科学计数法：`exp(-100) ≈ 3.72007598e-44` |
| 其它 | 定点：`√2 ≈ 1.41421356` |

```bash
--scientific=auto|always|never     # 默认 auto; --no-scientific 等价于 never
--sci-threshold=12                 # |v| ≥ 10^n 转科学计数法
```

科学计数法**也能作为输入**：`1.5e-3`、`2E10`、`1e-20*1e20` 都能正确解析（`2e` 仍表示 `2×e`）。

### 5.0.1.1 精度承诺

* 打印的小数一律是**正确舍入**的结果（偏差 ≤ 0.5 ulp），不是截断；重复小数会给出循环节
  （`1/7 = 0.(142857) ≈ 0.142857…`）。
* 度↔弧度换算、三角函数等在超过 15 位小数时统一走高精度路径，**不使用硬编码的 π 常量**
  （曾因 21 位硬编码常量导致 135° 的弧度值末位出错，现已修复并加了回归测试）。
* SymPy 给出的精确解（含根式、虚数）会**同时**附上高精度数值，且虚部不会被漏掉
  （`x = -i·√(-1/2 + √(17)/2)`，而不是看着像实数的 `-√(1/2 - √(17)/2)`）。
* 纯文本里的分数/小数会按打印形态补括号，保证"打印出来的东西再读回去"语义不变
  （`2^0.5` 打印为 `2^(1/2)`、`1/0.5` 打印为 `1/(1/2)`，不会出现 `2^1/2`、`1/1/2` 这类歧义）。

### 5.0.1.2 两种大数/浮点后端的实测差异

桌面端默认（GMP + MPFR）与"纯内置"后端（Android 及未装 GMP/MPFR 的构建走这条）在同一台机器上的实测：

| 操作 | GMP + MPFR | 纯内置 | 倍数 |
|---|---|---|---|
| `2^30000`（精确 9031 位） | 0.01 s | 0.01 s | 1.0× |
| `10000!`（精确 35660 位） | 0.03 s | 0.23 s | 8.2× |
| `10^100000`（精确 10 万位） | 0.04 s | 0.16 s | 4.5× |
| `10^1000000`（精确 100 万位） | 0.19 s | 15.5 s | 80× |
| `√6` 到 50 位 | 0.02 s | 0.01 s（走 SymPy/mpmath） | — |
| `x^200-2=0`（数值求根） | 3.39 s | 3.39 s | 1.0× |
| `x^4=5` 到 30 位 | 1.95 s | 1.93 s | 1.0× |

规律：**小规模差不多，规模越大差距越夸张**——内置后端是教科书 O(n²) 乘法，GMP 是分治乘法。
数值求根/迭代类算法两边一样快（瓶颈在算法不在大数乘法）。手机上 CPU 通常还要再慢 2~4 倍。

因此精确运算的**规模闸门按后端缩放**：结果位数上限 GMP 取约 200 万位、内置后端取约 30 万位
（估算值 = 底数位数 × 指数），超出就立即报错并建议 `--engine sympy`，而不是开始一个几分钟
都停不下来的运算。`--engine-info` 会报告实际后端。

### 5.0.1.3 精度"不许吹牛"规则

* 配置点名要的高精度后端（`hpfloat=mpfr`）在本构建里不存在时，计算前会**警告**，
  并且**按平台 long double 的可靠位数截断输出**（不再打印后段是噪声的位数）。
  例：无 MPFR 的构建 + `--decimals 50` → 只给约 33 位 + 一行警告；同一构建加
  `--hpfloat=sympy`（需 python3+sympy）立刻拿到正确的 50 位。
* 三角函数/度↔弧度换算的 π 用超出 long double 精度的 40 位常量，内置后端也能算到平台极限
  （曾用 21 位常量：`tan(37°)` 第 21 位起就错，现误差 2.6e-33）。
* Android 端默认 `hpfloat=auto`，会走 SymPy/mpmath 拿到任意精度，不受上面的 33 位限制。

### 5.0.2 各平台差异（`long double` 宽度决定内置后端的有效位）

| 平台 / 编译器 | `long double` | 十进制有效位 | 指数范围 | `printf` 的 `%Lf/%Le` |
|---|---|---|---|---|
| **Linux arm64（本机）** | IEEE binary128 (113 bit) | **33** | ±4931 | 支持 |
| Linux x86-64 / x86 (GCC/Clang) | x87 80-bit (64 bit) | 18 | ±4932 | 支持 |
| **Windows x86/x64 (MSVC)** | 与 `double` 同型 (53 bit) | **15** | ±308 | **不支持** → 已用 `%f/%e` 分支处理 |
| Windows (MinGW-w64, x86) | x87 80-bit | 18 | ±4932 | 支持 |
| macOS arm64 (Apple Silicon) | 与 `double` 同型 | 15 | ±308 | 支持 |
| macOS x86-64 | x87 80-bit | 18 | ±4932 | 支持 |

带来的实际区别（`hpfloat=builtin` 时）：

- 本机 `--decimals 40` 会**自动截到 33 位有效数字**（多印的位是噪声，程序不印）；
  Windows/MSVC 或 macOS-arm64 上则截到 15 位，Linux x86 截到 18 位。
- 想要任意位数就用 **MPFR**（`hpfloat=mpfr`，本机已启用）：`--decimals 200` 也给得出正确数字，与平台无关。
- `--engine-info` 会打印当前后端；`longDoubleIsDouble()` 可判断"long double 是否等于 double"。

> MPFR 路径不启动任何子进程（比走 SymPy 更快），只有在没有编译 MPFR 时才退到 SymPy/mpmath。

## 5.1 求解引擎（复用现成轮子）

`--engine=auto`（默认）会探测 `python3 -c "import sympy"`，可用就把方程交给 SymPy，不可用或 SymPy 给不出闭式解时
自动回退到内置引擎（回退原因会在 `--show step` 时显示）。

```bash
./EasyMath --engine-info                 # 探测结果: 符号引擎 + 数值后端
./EasyMath --engine=sympy  --solve "x^4=5"     # 强制外部引擎
./EasyMath --engine=builtin --solve "x^4=5"    # 强制内置引擎
./EasyMath --python /usr/bin/python3 --engine-timeout 30000 --solve "…"
./EasyMath --dump-engine-script > engine.py    # 内嵌的桥脚本(约 200 行, 可自行审阅/修改)
```

外部引擎带来的差别（同一道题）：

| 命令 | `builtin` | `sympy` |
|---|---|---|
| `--solve "x^4=5"` | `x ≈ ±1.49534878`(数值) | `x = ±5^(1/4)`, `±5^(1/4)i`（精确根式 + 精确复根） |
| `--solve "sin(x)=0.5"` | 区间内 63 个数值根 | `x = 2nπ + π/6  或  x = 2nπ + 5π/6  (n ∈ ℤ)` 并附 LaTeX |
| `--solve "cos(x)=x"` | `x ≈ 0.73908513` | 无闭式解 → 自动回退到内置数值扫描 |
| `--lagrange "x=1,y=0 x=2,y=0 x=3,y=6"` | 只给展开式 | 额外给出 `P(x) = 6x(x-2)` 因式分解 |
| `--line "37" --decimals 25` | long double 上限(~15 位) | `y ≈ 0.7535540501027941570739564x`（mpmath 任意精度） |

实现方式：EasyMath 通过 `fork/exec` 调用 `python3 -`，把内嵌的桥脚本从 stdin 送入、方程作为 argv 传入，
再用制表符分隔的记录协议读回结果，因此**不需要 JSON/HTTP 依赖**，也不会被 shell 转义问题影响；
子进程带超时保护（`--engine-timeout`，默认 15000 ms），超时即 SIGKILL 并回退。
单变量多项式、多元线性/非线性方程组、一般解集合、因式分解分别走 `roots / solve / solveset / factor` 等 SymPy API。

## 5.2 交互模式

直接运行 `EasyMath` 进入菜单；也可 `EasyMath -i` 强制进入。交互模式下：

- 运算结束后会询问 **是否保存为 HTML（Markdown 内嵌）**；
- 输入 `5` 进入配置菜单，可用 `键=值` 修改任意配置并 `save` 写入配置文件；
- `Ctrl+C` / `Ctrl+D` 一律**安全退出**（退出码 130 / 0），不会留下半截文件。

## 5.3 随时打断（Ctrl+C）

长计算（高次求根、大范围数值扫描、外部 SymPy 求解、超高精度求值）跑起来之后可以随时打断：

* **交互模式**：`Ctrl+C` 只取消**当前这一次**计算，打印"计算已中断"并回到主菜单，
  中断标志会被清掉，接着算别的东西不受影响；在菜单上（空闲、等待输入时）再按一次 `Ctrl+C`
  才是安全退出。这样既不会丢会话，也不会误退出。
* **非交互模式**（`--solve` 等）：`Ctrl+C` 立即结束，退出码 `130`，输出"计算已中断"，
  便于脚本判断（`if [ $? -eq 130 ]; then ...`）。
* **Android 版**：界面上的「打 断」按钮等价于 `Ctrl+C`：立即让界面恢复可用（在途结果按请求编号
  作废），并请求内核停止计算；内嵌 SymPy 为正进程内调用，无法强行杀死正在执行的 Python，
  但内置引擎的长循环、MPFR 求根/求值、以及尚未启动的 Python 调用都会立刻收手。

实现要点：中断标志是一个 `std::atomic<bool>`（`src/interrupt.hpp`），在 Durand–Kerner 迭代、
牛顿精化、有理根枚举、数值扫描与二分、多项式 GCD、代入消元、高精度复数迭代、循环小数展开、
质因数化简等所有长循环里按固定间隔检查；外部引擎（SymPy 子进程）在收到中断时
`SIGKILL` 掉子进程并回收。

## 6. 非交互模式（`--后缀`）

- 同时给出两个模式（如 `--solve … --eval …`）→ **报错并退出（码 2）**。
- 只给了模式没给输入（如 `EasyMath --solve`）→ **自动进入交互模式补全**该模式的输入。
- 未知选项 → 报错（码 2）。

退出码：`0` 成功 · `1` 计算失败/无解 · `2` 用法错误/模式冲突 · `3` 输入错误 · `130` 被 Ctrl+C 中断。

## 7. 配置与输出开关

配置文件默认 `~/.easymath.conf`（可用 `--config` 指定，`--no-config` 忽略），格式为 `键 = 值`：

```ini
lang = zh              # zh / en
decimals = 8           # 近似值小数位
saveDir = .            # 保存目录(默认当前目录)
useDefaultName = true  # 是否使用默认命名
nameTemplate = 函数 {date} {n}
overwrite = false      # false: 重名时加 (1); true: 直接覆盖并提示
saveHtml = false       # 非交互默认是否保存
askSave = true         # 交互模式是否询问保存
mathjax = true         # HTML 中引入 MathJax
saveMarkdownAlso = true
degreesDefault = true
realOnly = false
complex = true
separator =

# 输出通道(全部可单独开关): 1 开 / 0 关
out.banner = 1        out.input = 1       out.normalized = 1    out.note = 1
out.step = 0          out.lagrange = 1    out.polynomial = 1    out.plain = 1
out.decimal = 1       out.approx = 1      out.latex = 1         out.markdown = 0
out.html = 0          out.solution = 1    out.verify = 1        out.file = 1
out.prompt = 1        out.tip = 1
```

命令行等价形式：

```bash
./EasyMath --eval "1+1" --hide all --show plain      # 只输出纯文本结果
./EasyMath --lagrange "…" --hide step,verify,note
./EasyMath --set decimals=4 --set "nameTemplate=MyFunc {date} {n}"
./EasyMath --dump-config > ~/.easymath.conf          # 生成默认配置
```

任何一类输出被关闭后，终端输出、Markdown 与 HTML 中都**不会**出现该类内容。

## 8. 保存文件与命名规则

- 默认命名模板 `函数 {date} {n}` → `函数 2026-9-18 1.html`（同时写同名 `.md`）。
  文件名中的日期用 `-` 分隔，因为 `/` 不能出现在文件名里；扫描时 `-`、`.`、`_` 分隔都认。
- 序号 **取同日期已有文件的最大值 + 1**（即使缺号也相信已有文件：存在 `… 2.html` 时下一个就是 `… 3.html`）。
- 自定义文件名（`--out 名字` 或在交互中输入）若已存在：
  - `overwrite = true` → 直接覆盖并提示"已按配置覆盖同名文件"；
  - `overwrite = false` → 依次尝试 `名字(1)`、`名字(2)`… 直到不重名，并提示已改名。
- HTML 是完整的 HTML5 文档：正文为 Markdown 渲染结果 + `$$…$$` 数学（可选 MathJax），
  并把 Markdown 源码内嵌在 `<script type="text/markdown">` 中，方便二次编辑。

## 9. 测试

七套测试，共 **3500+ 项检查**；一条命令跑完全部：

```bash
make test-all                 # 或 ./tests/run_all.sh ./build/EasyMath
./tests/run_all.sh ./EasyMath --quick   # 快速档(对拍/模糊/精度都减少用例)
```

也可以单跑某一套：

| 套件 | 命令 | 规模 |
|---|---|---|
| 核心单元(大数/有理数/归一化/解析/多项式/求解) | `make core-test` | 164 项 |
| 打印-重解析往返(括号优先级/隐式乘法) | `make rt-test` | 72 例 |
| 端到端 CLI(四模式/引擎/后端/配置/健壮性/本轮回归) | `make test` | 132 项 |
| 与独立预言机差分对拍(Python int/Fraction + SymPy) | `make diff-test` | 726 项 |
| **精度**(逐位对照 mpmath, 含正确舍入判定) | `make prec-test` | 839 项 |
| **打断机制**(SIGINT 中途打断的行为承诺) | `make int-test` | 10 项 |
| 模糊测试(内置与 SymPy 两种引擎, 建议配 sanitizer) | `make fuzz-test` | 1600+ 组 |

### 9.1 精度测试（`tests/test_precision.py`）

参考值全部由 **mpmath 以 220 位十进制重算**，不引用 EasyMath 的任何中间结果，因此
"少算了几位""末位截断""悄悄退化成 double/long double"都会直接暴露。四类判定：

1. **正确舍入**：打印值与参考值的偏差必须 ≤ 0.5 ulp（末位截断会到 1 ulp，判定失败）；
   循环小数按截断形态放宽到 1 ulp。
2. **位数达标**：`--decimals N` 时精确根/无理根都要给出 N 位有效小数
   （末位是 0 被裁掉的情况允许 2 位余量）；精确值本身很短（如 `1/2`）不苛求位数。
3. **精确性**：`5!`、`2^1000`、`100!`、`3^500` 等用 Python 大整数比对；
   有理根（`1`、`-2/3`）按根集合逐字比对。
4. **不遗漏 + 双引擎一致**：方程组的每个根（实部与虚部）都要有位数字达标的数值；
   `--engine builtin` 与 `--engine sympy` 在 30 位下的数值集合必须完全相同。

覆盖范围：超越/根式求值（含 `e^π`）、20/30/50 位常规精度、**80/120 位超精**
（远超本机 `long double` 的 33 位，用来证明没有退化）、2~7 次方程求根、
无理数系数方程组、13 个角度的度↔弧度换算、拉格朗日插值系数的 Fraction 独立复算、
科学计数法 `e^±100`。

### 9.1.1 模糊测试

`tests/fuzz_cli.py` 同时支持内置引擎与 `--engine=sympy`，固定语料（Unicode、LaTeX 片段、
畸形分隔符、参数边角等）+ N 组随机用例，逐个检查"崩溃 / 卡死 / sanitizer 报错 / 退出码非法"。
单例超时设为 180 秒：这里要抓的是**真的卡死**，不是比速度——机器繁忙（例如同时跑差分、精度套件）
时短超时会产生假报警（本轮就踩过一次，复测单例最慢 0.79 秒）。

### 9.2 打断测试（`tests/test_interrupt.py`）

对"随时可以打断"的承诺逐条验证，并且先测量每个用例**完整跑完**的耗时作为基线，
以免把"本来就快"误判成"成功打断"：

* 计算中途发 `SIGINT` → 必须在 ~2 秒内停下（实测延迟约 10 毫秒），退出码 `130`；
* 非交互模式给出明确的"计算已中断"提示；
* 交互模式只取消这一次计算、回到菜单，**之后的计算仍然正确**（验证中断标志被清掉）；
* 菜单上（空闲时）按 `Ctrl+C` → 安全退出；
* 不崩溃（无 SIGSEGV/SIGABRT）、子进程全部回收、无僵尸。

对拍/模糊测试覆盖的内容：整数四则与幂（对照 Python int）、有理数与循环小数展开（对照 Fraction 的
独立实现）、拉格朗日插值（独立实现 + 逐点代入）、内置引擎 vs SymPy 的求根结果、打印文本重解析后的语义
等价性，以及 1000+ 组畸形输入（超深嵌套、超长标识符、溢出序号、长循环节、大系数、非法 LaTeX、
Unicode 边界等）下的"不崩溃、不卡死、退出码合法"。

## 10. 配置项补充

```ini
engine = auto          # auto | builtin | sympy
python = python3       # python3 可执行文件
engineTimeout = 15000  # 外部引擎超时(毫秒)
bigint = auto          # auto | gmp | builtin      (编译期决定)
hpfloat = auto         # auto | mpfr | sympy | builtin
precision = 0          # MPFR 精度(位), 0 = 按 decimals 自动
```

## 11. 已知限制

- 内置引擎下：求值中同时出现不同根式（如 `1+√2`）时，精确形式无法用单个"有理数×根式"表示，会退化为近似值并提示；
  小数位超过 ~15 位需要外部引擎（`auto`/`sympy`），否则 long double 精度到顶。
- 外部引擎与高精度库都是可选依赖：换机器后 `apt install libgmp-dev libmpfr-dev libmpc-dev && pip install sympy`
  重新编译即可恢复；没有它们程序仍然可用（自动退回内置实现）。
- `bigint` 是编译期选项：用 `--bigint=builtin` 只会给出提示，实际后端取决于编译时的宏。
- 非多项式方程只扫描 `[scanLo, scanHi]`（默认 `[-100,100]`）范围内的实根。
- 数学上无初等闭式解的方程组会给出"超出解析求解能力"的明确提示，而不是给出错误答案。
- `√(负数)`、`负数的非整数次幂` 需要复数支持，单变量多项式求根支持复根，其它场合会明确报错。
