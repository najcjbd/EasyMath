#!/bin/sh
# 本机是 aarch64, Google 只发 x86_64 的 aapt2, 用 box64 运行它
exec box64 /opt/android-sdk/build-tools/36.0.0/aapt2 "$@"
