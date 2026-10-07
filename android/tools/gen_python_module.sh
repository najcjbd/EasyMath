#!/usr/bin/env bash
# 从桌面端二进制导出内嵌的 SymPy 桥脚本, 转成可被 Chaquopy import 的模块。
# 保证单一来源: 脚本本体只存在于 src/engine.cpp, 这里只是导出+加壳。
set -euo pipefail
BIN="${1:-/opt/EasyMath/bin/EasyMath}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../app/src/main/python/easymath_engine.py"
[ -x "$BIN" ] || { echo "找不到可执行文件: $BIN"; exit 1; }
mkdir -p "$(dirname "$OUT")"
"$BIN" --dump-engine-script > /tmp/_eng_raw.py
python3 - "$OUT" <<'PY'
import sys
out = sys.argv[1]
src = open('/tmp/_eng_raw.py', encoding='utf-8').read().rstrip('\n')
lines = src.split('\n')
assert lines[-1].strip() == 'main()', '脚本末尾结构变了: %r' % lines[-1]
lines = lines[:-1]                      # 去掉顶层 main() 调用, 使其 import 安全
lines.append('')
lines.append('if __name__ == "__main__":')
lines.append('    main()')
lines.append('')
lines.append('')
lines.append('def run_main(joined_args):')
lines.append('    """Chaquopy 入口: 参数用 \\x1f 连接, 返回捕获的输出文本"""')
lines.append('    import io')
lines.append('    import contextlib')
lines.append('    argv = [a for a in joined_args.split("\\x1f") if a != ""]')
lines.append('    buf = io.StringIO()')
lines.append('    old = sys.argv')
lines.append('    sys.argv = ["-"] + argv')
lines.append('    try:')
lines.append('        with contextlib.redirect_stdout(buf):')
lines.append('            main()')
lines.append('    finally:')
lines.append('        sys.argv = old')
lines.append('    return buf.getvalue()')
# 记录来源指纹, 便于发现"桥脚本忘了重新生成"(Android 与桌面端行为漂移)
import hashlib
digest = hashlib.sha256(src.encode('utf-8')).hexdigest()[:16]
header = '# 自动生成, 请勿手改: 来源 src/engine.cpp 内嵌桥脚本 (sha256:%s)' % digest
lines.insert(0, header)
open(out, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
print('已生成: %s (%d 行, 桥脚本 sha256:%s)' % (out, len(lines), digest))
PY
