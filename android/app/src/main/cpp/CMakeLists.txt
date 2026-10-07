cmake_minimum_required(VERSION 3.22.1)
project(easymath CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 桌面端核心源码(与 /root/Math/src 完全同一份, 零改动复用)
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src)

add_library(easymath SHARED
    jni_bridge.cpp
    ${CORE}/unicode.cpp
    ${CORE}/bigint.cpp
    ${CORE}/rational.cpp
    ${CORE}/expr.cpp
    ${CORE}/poly.cpp
    ${CORE}/solve.cpp
    ${CORE}/hpnum.cpp
    ${CORE}/engine.cpp
    ${CORE}/output.cpp
    ${CORE}/fileio.cpp
    ${CORE}/config.cpp
    ${CORE}/interrupt.cpp
    ${CORE}/i18n.cpp
    ${CORE}/modes.cpp
    ${CORE}/inflate.cpp
    ${CORE}/zipfile.cpp
    ${CORE}/truetype.cpp
    ${CORE}/glyph.cpp
)

target_include_directories(easymath PRIVATE ${CORE} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(easymath PRIVATE -Wall -Wextra -O2)
find_library(log-lib log)
target_link_libraries(easymath ${log-lib})
