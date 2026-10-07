#!/usr/bin/env bash
# 生成发行包: 源码包 / Windows 包 / Android 源码包, 并刷新 SHA256SUMS.txt
# 用法: packaging/make-dist.sh [版本号] [APK路径]
#   版本号默认读 android/app/build.gradle 的 versionName; APK 默认取 android/app/build/outputs/apk/debug/app-debug.apk
set -euo pipefail
# 版本号只有一个真源: android/app/build.gradle 的 versionName(= 桌面端 src/cli.cpp 的 kVersion)
GRADLE_VER="$(grep -oE "versionName '[^']+'" android/app/build.gradle 2>/dev/null | head -1 | cut -d"'" -f2)"
VER="${1:-${GRADLE_VER:-0.0.0}}"
APK_IN="${2:-android/app/build/outputs/apk/debug/app-debug.apk}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
DIST="$ROOT/dist"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "== 1/4 源码包 (EasyMath-$VER-src.tar.gz) =="
SRC="$STAGE/EasyMath-$VER"
mkdir -p "$SRC"
cp CMakeLists.txt Makefile install.sh README.md QUICKSTART.md CHANGELOG.md "$SRC/"
mkdir -p "$SRC/src" "$SRC/tests" "$SRC/packaging"
cp src/*.cpp src/*.hpp "$SRC/src/"
cp tests/*.cpp tests/*.sh tests/*.py "$SRC/tests/"
cp packaging/easymath.conf.example packaging/build-windows.sh packaging/make-dist.sh "$SRC/packaging/"
chmod +x "$SRC/tests/"*.sh "$SRC/tests/"*.py "$SRC/packaging/"*.sh
mkdir -p "$DIST"
tar czf "$DIST/EasyMath-$VER-src.tar.gz" -C "$STAGE" "EasyMath-$VER"

echo "== 2/4 Windows 包 (EasyMath-$VER-windows.tar.gz) =="
WSTAGE="$STAGE/EasyMath-$VER-windows"
mkdir -p "$WSTAGE"
[ -f EasyMath-x86_64-w64-mingw32.exe ] || { echo "缺少 64 位 exe, 先跑 packaging/build-windows.sh"; exit 1; }
cp EasyMath-x86_64-w64-mingw32.exe "$WSTAGE/EasyMath-64bit.exe"
[ -f EasyMath-i686-w64-mingw32.exe ] && cp EasyMath-i686-w64-mingw32.exe "$WSTAGE/EasyMath-32bit.exe" || true
cp packaging/easymath.conf.example QUICKSTART.md "$WSTAGE/"
cat > "$WSTAGE/使用说明.txt" <<'TXT'
EasyMath (Windows 版)
=====================
1. 直接双击 EasyMath-64bit.exe (64 位系统) 或 EasyMath-32bit.exe (32 位系统)。
   这是控制台程序: 双击后会出现一个命令行窗口, 按提示操作即可。
2. 想更方便地调用, 可以把 exe 所在目录加进 PATH, 然后在 cmd/PowerShell 里:
      EasyMath-64bit --lagrange "x=1,y=3 x=2,y=5 x=3,y=9"
3. 配置文件: 把 easymath.conf.example 复制到 %USERPROFILE%\.easymath.conf 后按需修改。
   想要更高精度, 可把 bigint/hpfloat 设为 gmp/mpfr (需自备对应 DLL), 默认用自带后端。
4. 长计算随时可以按 Ctrl+C 打断: 立即停止当前计算, 退出码 130。
5. 本程序零依赖(静态链接), 不需要安装运行库。
TXT
tar czf "$DIST/EasyMath-$VER-windows.tar.gz" -C "$STAGE" "EasyMath-$VER-windows"

echo "== 3/4 Android 包 =="
mkdir -p "$DIST/android"
# 版本号从 build.gradle 读, 避免文件名与包内版本漂移
APKVER="$(grep -oE "versionName '[^']+'" android/app/build.gradle | head -1 | cut -d"'" -f2)"
[ -n "$APKVER" ] || APKVER="0.0.0"
if [ -f "$APK_IN" ]; then
  cp "$APK_IN" "$DIST/android/EasyMath-$APKVER-android-sympy.apk"
  echo "   APK -> dist/android/EasyMath-$APKVER-android-sympy.apk"
else
  echo "   跳过 APK (找不到 $APK_IN)"
fi
# Android 源码(不含构建产物与预编译 aapt2 二进制)
ASTAGE="$STAGE/EasyMath-android-src"
mkdir -p "$ASTAGE"
cp -r android/app android/tools android/docs android/build.gradle.kts android/settings.gradle.kts \
      android/gradle.properties android/README.md android/aapt2-launcher.c "$ASTAGE/" 2>/dev/null || true
rm -rf "$ASTAGE/app/build" "$ASTAGE/app/.cxx"
tar czf "$DIST/android/EasyMath-$APKVER-android-src.tar.gz" -C "$STAGE" "EasyMath-android-src"
echo "   Android 源码 -> dist/android/EasyMath-$APKVER-android-src.tar.gz"

echo "== 4/4 校验和 =="
( cd "$DIST" && find . -type f \( -name '*.tar.gz' -o -name '*.apk' \) -printf '%P\n' | sort | \
  while read -r f; do sha256sum "$f"; done > SHA256SUMS.txt )
cat "$DIST/SHA256SUMS.txt"
echo "完成: $DIST"
