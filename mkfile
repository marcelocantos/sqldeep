# ── Toolchain ────────────────────────────────────────────────────────
cc  = cc
cxx = c++ -std=c++20
cxxflags = -Wall -Wextra -Wpedantic

vendor   = vendor
incflags = -I. -I$vendor/include

# ── Sources ──────────────────────────────────────────────────────────
test_srcs = tests/doctest_main.cpp $[wildcard tests/test_*.cpp]
test_objs = $[patsubst %.cpp,build/%.o,$test_srcs]

# ── Tasks ────────────────────────────────────────────────────────────
!test: build/sqldeep_tests
    ./$input

!example: build/demo
    ./$input

!clean:
    rm -rf build/ .mk/

# ── Binaries ─────────────────────────────────────────────────────────
# Tests link sqlite3 for integration tests; the library itself does not.
build/sqldeep_tests: $test_objs build/sqldeep.o build/sqlite3.o
    $cxx $cxxflags -o $target $inputs

build/demo: build/examples/demo.o build/sqldeep.o
    $cxx $cxxflags -o $target $inputs

# ── Compilation rules ────────────────────────────────────────────────
build/sqldeep.o: sqldeep.cpp
    $cxx $cxxflags $incflags -c $input -o $target

build/sqlite3.o: $vendor/src/sqlite3.c
    $cc -w -c $input -o $target

build/tests/{name}.o: tests/{name}.cpp
    $cxx $cxxflags $incflags -c $input -o $target

build/examples/{name}.o: examples/{name}.cpp
    $cxx $cxxflags $incflags -c $input -o $target
