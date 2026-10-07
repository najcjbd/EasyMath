# EasyMath 快速上手

安装位置：`/opt/EasyMath/bin/EasyMath`

```bash
export PATH=/opt/EasyMath/bin:$PATH      # 想直接敲 EasyMath 就加进 ~/.bashrc
EasyMath --help                          # 全部选项
EasyMath --engine-info                   # 看当前用的求解引擎/数值后端
```

---

## 1. 交互模式（不想记参数就用它）

直接运行 `EasyMath`：

```
 1) 拉格朗日插值   2) 解方程   3) 求值/开方   4) 过原点直线
 5) 配置           6) 帮助     0) 退出
请选择 [0-6]: 1
请输入数据点 (如 x=1,y=3 x=2,y=5 x=3,y=9): x=1,y=3 x=2,y=5 x=3,y=9
...
是否保存为 HTML (Markdown 内嵌)? [Y/n] y
已保存: 函数 2026-9-18 1.html  以及 函数 2026-9-18 1.md
```

- `5` 是配置菜单：用 `键=值` 改任意配置，输入 `save` 写入 `~/.easymath.conf`。
- 退出：`0` / `Ctrl+D`；运算中 `Ctrl+C` 会安全退出（退出码 130），不会留半个文件。

## 2. 非交互模式

| 模式 | 短选项 | 例子 |
|---|---|---|
| 拉格朗日插值 | `-l` / `--lagrange` | `EasyMath -l "x=1,y=3 x=2,y=5 x=3,y=9"` |
| 解方程 | `-s` / `--solve` | `EasyMath -s "x^4=5"` |
| 求值/开方 | `-e` / `--eval` | `EasyMath -e "5! + 6^8"` |
| 过原点直线 | `-L` / `--line` | `EasyMath -L "45°"` |

同时给两个模式会报错（退出码 2）；只给模式不给输入会自动进交互模式补全。

### 实例（真实输出）

```bash
$ EasyMath --lagrange "x=1,y=3 x=2,y=5 x=3,y=9"
数据点: (1, 3), (2, 5), (3, 9)
P(x) = x^2 - x + 3
简写: P(x) = x² - x + 3
y = x^{2} - x + 3
P(1) = 3 ✓   P(2) = 5 ✓   P(3) = 9 ✓

$ EasyMath --solve "x^4=5"
x_1 = -5^(1/4)  (≈ -1.49534878)
x_2 = 5^(1/4)   (≈ 1.49534878)
x_3 = -5^(1/4)i (≈ -1.49534878i)
x_4 = 5^(1/4)i  (≈ 1.49534878i)

$ EasyMath --solve "y=5x, y=6z, x=2z"        # 多元一次
解 1: x = 0, y = 0, z = 0

$ EasyMath --solve "x+y=5, x*y=6"            # 非线性组
解 1: x = 2, y = 3
解 2: x = 3, y = 2

$ EasyMath --solve "sin(x)=0.5"              # 一般解集
解集: x = 2·n·π + π/6   (n ∈ ℤ) 或 x = 2·n·π + 5π/6   (n ∈ ℤ)

$ EasyMath --eval "5! + 6^8" "√6" "1/3" "5x6"
5! + 6^8 = 1679736
√(6) = √6          (近似值: √(6) ≈ 2.44948974)
1/3 = 0.(3)        (≈ 0.33333333)
5*1*6 = 30

$ EasyMath --line "45°"      → y = x
$ EasyMath --line "30"       → y = (√3/3)x      (特殊角用精确根式)
$ EasyMath --line "37"       → y ≈ 0.75355405x  (其它角度用近似值)
```

## 3. 输入怎么写都行

| 写法 | 含义 |
|---|---|
| `x⁴` / `x^4` / `x^{4}` | x 的 4 次方 |
| `x1=` / `x_1=` / `x^1=` / `5x=` | 插值里第 1/5 个点 |
| `P1=(1,2)` / `(1,2)` / `[3,4]` | 一个数据点 |
| `×` `·` `÷` `−` `√` `∛` `∠` `π` `°` `≠` | 常见数学符号 |
| `２×３`、`𝐱+𝜶`、`5乘以6`、`根号6`、`45度` | 全角 / 数学字体 / 中文词 |
| `\frac{1}{2}`、`\sqrt[3]{8}`、`\begin{cases}…\end{cases}` | LaTeX |
| `1.5e-3`、`2E10` | 科学计数法 |
| `,` `;` `:` `@` `#` `$` `空格` … | 分隔符（也可 `--sep "@"`） |

