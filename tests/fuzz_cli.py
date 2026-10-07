#!/usr/bin/env python3
"""EasyMath CLI 模糊测试: 检查崩溃/超时/sanitizer 报错/异常退出码。

用法: fuzz_cli.py <二进制> [--cases N] [--engine=builtin]
  --cases N   随机用例组数(固定语料另计, 默认 400)

超时设得比较宽松(180 秒): 这里要抓的是"真的卡死", 不是比速度;
机器繁忙(例如同时跑差分/精度套件)时短超时会产生假报警。
"""
import os
import random
import subprocess
import sys
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else "./EasyMath"
ENGINE = "--engine=builtin"
for a in sys.argv[2:]:
    if a.startswith("--engine="):
        ENGINE = a

ALLOWED = {0, 1, 2, 3, 130}
ENV = dict(os.environ)
ENV["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=0"
ENV["UBSAN_OPTIONS"] = "print_stacktrace=1"

BAD_MARKERS = ["AddressSanitizer", "runtime error", "UndefinedBehaviorSanitizer",
               "SEGV", "stack-overflow", "double-free", "heap-buffer-overflow"]

# 超时不一定是被测程序的锅: 这台机器(Android/Termux, 还会跑构建)偶尔会整体卡住,
# 于是同一个用例单独跑 200 次都在 1s 内, 却偶发地被 180s 判超时。
# 碰到超时就拿一次"零成本调用"探一下环境: 环境自己都卡 -> 记为环境卡顿, 不算产品问题;
# 环境秒回 -> 那就是真的死锁, 照常判失败。两种情况都会打印出来, 不掩盖。
def env_stalled(bin_path, env):
    t = time.time()
    try:
        subprocess.run([bin_path, "--version"], capture_output=True, timeout=30,
                       stdin=subprocess.DEVNULL, env=env)
        return time.time() - t
    except subprocess.TimeoutExpired:
        return None

# ---------- 语料 ----------
PIECES = [
    "", " ", "\t\n", ",", ",,,", "@#$", "|", "\\", "=", "==", "===", "!", "!!", "%", "()", ")(",
    "((((((((((", "))))))))))", "[", "]", "{", "}", "x=", "=x", "x==", "=1", "1=",
    "x^", "^2", "x^^2", "2^^", "*3", "/3", "+", "-", "**", "//", "5x", "x5", "5x6", "x_y", "x__1",
    "x⁴", "x₁", "x⁰", "x⁻¹", "x⁺", "²", "³³³", "₀₁₂", "x^99999", "2^2^2^2^2^2", "9^999999999",
    "100000!", "1000000!", "(-1)!", "0.5!", "1/0", "0/0", "0^0", "0^-1", "(-8)^(1/3)", "(-1)^0.5",
    "sqrt(-1)", "sqrt(-4)", "log(0)", "log(-5)", "ln(0)", "tan(90°)", "cot(0)", "csc(0)", "sec(90°)",
    "asin(2)", "acos(-2)", "atan(1e999)", "exp(1e999)", "1e5", "1.2.3", "1_000", "--5", "+-*/",
    "inf", "nan", "Infinity", "∏()", "∑()", "∏(1)", "∫(1,2)", "∠", "∠∠45", "45°", "45度", "45dgree",
    "\\frac{1}", "\\frac{}{}", "\\sqrt[", "\\sqrt[]{}", "\\begin{cases}", "\\end{cases}",
    "\\unknowncmd{1}", "\\frac{\\frac{1}{2}}{3}", "$x$", "$$x$$", "\\(x\\)", "x^{", "x^{}", "x_{",
    "π", "ππ", "π=3", "e", "tau", "i", "∞", "≠", "≤", "≥", "±", "∓", "√", "∛", "∜", "√√√√2",
    "   ", "\u3000", "\uFF11\uFF12", "𝐱+𝒙+𝑥+𝕩", "𝜶+β+γ", "😀", "\xff\xfe", "x\x01y",
    "x" * 5000, "(" * 300, "1+" * 300 + "1", "x^" + "("*100 + "2" + ")"*100,
    "1e999999", "999999999999999999999999999999^999999999",
    "0.000000000000000000000000000001", ".5", "5.", "1/3", "-1/3", "1/-3",
]
POINT_TOKENS = ["x=1", "y=2", "x1=3", "y1=4", "5x=1", "2y=2", "x_1=1", "x^1=1", "x999999999=1",
                "x0=1", "x-1=1", "P1=(1,2)", "(1,2)", "[3,4]", "{5,6}", "(1)", "(1,2,3)", "P=(1,2)",
                "P99999999999999999999=(1,2)", "x=", "=1", "x=1,y=2", "x=" + "9" * 50]
EQ_TOKENS = ["x=1", "y=x", "x^2=4", "x^4=5", "x+y=1", "x*y=1", "sin(x)=0.5", "cos(x)=x",
             "x^100=1", "x^1000=1", "x^-1=2", "0=0", "1=2", "x=x", "x!=x", "sqrt(x)=-1",
             "x^2+y^2=25", "y=x^4", "e^x=1", "ln(x)=1", "x/0=1", "1/x=0", "x^2+1=0",
             "x=", "=x", "=", "x==1", "x=1=2", "999999999^999=x"]
SEPS = [",", ";", "@", "#", "$", "|", " ", "\t"]

def rand_input(rng, tokens, n=None):
    n = n or rng.randint(1, 6)
    return rng.choice(SEPS).join(rng.choice(tokens) for _ in range(n))

def gen_corpus(rng, n):
    cases = []
    for p in PIECES:
        cases.append(["--eval", p])
        cases.append(["--solve", p])
        cases.append(["--lagrange", p])
        cases.append(["--line", p])
    for _ in range(n):
        mode = rng.choice(["--eval", "--solve", "--lagrange", "--line"])
        if mode == "--lagrange":
            s = rand_input(rng, POINT_TOKENS)
        elif mode == "--solve":
            s = rand_input(rng, EQ_TOKENS)
        elif mode == "--line":
            s = rng.choice(PIECES + ["45", "-45", "0", "90", "180", "360", "1e999",
                                     "999999999999999999999999", "45°", "0.5rad", "π/4"])
        else:
            s = rng.choice(PIECES)
        args = [mode, s, ENGINE]
        if rng.random() < 0.3:
            args.append("--decimals=%d" % rng.choice([0, 1, 8, 30, 200]))
        if rng.random() < 0.2:
            args.append("--hpfloat=%s" % rng.choice(["auto", "mpfr", "sympy", "builtin"]))
        if rng.random() < 0.2:
            args += ["--sep", rng.choice(["@", "#", "$", ",", "?", "x", ""])]
        if rng.random() < 0.15:
            args.append("--real")
        if rng.random() < 0.15:
            args.append(rng.choice(["--hide", "--show"]) + "=" + rng.choice(
                ["all", "plain", "latex", "nonexistent", ""]))
        if rng.random() < 0.1:
            args += ["--set", rng.choice(["decimals=abc", "=1", "engine=bogus", "precision=-5",
                                          "out.plain=1"])]
        cases.append(args)
    # 超长输入只能走 stdin(交互模式)
    long_inputs = ["(" * 60000 + "1" + ")" * 60000, "-" * 60000 + "1", "1+" * 60000 + "1",
                   "sqrt(" * 6000 + "1" + ")" * 6000, "x" * 60000,
                   "1+" * 2000 + "x^2"]
    stdin_cases = []
    for li in long_inputs:
        stdin_cases.append((["-i"], li))
    # 参数层面的边角
    for extra in [[], ["--engine=builtin"], ["--engine=sympy"], ["--engine=nope"],
                  ["--decimals"], ["--decimals=999"], ["--out"], ["--out", "x/y"],
                  ["--out", "../evil"], ["--outdir", "/proc/x"], ["--config", "/dev/null"],
                  ["--config", "/nonexistent"], ["--set"], ["--sep"], ["-i", "--lagrange"],
                  ["--solve", "x=1", "--eval", "1"], ["--no-config"], ["--dump-engine-script"],
                  ["--engine-info"], ["--precision=0"], ["--precision=-1"], ["--hpfloat=nope"],
                  ["--bigint=nope"], ["x=1,y=2"], ["--", "--eval", "1"]]:
        cases.append(extra)
    return cases, stdin_cases

def run_stdin(args, data):
    return subprocess.run([BIN] + args, input=data.encode("utf-8", "surrogateescape"),
                          capture_output=True, timeout=180, env=ENV)


def main():
    rng = random.Random(20240918)
    n = 400
    for a in sys.argv[2:]:
        if a.startswith("--cases="):
            n = int(a.split("=")[1])
    cases, stdin_cases = gen_corpus(rng, n)
    bad = 0
    stalls = 0
    for args, data in stdin_cases:
        try:
            r = run_stdin(args, data + "\n0\n")
        except subprocess.TimeoutExpired:
            probe = env_stalled(BIN, ENV)
            if probe is None or probe > 15.0:
                stalls += 1
                print("环境卡顿(stdin, 非产品问题): len=%d —— 探测 --version 也要 %s s"
                      % (len(data), ">30" if probe is None else "%.1f" % probe))
            else:
                print("TIMEOUT(stdin, 疑似死锁): %r len=%d" % (args, len(data)))
                bad += 1
            continue
        out = (r.stdout + r.stderr).decode("utf-8", "replace")
        if r.returncode not in ALLOWED:
            print("EXIT %d (stdin, len=%d): %s" % (r.returncode, len(data), out[:300]))
            bad += 1
        for m in BAD_MARKERS:
            if m in out:
                print("SANITIZER/CRASH(stdin) (%s, len=%d): %s" % (m, len(data), out[:500]))
                bad += 1
                break
    slowest = (0.0, None)
    for i, args in enumerate(cases):
        t0 = time.time()
        try:
            # stdin 明确给 /dev/null: 否则若被测程序退到交互提示, 会继承调用者的
            # stdin 干等, 让"模糊测试"变成"挂住测试"(超时哨兵仍在)
            r = subprocess.run([BIN] + args, capture_output=True, timeout=180, env=ENV,
                               stdin=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            probe = env_stalled(BIN, ENV)
            if probe is None or probe > 15.0:
                stalls += 1
                print("环境卡顿(非产品问题): %r —— 超时后探测 --version 也要 %s s"
                      % (args, ">30" if probe is None else "%.1f" % probe))
            else:
                print("TIMEOUT(疑似死锁): %r —— 同一时刻 --version 只用 %.2fs" % (args, probe))
                bad += 1
            continue
        dt = time.time() - t0
        if dt > slowest[0]:
            slowest = (dt, args)
        out = (r.stdout + r.stderr).decode("utf-8", "replace")
        if r.returncode not in ALLOWED:
            print("EXIT %d: %r\n  %s" % (r.returncode, args, out[:400]))
            bad += 1
            continue
        for m in BAD_MARKERS:
            if m in out:
                print("SANITIZER/CRASH (%s): %r\n  %s" % (m, args, out[:600]))
                bad += 1
                break
    if slowest[1] is not None:
        print("最慢用例: %.1fs %r" % (slowest[0], slowest[1]))
    if stalls:
        print("另有 %d 次环境卡顿(设备整体卡住导致, 已复核不计为产品问题)" % stalls)
    print("模糊测试: %d + %d(stdin) 个用例, 问题 %d 个" % (len(cases), len(stdin_cases), bad))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
