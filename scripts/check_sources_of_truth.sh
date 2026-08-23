#!/usr/bin/env bash
# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0
#
# Fail if checked-in copies / generated artefacts diverge from their SoT.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail=0

check_same() {
  local a="$1" b="$2"
  if [[ ! -e "$a" ]]; then
    echo "error: missing $a" >&2
    fail=1
    return
  fi
  if [[ ! -e "$b" ]]; then
    echo "error: missing $b" >&2
    fail=1
    return
  fi
  if ! diff -q "$a" "$b" >/dev/null; then
    echo "error: $a and $b differ" >&2
    fail=1
  else
    echo "ok: $a ≡ $b"
  fi
}

check_same dist/sqldeep.h swift/Sources/CSQLDeep/include/sqldeep.h
check_same dist/sqldeep_xml.h swift/Sources/CSQLDeepXML/include/sqldeep_xml.h
check_same testdata/sqlite.yaml swift/Tests/SQLDeepRuntimeTests/sqlite.yaml

# Kotlin androidTest must not keep an unenforced private copy.
if [[ -e kotlin/src/androidTest/assets/sqlite.yaml ]]; then
  check_same testdata/sqlite.yaml kotlin/src/androidTest/assets/sqlite.yaml
else
  echo "ok: no kotlin/src/androidTest/assets/sqlite.yaml (assets.srcDirs → testdata/)"
fi

if ! python3 scripts/gen_agent_guide_inc.py --check; then
  fail=1
fi

# Swift must link the C runtime, not reimplement it.
if [[ -f swift/Sources/SQLDeepRuntime/SQLDeepRuntime.swift ]]; then
  echo "error: SQLDeepRuntime.swift must not exist; link dist/sqldeep_xml.c via CSQLDeepXML" >&2
  fail=1
fi
if [[ ! -f swift/Sources/CSQLDeepXML/sqldeep_xml_lib.c ]]; then
  echo "error: missing swift/Sources/CSQLDeepXML/sqldeep_xml_lib.c" >&2
  fail=1
fi
if ! grep -q 'sqldeep_xml\.c' swift/Sources/CSQLDeepXML/sqldeep_xml_lib.c; then
  echo "error: CSQLDeepXML must #include dist/sqldeep_xml.c" >&2
  fail=1
fi

exit "$fail"
