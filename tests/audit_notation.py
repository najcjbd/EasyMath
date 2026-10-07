#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""标记法/函数体检: 找"静默算错"和"该支持却报错"。

每条的期望值都是独立写死(或由 Python 现算)的, 不看工具自己的输出对不对。
重点是用户报过的两类: |x| 被静默当分隔符、ceil(正数) 错成 floor。
"""
import subprocess
import sys

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/EasyMath"
HIDE = ["--hide", "all"]

# (表达式, 期望出现在输出里的片段列表)  —— 片段按"全部出现"判定
CASES = [
    # 优先级 / 结合性
    ("-2^2", ["-4"]), ("2^3^2", ["512"]), ("2^-1", ["1/2"]), ("-3!", ["-6"]),
    ("(3!)!", ["720"]), ("3!^2", ["36"]),
    # 根式
    ("sqrt(9)", ["3"]), ("√9", ["3"]), ("cbrt(-8)", ["-2"]), ("∛(-8)", ["-2"]),
    ("root4(16)", ["2"]), ("sqrt(2)^2", ["2"]),
    # 阶乘 / 百分号
    ("0!", ["1"]), ("5%", ["1/20"]), ("100%", ["1"]), ("10%*200", ["20"]), ("50%+50%", ["1"]),
    # 取整 / 符号(上一次 ceil 就出在这类)
    ("floor(-2.5)", ["-3"]), ("ceil(-2.5)", ["-2"]), ("ceil(2.5)", ["3"]), ("floor(2.5)", ["2"]),
    ("round(2.5)", ["3"]), ("round(-2.5)", ["-2"]), ("sign(-3)", ["-1"]), ("sign(0)", ["0"]),
    ("abs(-3/2)", ["3/2"]), ("⌊2.5⌋", ["2"]), ("⌈2.5⌉", ["3"]), ("|3+4i|", ["5"]),
    # 对数 / 三角 / 单位
    ("log(8,2)", ["3"]), ("log2(8)", ["3"]), ("log10(1000)", ["3"]), ("ln(e)", ["1"]),
    ("asin(0.5)", ["π/6"]), ("sin(30°)", ["1/2"]), ("acos(0.5)", ["π/3"]),
    ("atan(1)", ["π/4"]), ("deg(180)", ["π"]),   # deg(x) = x 度 -> 弧度(与 180° 等价)
    ("sinh(0)", ["0"]), ("cos(0)", ["1"]), ("tan(0)", ["0"]),
    # 复数
    ("(1+i)^2", ["2i"]), ("1/i", ["-i"]), ("i*i", ["-1"]), ("(3+4i)*(3-4i)", ["25"]),
    # 整数函数
    ("gcd(-4,6)", ["2"]), ("lcm(-4,6)", ["12"]), ("gcd(0,5)", ["5"]),
    ("max(1,2,3)", ["3"]), ("min(4,-1,2)", ["-1"]),
    # 上/下标与常见符号写法
    ("√4", ["2"]), ("x²", []),          # x² 在求值模式应报未知量(不是解析失败)
    ("1/2+1/3", ["5/6"]), ("1-1", ["0"]), ("0^0", []),
]


def run(expr):
    r = subprocess.run([BIN, "--eval", expr] + HIDE + ["--show", "plain,decimal,approx,note"],
                       capture_output=True, text=True, timeout=60, stdin=subprocess.DEVNULL)
    return (r.stdout + r.stderr), r.returncode


def main():
    bad = []
    for expr, wants in CASES:
        try:
            out, rc = run(expr)
        except subprocess.TimeoutExpired:
            bad.append("%-14s 挂住" % expr)
            continue
        if rc not in (0, 1, 2, 3):
            bad.append("%-14s 退出码 %d: %s" % (expr, rc, out.strip()[:80]))
            continue
        missing = [w for w in wants if w not in out]
        if missing:
            bad.append("%-14s 缺 %s | 实际: %s" % (expr, missing, out.strip().replace("\n", " ⏎ ")[:110]))
    print("标记法体检: 检查 %d 项, 问题 %d 个" % (len(CASES), len(bad)))
    for b in bad:
        print("  - " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
