# ── Toolchain ────────────────────────────────────────────────────────
cc  = cc
cxx = c++ -std=c++20
cxxflags = -Wall -Wextra -Wpedantic

vendor   = vendor
incflags = -I. -Idist -I$vendor/include

# ── Static library ───────────────────────────────────────────────
lib = build/libsqldeep.a

# ── Sources ──────────────────────────────────────────────────────────
test_srcs = tests/doctest_main.cpp $[wildcard tests/test_*.cpp]
test_objs = $[patsubst %.cpp,build/%.o,$test_srcs]

# ── Tasks ────────────────────────────────────────────────────────────
!test: build/sqldeep_tests
    ./$input

!lib: $lib

!example: build/demo
    ./$input

!shell: build/sqldeep

!clean:
    rm -rf build/ .mk/

# ── Binaries ─────────────────────────────────────────────────────────
# Tests link sqlite3 for integration tests; the library itself does not.
build/sqldeep_tests: $test_objs build/sqldeep.o build/sqlite3.o
    $cxx $cxxflags -o $target $inputs

build/demo: build/examples/demo.o build/sqldeep.o
    $cxx $cxxflags -o $target $inputs

build/sqldeep: build/cmd/sqldeep.o build/sqldeep.o build/sqlite3.o
    $cxx -o $target $inputs -lreadline -lz

$lib: build/sqldeep.o
    ar rcs $target $inputs

# ── Compilation rules ────────────────────────────────────────────────
build/sqldeep.o: dist/sqldeep.cpp dist/sqldeep.h
    $cxx $cxxflags $incflags -c $input -o $target

build/sqlite3.o: $vendor/src/sqlite3.c
    $cc -w -c $input -o $target

build/tests/{name}.o: tests/{name}.cpp
    $cxx $cxxflags $incflags -c $input -o $target

build/examples/{name}.o: examples/{name}.cpp
    $cxx $cxxflags $incflags -c $input -o $target

build/cmd/{name}.o: cmd/{name}.c
    $cc -w $incflags -I$vendor/src -c $input -o $target
