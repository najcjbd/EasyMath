/* 本机 aarch64: Google 只提供 x86_64 的 aapt2。
 * AGP 会校验 aapt2 必须是 ELF 可执行文件, 所以这里编译一个 aarch64 启动器,
 * 由它调用 box64 去运行真正的 aapt2。 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BOX64 "/usr/local/bin/box64"
#define REAL_AAPT2 "/opt/android-sdk/build-tools/36.0.0/aapt2"

int main(int argc, char **argv) {
    char **nargv = (char **)malloc(sizeof(char *) * (size_t)(argc + 2));
    if (!nargv) return 127;
    nargv[0] = (char *)"box64";
    nargv[1] = (char *)REAL_AAPT2;
    for (int i = 1; i < argc; ++i) nargv[i + 1] = argv[i];
    nargv[argc + 1] = NULL;
    execv(BOX64, nargv);
    perror("execv box64");
    return 127;
}
