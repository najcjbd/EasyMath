# EasyMath - 便捷构建(make install / make test)
PREFIX ?= /opt/EasyMath
BUILD  ?= build
JOBS   ?= $(shell nproc 2>/dev/null || echo 4)

all: $(BUILD)/EasyMath

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/EasyMath: CMakeLists.txt $(wildcard src/*.cpp) $(wildcard src/*.hpp)
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX)
	cmake --build $(BUILD) -j$(JOBS)

install: $(BUILD)/EasyMath
	cmake --install $(BUILD)

test: $(BUILD)/EasyMath
	./tests/run_cli_tests.sh $(BUILD)/EasyMath

# 精度测试: 逐位对照 mpmath(需要 python3 + mpmath)
prec-test: $(BUILD)/EasyMath
	python3 tests/test_precision.py $(BUILD)/EasyMath --digits=20,30,50

# 打断机制测试: SIGINT 中途打断的行为承诺
int-test: $(BUILD)/EasyMath
	python3 tests/test_interrupt.py $(BUILD)/EasyMath

# 差分对拍(需要 python3, 可选 sympy) / 模糊测试
diff-test: $(BUILD)/EasyMath
	python3 tests/difftest.py $(BUILD)/EasyMath --cases=200

fuzz-test: | $(BUILD)
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I src -DEASYMATH_USE_GMP -DEASYMATH_USE_MPFR src/*.cpp -lgmpxx -lgmp -lmpfr -lmpc -o $(BUILD)/EasyMath-asan
	python3 tests/fuzz_cli.py $(BUILD)/EasyMath-asan --cases=500

# 用 SymPy 引擎再跑一遍模糊测试(每例会启动一次 Python, 较慢)
fuzz-test-sympy: | $(BUILD)
	python3 tests/fuzz_cli.py $(BUILD)/EasyMath --cases=300 --engine=sympy

# 全量测试(十三套)
test-all: $(BUILD)/EasyMath
	./tests/run_all.sh $(BUILD)/EasyMath

inflate-test: $(BUILD)/test_inflate
$(BUILD)/test_inflate: tests/test_inflate.cpp $(wildcard src/*.hpp) | $(BUILD)
	g++ -std=c++17 -O2 -I src tests/test_inflate.cpp src/inflate.cpp src/zipfile.cpp -lz -o $(BUILD)/test_inflate
	$(BUILD)/test_inflate

glyph-test: $(BUILD)/test_glyph
$(BUILD)/test_glyph: tests/test_glyph.cpp $(wildcard src/*.hpp) | $(BUILD)
	g++ -std=c++17 -O2 -I src tests/test_glyph.cpp src/glyph.cpp src/truetype.cpp src/zipfile.cpp src/inflate.cpp -o $(BUILD)/test_glyph
	$(BUILD)/test_glyph

font-test: $(BUILD)/test_truetype
$(BUILD)/test_truetype: tests/test_truetype.cpp $(wildcard src/*.hpp) | $(BUILD)
	g++ -std=c++17 -O2 -I src tests/test_truetype.cpp src/truetype.cpp src/zipfile.cpp src/inflate.cpp -o $(BUILD)/test_truetype
	$(BUILD)/test_truetype

rt-test: $(BUILD)/test_roundtrip
$(BUILD)/test_roundtrip: tests/test_roundtrip.cpp $(wildcard src/*.cpp) | $(BUILD)
	g++ -std=c++17 -O2 -I src tests/test_roundtrip.cpp src/expr.cpp src/rational.cpp src/bigint_gmp.cpp src/unicode.cpp src/interrupt.cpp -DEASYMATH_USE_GMP -lgmpxx -lgmp -o $(BUILD)/test_roundtrip
	$(BUILD)/test_roundtrip

core-test: $(BUILD)/test_core
$(BUILD)/test_core: tests/test_core.cpp $(wildcard src/*.cpp) | $(BUILD)
	g++ -std=c++17 -O2 -I src tests/test_core.cpp src/expr.cpp src/poly.cpp src/solve.cpp src/rational.cpp src/bigint_gmp.cpp src/hpnum.cpp src/unicode.cpp src/i18n.cpp src/interrupt.cpp src/engine.cpp -DEASYMATH_USE_GMP -DEASYMATH_USE_MPFR -lgmpxx -lgmp -lmpfr -lmpc -o $(BUILD)/test_core
	$(BUILD)/test_core

bigdigits-test: $(BUILD)/EasyMath
	python3 tests/test_bigdigits.py $(BUILD)/EasyMath

equations-test: $(BUILD)/EasyMath
	python3 tests/audit_equations.py $(BUILD)/EasyMath

audit-test: $(BUILD)/EasyMath
	python3 tests/audit_cli.py $(BUILD)/EasyMath

windows:
	./packaging/build-windows.sh

clean:
	rm -rf $(BUILD)

.PHONY: all install test test-all core-test rt-test prec-test int-test diff-test fuzz-test fuzz-test-sympy inflate-test font-test glyph-test bigdigits-test audit-test equations-test windows clean
