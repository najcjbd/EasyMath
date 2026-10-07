#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""EasyMath 打断机制回归测试。

对"用户随时可以打断"的三条承诺逐条验证:
  1) 计算中途发 SIGINT, 必须在很短时间内停下来(不等到算完);
  2) 非交互模式 -> 退出码 130, 且给出明确的"已中断"提示;
  3) 交互模式 -> 只取消这一次计算, 回到菜单, 之后的计算仍然正常;
  4) 菜单上(空闲时)按 Ctrl+C -> 安全退出 0/130 且不留残余;
  5) 被打断之后, 同一次运行里接着算的结果必须仍然正确(中断标志必须被清掉);
  6) 中断不能把程序搞崩(SIGSEGV/SIGABRT)或留下僵尸子进程。

用法: test_interrupt.py [二进制] [--slow]
"""
import os
import re
import signal
import subprocess
import sys
import time

BIN = "./EasyMath"
SLOW = "--slow" in sys.argv
for a in sys.argv[1:]:
    if not a.startswith("-"):
        BIN = a

# 每个用例: (说明, 参数, 该计算单独跑完需要的秒数下限)
HEAVY = [
    ("200 次方程求根", ["--solve", "x^200-2=0"], 2.0),
    ("150 次方程求根", ["--solve", "x^150-3x+1=0"], 1.0),
    ("大整数阶乘", ["--eval", "20000!"], 0.5),
    ("高精度求值", ["--eval", "√6", "--decimals", "5000"], 0.0),
    ("180 点拉格朗日插值", ["--lagrange",
                          " ".join("x=%d,y=%d" % (i, i * i + 1) for i in range(1, 181))], 0.5),
]
if SLOW:
    HEAVY.append(("300 次方程求根", ["--solve", "x^300-2=0"], 5.0))

checks = 0
fails = []


def note(ok, msg):
    global checks
    if ok:
        checks += 1
    else:
        fails.append(msg)


def baseline(args):
    t0 = time.time()
    p = subprocess.run([BIN] + args, capture_output=True, text=True, timeout=900)
    return time.time() - t0, p.returncode


def wait_after_sigint(args, delay, timeout=60):
    """启动, delay 秒后发 SIGINT, 返回 (退出码, 总耗时, 输出)"""
    p = subprocess.Popen([BIN] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    t0 = time.time()
    time.sleep(delay)
    alive = p.poll() is None
    if alive:
        p.send_signal(signal.SIGINT)
    try:
        out, _ = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
        return None, time.time() - t0, out
    return p.returncode, time.time() - t0, out


def main():
    global checks
    # 先摸清每个重活需要多久(否则"被打断"可能只是因为本来就快)
    print("== 建立耗时基线 ==")
    base = {}
    for name, args, _ in HEAVY:
        dt, rc = baseline(args)
        base[name] = dt
        print("   %-16s 完整运行 %.2fs (exit=%d)" % (name, dt, rc))

    print("== 用例1/2: 非交互中途打断 ==")
    for name, args, minsec in HEAVY:
        full = base[name]
        if full < 0.8:
            print("   跳过 %s: 本来只要 %.2fs, 无法可靠打断" % (name, full))
            continue
        delay = min(0.8, full / 3)
        rc, dt, out = wait_after_sigint(args, delay)
        stopped = rc == 130
        fast = dt < max(delay + 2.0, full * 0.6)
        crashed = rc is not None and rc < 0
        has_msg = ("已中断" in out) or ("interrupted" in out.lower())
        note(stopped and fast and not crashed,
             "打断 %s: exit=%s(期望130) 耗时=%.2fs(完整 %.2fs) 崩溃=%s\n      %s"
             % (name, rc, dt, full, crashed, out.strip()[-200:]))
        note(has_msg, "打断 %s: 缺少中断提示\n      %s" % (name, out.strip()[-200:]))
        if stopped and fast and has_msg:
            print("   %-16s exit=130 用时 %.2fs (完整 %.2fs) OK" % (name, dt, full))

    print("== 用例3/5: 交互模式打断后继续算 ==")
    script = "2\nx^200-2=0\n3\n1+1\n4\n45°\n0\n"
    p = subprocess.Popen([BIN], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    p.stdin.write(script)
    p.stdin.flush()
    time.sleep(0.8)
    p.send_signal(signal.SIGINT)
    try:
        out, _ = p.communicate(timeout=180)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
        fails.append("交互打断后卡住(超时)")
        out = ""
    note("已回到主菜单" in out or "back to the menu" in out,
         "交互模式打断后没有回到菜单\n      %s" % out.strip()[-300:])
    note(re.search(r"\b2\b", out) is not None,
         "交互模式打断后, 后续计算(1+1)没有输出\n      %s" % out.strip()[-300:])
    note(p.returncode == 0, "交互模式打断后继续使用, 最终退出码应为 0, 实际 %s" % p.returncode)
    if p.returncode == 0 and "已回到主菜单" in out:
        print("   交互打断 -> 回菜单 -> 继续算 -> 正常退出 OK")

    print("== 用例4: 菜单上按 Ctrl+C 安全退出 ==")
    p = subprocess.Popen([BIN], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    time.sleep(1.0)
    p.send_signal(signal.SIGINT)
    try:
        out, _ = p.communicate(timeout=30)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
        fails.append("菜单上 Ctrl+C 没有退出")
        out = ""
    note(p.returncode in (0, 130), "菜单 Ctrl+C: 退出码 %s" % p.returncode)
    note("安全退出" in out or "exited" in out.lower() or p.returncode == 0,
         "菜单 Ctrl+C: 无安全退出提示\n      %s" % out.strip()[-200:])
    print("   菜单 Ctrl+C 退出码 %s OK" % p.returncode)

    print("== 用例6: 打断后没有残留进程 ==")
    # (a) 本测试启动的子进程必须都被回收(没有僵尸)
    zombies = subprocess.run(["ps", "--ppid", str(os.getpid()), "-o", "stat=,args="],
                             capture_output=True, text=True).stdout
    zlist = [l for l in zombies.splitlines() if l.strip().startswith("Z")]
    note(not zlist, "存在僵尸子进程: %s" % zlist)
    # (b) 全局扫描只作提示: 同一时刻可能有别人在跑 EasyMath(例如精度套件),
    #     所以不能把它当成失败条件, 只报告"命令行第一个词就是本二进制"的进程
    target = os.path.abspath(BIN)
    leftover = subprocess.run(["ps", "-eo", "pid,args"], capture_output=True, text=True).stdout
    mine = []
    for line in leftover.splitlines()[1:]:
        parts = line.strip().split(None, 1)
        if len(parts) != 2:
            continue
        if os.path.abspath(parts[1].split()[0]) == target:
            mine.append(line.strip())
    if not zlist:
        print("   子进程全部回收 OK" + ("  (同时刻另有 %d 个 EasyMath 在跑, 属其他任务)"
                                        % len(mine) if mine else ""))

    print("== 用例7: 随机时机(含连续两次)打断不崩溃 ==")
    import random
    random.seed(20240919)
    pool = [
        ["--solve", "x^180-2=0"],
        ["--solve", "x^60-3x+1=0"],
        ["--eval", "√6", "--decimals", "2000"],
        ["--eval", "100000!", "--decimals", "20"],
        ["--line", "1rad", "--decimals", "3000"],
        ["--solve", "x^2+y^2=5, y=x^2-1"],
        ["--lagrange", " ".join("x=%d,y=%d" % (i, i * i + 1) for i in range(1, 40))],
    ]
    crash = 0
    hang = 0
    weird = 0
    for i in range(14):
        args = pool[i % len(pool)]
        delay = random.uniform(0.05, 0.6)
        p = subprocess.Popen([BIN] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(delay)
        try:
            p.send_signal(signal.SIGINT)
            if i % 3 == 0:            # 连续两次 Ctrl+C
                time.sleep(0.05)
                p.send_signal(signal.SIGINT)
        except ProcessLookupError:
            pass
        try:
            out, _ = p.communicate(timeout=45)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
            hang += 1
            fails.append("随机打断卡死(超时): %s delay=%.2f" % (" ".join(args), delay))
            continue
        rc = p.returncode
        if rc is None or rc < 0:
            crash += 1
            fails.append("随机打断被信号杀死: %s rc=%s" % (" ".join(args), rc))
        elif rc not in (0, 1, 130, 2, 3):
            weird += 1
            fails.append("随机打断退出码异常: %s rc=%s\n      %s" % (" ".join(args), rc, out.strip()[-160:]))
    if crash == 0 and hang == 0 and weird == 0:
        checks += 1
        print("   14 次随机时机(含 5 次连续两次)打断: 无崩溃/无卡死/退出码合法 OK")

    print("打断测试: 通过 %d 项, 失败 %d 项" % (checks, len(fails)))
    for f in fails:
        print("  FAIL " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
