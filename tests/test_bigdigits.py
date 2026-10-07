#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""超大小数位(>200)测试: 逐位对拍 + "拿不出就钳位"的承诺。

要求(用户提的):
  * 配置界面能手填 >200;
  * >200 走特殊算法, 但**必须精确**(逐位对照 mpmath / exact 分数展开);
  * 拿不出这么多位时, 退到本机能精确给出的最大位数并明确告知, 不许打噪声位。
"""
import re
import subprocess
import sys

try:
    import mpmath as mp
except ImportError:
    print("超大小数位测试: 跳过 (未安装 mpmath)")
    sys.exit(0)

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/EasyMath"
HIDE = "--hide=banner,input,step,steps,tip,prompt,file,verify,normalized"
PASS, FAIL = 0, 0


def ok():
    global PASS
    PASS += 1


def bad(name, detail=""):
    global FAIL
    FAIL += 1
    print("FAIL: " + name)
    if detail:
        for ln in str(detail).splitlines()[:4]:
            print("      " + ln)


def run(args, timeout=300):
    return subprocess.run([BIN] + args, capture_output=True, text=True, timeout=timeout,
                          stdin=subprocess.DEVNULL)



def printed_value(text):
    """取输出里最长的一个小数串, 解析成 mpf"""
    cands = re.findall(r"\d+\.\d+", text)
    if not cands:
        return None, ""
    best = max(cands, key=len)
    return mp.mpf(best), best


def digits_after_point(txt):
    return len(txt.split(".")[1]) if "." in txt else 0


def frac_digits(text):
    """取第一条形如 0.xxxx 的数字串的小数部分"""
    m = re.search(r"\d+\.(\d+)", text)
    return m.group(1) if m else ""


def main():
    mp.mp.dps = 1200

    # 1) 精确有理数: 1/3 的 300 位应当逐位等于精确展开
    r = run(["--eval", "1/3", "--decimals", "300", "--hide", "all", "--show", "decimal,approx"])
    val, raw = printed_value(r.stdout)
    if val is not None and digits_after_point(raw) >= 300 and abs(val - mp.mpf(1) / 3) <= mp.mpf("0.5e-300"):
        ok()
    else:
        bad("1/3 的 300 位精确展开", "位数=%d 得到 %s" % (digits_after_point(raw), raw[:60]))
    # 7/13 循环节 6 位
    r2 = run(["--eval", "7/13", "--decimals", "300", "--hide", "all", "--show", "decimal"])
    got2 = frac_digits(r2.stdout)
    import fractions
    fr = fractions.Fraction(7, 13)
    digits = ""
    n = fr.numerator % fr.denominator
    for _ in range(300):
        n *= 10
        digits += str(n // fr.denominator)
        n %= fr.denominator
    if got2[:300] == digits:
        ok()
    else:
        bad("7/13 的 300 位", "差在第 %d 位" % next((i for i in range(300)
                                                    if got2[i:i + 1] != digits[i:i + 1]), -1))

    # 2) 无理数: sqrt(2) / pi / e 的 300 位对拍 mpmath
    for expr, ref in (("sqrt(2)", mp.sqrt(2)), ("pi", mp.pi), ("e", mp.e), ("sqrt(3)+pi/7", mp.sqrt(3) + mp.pi / 7)):
        r = run(["--eval", expr, "--decimals", "300", "--hide", "all", "--show", "approx"])
        txt = r.stdout + r.stderr
        val, raw = printed_value(txt)
        # 判据: 打印到 300 位小数, 且误差 <= 0.5e-300(即"正确舍入")
        # 末位若是 0 会被去掉, 所以位数允许 299; 真正的判据是"误差 <= 0.5e-300"
        if val is not None and digits_after_point(raw) >= 290 and abs(val - ref) <= mp.mpf("0.5e-300"):
            ok()
        else:
            bad("无理数 300 位: %s" % expr,
                "位数=%d 误差=%s\n得到 %s" % (digits_after_point(raw),
                                            (abs(val - ref) if val is not None else "无"),
                                            raw[:70]))

    # 3) 钳位: 没有 MPFR/SymPy 时必须退到 long double 可靠位数, 且明确告知
    r = run(["--eval", "sqrt(2)", "--decimals", "300", "--hpfloat=builtin",
             "--hide", "all", "--show", "note,approx"])
    txt = r.stdout + r.stderr
    got = max(re.findall(r"\d+\.(\d+)", txt), key=len)
    if "已按" in txt and ("位输出" in txt or "位处理" in txt):
        ok()
    else:
        bad("钳位要明确告知", txt[:200])
    if len(got) <= 33:
        ok()
    else:
        bad("钳位后不该还打 %d 位" % len(got), got[:80])
    # 钳位后的位数必须仍然正确
    val, raw = printed_value(txt)
    nd = digits_after_point(raw)
    if val is not None and abs(val - mp.sqrt(2)) <= mp.mpf("0.5e-%d" % nd):
        ok()
    else:
        bad("钳位后的位数本身要正确(正确舍入)", "位数=%d 得到 %s" % (nd, raw[:50]))

    # 4) 超过硬上限: 要说清楚并按上限处理(不崩)
    r = run(["--eval", "1/3", "--decimals", "150000", "--hide", "all", "--show", "note,decimal"],
            timeout=600)
    txt = r.stdout + r.stderr
    if r.returncode == 0 and ("上限" in txt or "100000" in txt):
        ok()
    else:
        bad("超过硬上限要提示", "退出码 %d, 输出 %s" % (r.returncode, txt[:200]))

    # 5) 配置里也能填 >200
    r = run(["--eval", "1/3", "--decimals", "250", "--hide", "all", "--show", "decimal,approx"])
    got = frac_digits(r.stdout)
    if len(got) >= 250:
        ok()
    else:
        bad("配置/参数接受 >200", "只得到 %d 位" % len(got))

    print("超大小数位测试: 通过 %d 项, 失败 %d 项" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