乘法歧义自动判断：纯数值里 `5x6 = 30`（x 当乘号）；含未知量的上下文里 `5x` 是 `5·x`、`x1` 是一个整体变量名。

## 4. 输出与保存

- 默认只打印到终端，不写文件；加 `--save` 写 HTML（同时写同名 `.md`）。
- 默认文件名：`函数 2026-9-18 1.html`（同一天自动递增；已有 `…5.html` 就接着生成 `…6.html`）。
- 自定义名字：`--out 我的直线`；重名时默认加 `(1)`，配置 `overwrite = true` 则直接覆盖并提示。
- 目录：`--outdir /path`（默认当前目录）。
- HTML = Markdown 渲染结果 + `$$…$$` 数学公式（可选 MathJax）+ 内嵌 Markdown 源码，方便二次编辑。

```bash
EasyMath --lagrange "x=1,y=3 x=2,y=5" --save --outdir ~/输出
EasyMath --line "45°" --save --out "我的直线"
```

## 5. 常用选项

| 选项 | 作用 |
|---|---|
| `--hide all --show plain,latex` | 精细控制输出内容（通道：plain/latex/decimal/approx/note/step/verify/file/prompt…） |
| `--decimals 30` | 小数位数（0–200） |
| `--real` / `--complex` | 只求实根 / 允许复根（默认允许） |
| `--engine sympy\|builtin\|auto` | 用外部符号引擎还是内置引擎（默认 auto） |
| `--hpfloat mpfr\|sympy\|builtin` | 高精度浮点来源（默认 mpfr） |
| `--scientific auto\|always\|never` | 科学计数法策略（默认 auto，精确值永远不用） |
| `--sci-threshold 12` | \|v\| ≥ 10^n 转科学计数法 |
| `--lang zh\|en` | 界面语言 |
| `--sep "@"` | 自定义分隔符 |
| `--deg` / `--rad` | 直线模式默认角度制 / 弧度制 |
| `--no-config` | 忽略 ~/.easymath.conf |
| `--dump-config` | 打印默认配置（可重定向成配置文件） |

## 6. 配置持久化

```bash
cp /opt/EasyMath/share/doc/EasyMath/easymath.conf.example ~/.easymath.conf
```

配置文件是 `键 = 值` 文本，支持行尾 `#` 注释；也可以交互菜单里 `5` → 改 → `save`。

## 7. 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 计算失败（如除零、方程无解） |
| 2 | 用法错误（模式冲突、未知选项、配置错误） |
| 3 | 输入错误（点数不足、无法解析） |
| 130 | 被 Ctrl+C 中断（安全退出） |

## 8. 想验证安装是否完好

```bash
T=/opt/EasyMath/share/EasyMath/tests
$T/run_cli_tests.sh /opt/EasyMath/bin/EasyMath     # 端到端 118 项
python3 $T/test_precision.py /opt/EasyMath/bin/EasyMath --digits=20,30,50   # 精度逐位对拍
python3 $T/test_interrupt.py /opt/EasyMath/bin/EasyMath                     # 打断机制
python3 $T/difftest.py /opt/EasyMath/bin/EasyMath                           # 差分对拍
```

或者一条命令跑全部七套：`./tests/run_all.sh /opt/EasyMath/bin/EasyMath`
（需要 `mpmath`：`pip install mpmath`）。

## 9. 算到一半想停下来？

按 `Ctrl+C`：

* 交互模式下只取消当前这次计算，回到主菜单，接着用；在菜单上再按一次才退出。
* 非交互模式下立即结束，退出码 `130`。
* 最多等约 10 毫秒，不会等到算完。

Android 版对应界面上的「打 断」按钮。
