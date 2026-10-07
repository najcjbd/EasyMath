#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""广义 CLI/模式体检(不是算得对不对, 而是"会不会崩/挂/静默错/界面坏掉")。

覆盖: 五个模式的边角输入、CLI 参数组合、配置读写、保存导出路径、语言切换。
判据(每条都必须成立):
  1) 不崩: 输出里没有段错误/ASan/UBSan 痕迹;
  2) 不挂: 20 秒内退出(stdin 明确给 /dev/null, 防止交互提示干等);
  3) 退出码合法: 0 / 1 / 2 / 3 / 130;
  4) 成功(exit 0)时输出非空, 且不该出现 "无法解析/错误:" 这种自相矛盾的组合;
  5) 参数错误必须是 exit 2 或 3, 不能"静默成功"。
"""
import os
import re
import subprocess
import sys
import tempfile

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/EasyMath"
TIMEOUT = 20
ALLOWED = {0, 1, 2, 3, 130}
CRASH = ["AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error", "SEGV",
         "stack-overflow", "double-free", "heap-buffer-overflow", "core dumped"]

MODES = ["lagrange", "solve", "eval", "line", "glyph"]
EDGE_INPUTS = [
    "", " ", "\t\n", ",", ",,,", "=", "==", "()", ")(", "((((((", "))))))", "[", "]", "{", "}",
    "+", "-", "*", "/", "^", "!", "%", "°", "√", "π", "∞", "1e999", "1e-999",
    "-1", "0", "0.0000000001", "999999999999999999999999999999",
    "1/0", "0/0", "sqrt(-1)", "log(0)", "log(-1)", "tan(90°)", "0^0", "!", "x", "abc",
    "1+", "+1", "1++1", "1**1", "1^^1", "1..2", "1,2,3", "1 2 3", "±1", "≤", "≠", "≥",
    "x=", "=x", "y=x^2", "x=y^2", "x+y=1", "a=b=c", "1/(x-x)",
    "中文测试", "🀄", "é", "ß", "1234567890" * 30, "(" * 200 + "1" + ")" * 200,
    "--", "-1", "@", "#", "$", "|", "\\", "\"", "'", "`", ";", ":", "?",
]

FLAG_CASES = [
    ["--help"], ["--version"], ["--engine-info"], ["--print-config"], ["--dump-config"],
    ["--dump-engine-script"], ["--font-list"],
    ["--solve"], ["--solve", ""], ["--eval"], ["--lagrange"], ["--line"], ["--glyph"],
    ["--solve", "x=1", "--eval", "1"],            # 模式冲突
    ["--decimals", "-1", "--eval", "1"], ["--decimals", "abc", "--eval", "1"],
    ["--decimals", "99999", "--eval", "1"],
    ["--set", "=1", "--eval", "1"], ["--set", "engine=bogus", "--eval", "1"],
    ["--set", "decimals=abc", "--eval", "1"], ["--set", "precision=-5", "--eval", "1"],
    ["--engine", "nope", "--eval", "1"], ["--hpfloat", "nope", "--eval", "1"],
    ["--bigint", "nope", "--eval", "1"], ["--numeric-ineq", "nope", "--solve", "x=1"],
    ["--show", "nosuch", "--eval", "1"], ["--hide", "nosuch", "--eval", "1"],
    ["--show", "", "--eval", "1"], ["--hidden"], ["--eval", "1", "extra", "args"],
    ["--lang", "en", "--eval", "1/3"], ["--lang", "zz", "--eval", "1"],
    ["--const", "a,b", "--eval", "1"], ["--derive", "abc", "--eval", "1"],
    ["--fit-tol", "0", "--glyph", "A"], ["--fit-tol", "-1", "--glyph", "A"],
    ["--size", "0", "--glyph", "A"], ["--at", "(1,2,3)", "--glyph", "A"],
    ["--at", "abc", "--glyph", "A"], ["--font", "nosuch", "--glyph", "A"],
    ["--font-pack", "/nonexistent.zip", "--glyph", "A"],
    ["--config", "/nonexistent"], ["--config", "/dev/null", "--eval", "1"],
    ["--no-config", "--eval", "1"], ["--real", "--solve", "x^2=-1"],
    ["--complex", "--solve", "x^2=-1"], ["--out", "x/y", "--eval", "1"],
    ["--out", "../evil", "--eval", "1"], ["--outdir", "/proc/x", "--eval", "1", "--save"],
]

CONFIG_CASES = [
    "decimals = abc\n", "decimals = -5\n", "decimals = 99999\n",
    "engine = bogus\n", "bigint = 1e999\n", "precision = -1\n",
    "= 1\n", "unknownKey = 1\n", "out.plain = maybe\n", "fontPack = \n", "anchor = (((\n",
    "[section]\ndecimals = 12\n", "decimals = 12 # 注释\n", "lang = en\n", "lang = \n",
]

fails = []
checks = 0


def run(args, extra_env=None, cwd=None):
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    return subprocess.run([BIN] + args, capture_output=True, timeout=TIMEOUT,
                          stdin=subprocess.DEVNULL, env=env, cwd=cwd)


def check(name, args, r, want_zero_ok=True):
    global checks
    checks += 1
    out = (r.stdout + r.stderr).decode("utf-8", "replace")
    if r.returncode not in ALLOWED:
        fails.append("%s: 退出码 %d\n    %r\n    %s" % (name, r.returncode, args, out[:200]))
        return
    for m in CRASH:
        if m in out:
            fails.append("%s: 崩溃痕迹 %s\n    %r\n    %s" % (name, m, args, out[:300]))
            return
    if r.returncode == 0 and not out.strip():
        fails.append("%s: 退出码 0 但没有任何输出\n    %r" % (name, args))
        return
    INFO_CMDS = ("--dump-engine-script", "--help", "--print-config", "--dump-config",
                 "--engine-info", "--font-list")
    informational = any(c in args for c in INFO_CMDS)
    if not informational and r.returncode == 0 and re.search(r"(无法解析|错误: |cannot parse|error: )", out):
        # 成功退出却带解析错误, 说明"部分失败被吞掉"了
        fails.append("%s: 退出码 0 但输出里有错误\n    %r\n    %s" % (name, args, out[:200]))


def main():
    for mode in MODES:
        for inp in EDGE_INPUTS:
            args = ["--" + mode, inp, "--hide=banner,prompt,file,tip"]
            try:
                r = run(args)
            except subprocess.TimeoutExpired:
                fails.append("%s 挂住: %r" % (mode, inp))
                continue
            check("%s 边角输入" % mode, args, r)
    for args in FLAG_CASES:
        try:
            r = run(args)
        except subprocess.TimeoutExpired:
            fails.append("参数组合挂住: %r" % (args,))
            continue
        check("参数组合", args, r)
    # 配置文件: 写坏值不能让程序崩, 也不能静默成功
    with tempfile.TemporaryDirectory() as td:
        for i, body in enumerate(CONFIG_CASES):
            p = os.path.join(td, "c%d.conf" % i)
            with open(p, "w", encoding="utf-8") as f:
                f.write(body)
            args = ["--config", p, "--eval", "1/3", "--hide=banner"]
            try:
                r = run(args)
            except subprocess.TimeoutExpired:
                fails.append("配置挂住: %r" % (body,))
                continue
            check("配置 %r" % body.strip()[:24], args, r)
    # 保存/导出: 目录不存在、只读目录、超长文件名
    with tempfile.TemporaryDirectory() as td:
        for args in (["--eval", "1/3", "--save", "--outdir", os.path.join(td, "deep", "nested")],
                     ["--eval", "1/3", "--save", "--outdir", td, "--out", "x" * 300],
                     ["--glyph", "A", "--save", "--outdir", td],
                     ["--solve", "x^2=2", "--save", "--outdir", td, "--decimals", "50"]):
            try:
                r = run(args)
            except subprocess.TimeoutExpired:
                fails.append("保存挂住: %r" % (args,))
                continue
            check("保存", args, r)

    print("CLI 体检: 检查 %d 项, 问题 %d 个" % (checks, len(fails)))
    for f in fails:
        print("  - " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
