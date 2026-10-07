cmake_minimum_required(VERSION 3.10)
project(EasyMath CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# 可选的高精度数值后端 (默认: 找到就用)
option(EASYMATH_WITH_GMP  "使用 GNU MP 做任意精度整数" ON)
option(EASYMATH_WITH_MPFR "使用 MPFR/MPC 做任意精度浮点" ON)

set(EASYMATH_SOURCES
  src/main.cpp
  src/engine.cpp
  src/cli.cpp
  src/config.cpp
  src/i18n.cpp
  src/output.cpp
  src/fileio.cpp
  src/modes.cpp
  src/solve.cpp
  src/poly.cpp
  src/expr.cpp
  src/rational.cpp
  src/bigint.cpp
  src/bigint_gmp.cpp
  src/hpnum.cpp
  src/unicode.cpp
  src/interrupt.cpp
  src/inflate.cpp
  src/zipfile.cpp
  src/truetype.cpp
  src/glyph.cpp
)

add_executable(EasyMath ${EASYMATH_SOURCES})
target_include_directories(EasyMath PRIVATE src)
find_package(Threads REQUIRED)
target_link_libraries(EasyMath PRIVATE Threads::Threads)

# 编译警告(安装日志里能直接看到)
if(MSVC)
  target_compile_options(EasyMath PRIVATE /W4)
else()
  target_compile_options(EasyMath PRIVATE -Wall -Wextra)
endif()


set(_suffixes aarch64-linux-gnu x86_64-linux-gnu arm-linux-gnueabihf i386-linux-gnu)

# ---- GMP ----
set(EASYMATH_GMP_FOUND FALSE)
if(EASYMATH_WITH_GMP)
  find_path(GMP_INCLUDE_DIR NAMES gmp.h PATH_SUFFIXES ${_suffixes})
  find_library(GMP_LIBRARY NAMES gmp)
  find_library(GMPXX_LIBRARY NAMES gmpxx)
  if(GMP_INCLUDE_DIR AND GMP_LIBRARY AND GMPXX_LIBRARY)
    target_compile_definitions(EasyMath PRIVATE EASYMATH_USE_GMP)
    target_include_directories(EasyMath PRIVATE ${GMP_INCLUDE_DIR})
    target_link_libraries(EasyMath PRIVATE ${GMPXX_LIBRARY} ${GMP_LIBRARY})
    set(EASYMATH_GMP_FOUND TRUE)
    message(STATUS "EasyMath: GMP 已启用 (${GMPXX_LIBRARY})")
  else()
    message(STATUS "EasyMath: 未找到 GMP, 使用自带大整数实现 (apt install libgmp-dev)")
  endif()
endif()

# ---- MPFR + MPC ----
set(EASYMATH_MPFR_FOUND FALSE)
if(EASYMATH_WITH_MPFR)
  if(NOT EASYMATH_GMP_FOUND)
    message(STATUS "EasyMath: MPFR 需要 GMP, 已跳过")
  else()
    find_path(MPFR_INCLUDE_DIR NAMES mpfr.h PATH_SUFFIXES ${_suffixes})
    find_library(MPFR_LIBRARY NAMES mpfr)
    find_path(MPC_INCLUDE_DIR NAMES mpc.h PATH_SUFFIXES ${_suffixes})
    find_library(MPC_LIBRARY NAMES mpc)
    if(MPFR_INCLUDE_DIR AND MPFR_LIBRARY AND MPC_INCLUDE_DIR AND MPC_LIBRARY)
      target_compile_definitions(EasyMath PRIVATE EASYMATH_USE_MPFR)
      target_include_directories(EasyMath PRIVATE ${MPFR_INCLUDE_DIR} ${MPC_INCLUDE_DIR})
      target_link_libraries(EasyMath PRIVATE ${MPFR_LIBRARY} ${MPC_LIBRARY})
      set(EASYMATH_MPFR_FOUND TRUE)
      message(STATUS "EasyMath: MPFR/MPC 已启用 (${MPFR_LIBRARY}, ${MPC_LIBRARY})")
    else()
      message(STATUS "EasyMath: 未找到 MPFR/MPC, 高精度浮点回退到 long double (apt install libmpfr-dev libmpc-dev)")
    endif()
  endif()
endif()

if(EASYMATH_GMP_FOUND)
  message(STATUS "EasyMath: 大整数后端 = GMP")
else()
  message(STATUS "EasyMath: 大整数后端 = builtin (自带实现)")
endif()
# ---------------- 安装 ----------------
install(TARGETS EasyMath RUNTIME DESTINATION bin)
install(FILES README.md QUICKSTART.md CHANGELOG.md DESTINATION share/doc/EasyMath)
install(FILES packaging/easymath.conf.example DESTINATION share/doc/EasyMath)
install(FILES packaging/build-windows.sh DESTINATION share/doc/EasyMath)
# KaTeX 资源(与 Android 端同一份): 导出"自包含 HTML"时内联进报告, 离线也能显示公式
install(DIRECTORY android/app/src/main/assets/katex DESTINATION share/EasyMath)
install(FILES android/app/src/main/assets/result.css DESTINATION share/EasyMath)
install(DIRECTORY tests/ DESTINATION share/EasyMath/tests
        PATTERN "*.tmp" EXCLUDE)
# 脚本需要可执行位(install(DIRECTORY) 不保留)
install(PROGRAMS tests/run_cli_tests.sh DESTINATION share/EasyMath/tests)
install(PROGRAMS tests/fuzz_cli.py tests/difftest.py DESTINATION share/EasyMath/tests)
install(PROGRAMS tests/test_precision.py tests/test_interrupt.py tests/run_all.sh
        DESTINATION share/EasyMath/tests)
# 头文件(便于用安装树里的测试源码自行编译验证)
install(DIRECTORY src/ DESTINATION include/EasyMath FILES_MATCHING PATTERN "*.hpp")
message(STATUS "EasyMath: 安装前缀 = ${CMAKE_INSTALL_PREFIX}")
