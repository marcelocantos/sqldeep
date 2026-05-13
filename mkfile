# ── Toolchain ────────────────────────────────────────────────────────
cc  = cc
cxx = c++ -std=c++20
cxxflags = -Wall -Wextra -Wpedantic

vendor      = vendor
incflags    = -I. -Idist -I$vendor/include
dp_dir      = $vendor/deepparser
dp_srcdir   = $dp_dir/src
dp_incflags = -I$dp_srcdir
dp_cflags   = -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -O2 -DNDEBUG

# Platform-dependent readline paths
lazy os = $[shell uname -s]
lazy rl_incflags = $[shell if [ "$os" = Darwin ]; then echo "-I/opt/homebrew/opt/readline/include"; fi]
lazy rl_ldflags  = $[shell if [ "$os" = Darwin ]; then echo "-L/opt/homebrew/opt/readline/lib"; fi]

# ── Static library ───────────────────────────────────────────────
lib            = build/libsqldeep.a
lib_deepparser = build/libdeepparser.a

dp_objs = build/deepparser/arena.o \
          build/deepparser/liteparser.o \
          build/deepparser/lp_tokenize.o \
          build/deepparser/lp_unparse.o \
          build/deepparser/parse.o

# ── Sources ──────────────────────────────────────────────────────────
test_srcs = tests/doctest_main.cpp $[wildcard tests/test_*.cpp]
test_objs = $[patsubst %.cpp,build/%.o,$test_srcs]

# ── Tasks ────────────────────────────────────────────────────────────
!test: build/sqldeep_tests
    ./$input

!lib: $lib

!deepparser: $lib_deepparser

!example: build/demo
    ./$input

!shell: build/sqldeep

!clean:
    rm -rf build/ .mk/

# ── Binaries ─────────────────────────────────────────────────────────
# Tests link sqlite3 for integration tests; the library itself does not.
# Deepparser objects are linked in so compile errors surface in `mk test`,
# but no sqldeep code consumes them yet (stage 1: vendor + build only).
build/sqldeep_tests: $test_objs build/sqldeep.o build/sqldeep_xml.o build/sqlite3.o $lib_deepparser
    $cxx $cxxflags -o $target $inputs

build/demo: build/examples/demo.o build/sqldeep.o
    $cxx $cxxflags -o $target $inputs

build/sqldeep: build/cmd/sqldeep.o build/sqldeep.o build/sqldeep_xml.o build/sqlite3.o
    $cxx -o $target $inputs $rl_ldflags -lreadline -lz

$lib: build/sqldeep.o
    ar rcs $target $inputs

$lib_deepparser: $dp_objs
    ar rcs $target $inputs

# ── Compilation rules ────────────────────────────────────────────────
build/sqldeep.o: dist/sqldeep.cpp dist/sqldeep.h
    $cxx $cxxflags $incflags -c $input -o $target

build/sqldeep_xml.o: dist/sqldeep_xml.c dist/sqldeep_xml.h
    $cc -w $incflags -c $input -o $target

build/sqlite3.o: $vendor/src/sqlite3.c
    $cc -w -c $input -o $target

build/deepparser/{name}.o: $dp_srcdir/{name}.c
    $cc $dp_cflags $dp_incflags -c $input -o $target

build/tests/{name}.o: tests/{name}.cpp
    $cxx $cxxflags $incflags -c $input -o $target

build/examples/{name}.o: examples/{name}.cpp
    $cxx $cxxflags $incflags -c $input -o $target

build/cmd/{name}.o: cmd/{name}.c
    $cc -w $incflags -I$vendor/src -DHAVE_READLINE=1 $rl_incflags -c $input -o $target
