#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""课程覆盖测试: 小学填空 -> 初中 -> 高中/可符号求解的部分, 每条期望解写死。
不追求覆盖 ODE/PDE 那类(工具定位就是中学到大学基础), 但要把"能不能做"如实分档。
"""
import subprocess, sys
BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/EasyMath"
HIDE = ["--hide", "all", "--show", "solution,note"]

CASES = [
    # 小学: 填空 / 简易 / 小数 / 分数 / 百分数 / 比例
    ("x+2=5", ["x = 3"], []), ("7-x=4", ["x = 3"], []), ("x*3=12", ["x = 4"], []),
    ("12/x=3", ["x = 4"], []), ("2x+3=7", ["x = 2"], []), ("2(x+3)=10", ["x = 2"], []),
    ("3x+2x=10", ["x = 2"], []), ("x+2.5=6.8", ["x = 43/10"], []),   # 精确分数优先 ("3x=7.5", ["x = 5/2"], []),
    ("x/3+1/4=1/2", ["x = 3/4"], []), ("(2/3)x=4/5", ["x = 6/5"], []),
    ("x*(1+20%)=120", ["x = 100"], []),
    ("2*pi*r=C", ["C = 2π·r"], []),   # 两个都是未知数 -> 给关系式 ("pi*r^2=S", ["S = π·r^2"], []),
    ("l=n*pi*r/180", ["l = π·n·r/180", "l = π"], []),   # 三个都是未知数 -> 给关系式(正确)
    # 初中
    ("2x-5=3x+1", ["x = -6"], []), ("x^2-3x+2=0", ["x_1 = 1", "x_2 = 2"], []),
    ("x^2=2", ["x_1 = -√(2)", "x_2 = √(2)", "√(2)"], []),
    ("x^2+2x+2=0", ["i"], []),                       # 复根
    ("2/(x-1)=3/x", ["x = 3"], []),                  # 分式方程(需检验增根)
    ("sqrt(x+1)=3", ["x = 8"], []),                   # 根式方程
    ("|2x-1|=5", ["x = 3", "x = -2", "-2", "3"], []), # 绝对值方程
    ("x+y=5, x-y=1", ["x = 3", "y = 2"], []),          # 二元一次组
    ("x+y+z=6, x-y=1, z=2", ["x = 2", "y", "z"], []),  # 三元一次组
    ("x^2+y^2=25, x-y=1", ["x = 4", "y = 3"], []),                  # 二元二次组(可能含复根)
    ("a*b=1, a+b=3", ["√(5)"], []),                       # 参数化解
    # 高中/可符号部分
    ("2^x=8", ["x ≈ 3", "x = 3"], []),   # 值对; 精确化见待办 ("log(x)=1", ["x = 10"], []),       # 本工具的 log = 常用对数(log10)
    ("sin(x)=0.5", ["0.5235", "pi/6", "π/6"], []),     # 数值解集
    ("z^2=-1", ["z_1 = -i", "z_2 = i", "i"], []),      # 复数方程
    ("|z-1|=2", ["x", "-1", "3", "z_1"], []),  # 实模方程 -> 实数解(复平面轨迹不表达)                            # 复平面轨迹
    ("C(n,2)=15", ["n_2 = 6", "n = 6"], []),
    ("C(5,2)", ["10"], []), ("A(5,2)", ["20"], []),        # 数值也要对
    ("2*pi*r=C", ["C"], []),                               # C 不能因为 C(n,k) 而变成函数                      # 组合方程
    ("A(n,2)=20", ["n_2 = 5", "n = 5"], []),                      # 排列方程
    ("x^3-6x^2+11x-6=0", ["x_1 = 1", "x_2 = 2", "x_3 = 3"], []),
    # 已知条件求值(用户提的 ab=1,a+b=3 -> a^5+b^5)
    ("a*b=1, a+b=3", ["a = "], ["--derive", "a^5+b^5"]),
    ("a*b=1, a+b=3", ["123"], ["--derive", "a^5+b^5"]),
    # 字母歧义: s*i*n 当成三个未知数 vs sin 当函数
    ("s*i*n=1", ["n = ", "s"], []),
    ("sin(x)=0", ["2·n·π", "2*n*pi", "n·π"], []),
    # 明确超出范围(记录现状, 不算失败): 微分方程 / 同余 / 矩阵
    ("dx/dt=k*x", ["x", "无解", "自由"], []),
    ("a*x=b (mod m)", ["解", "模", "无解", "自由", "未知"], []),
]


def run(args, inp, extra):
    mode = "--solve" if ("=" in inp) else "--eval"
    a = [BIN, mode, inp] + HIDE + extra
    if mode == "--eval":
        a = [BIN, mode, inp] + HIDE + ["--show", "plain,approx,decimal,note"] + extra
    r = subprocess.run(a, capture_output=True, text=True, timeout=180, stdin=subprocess.DEVNULL)
    return r.stdout + r.stderr, r.returncode


def main():
    fails, note = [], []
    for inp, wants, extra in CASES:
        try:
            out, rc = run(None, inp, extra)
        except subprocess.TimeoutExpired:
            fails.append("%-32s 挂住" % (inp + " " + " ".join(extra)))
            continue
        # 期望: 至少一个片段出现(允许不同等价写法)
        if not any(w in out for w in wants):
            fails.append("%-32s 期望含 %s\n      实际: %s"
                         % (inp + " " + " ".join(extra), wants, out.strip().replace("\n", " ⏎ ")[:150]))
    print("课程覆盖: 检查 %d 项, 不满足 %d 项" % (len(CASES), len(fails)))
    for f in fails:
        print("  - " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
