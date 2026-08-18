#!/usr/bin/env bash
#
# fossilize/mayhem/test.sh -- RUN Fossilize's own upstream test suite (5 binaries built by
# mayhem/build.sh with the project's normal flags) PLUS 3 KAT probes (mayhem/kat/*.cpp), and emit a
# CTRF summary. exit 0 iff nothing failed. Does NOT compile -- build.sh already built everything.
#
# WHY THE KAT PROBES ARE MANDATORY HERE (SPEC 6.3 anti-reward-hacking).
# Fossilize's own test/ suite is a homegrown abort()-on-failure framework (fossilize_test.cpp,
# varint_test.cpp, ...): each binary calls abort()/returns EXIT_FAILURE on a failed check and falls
# through to `return 0`/EXIT_SUCCESS otherwise -- there is no stdout assertion of any value. Under
# verify-repo's sabotage shim (LD_PRELOAD _exit(0) in a constructor, before main() runs, for every
# non-system binary), EVERY one of these 5 binaries would "pass" by exit code alone -- the process
# never reaches the code that could abort(). That is exactly the exit-code-only oracle the spec
# forbids (the C/C++ ctest/meson trap, but here it is the project's OWN test binaries, run directly).
#
# So on top of running the 5 upstream binaries (still useful: a crash/non-zero exit from a REAL
# defect is caught), we run 3 KAT probes (mayhem/kat/kat_foz_db.cpp, kat_state_json.cpp,
# kat_varint.cpp) that print `KAT_<NAME>=<value>` lines and are asserted here via `grep -qxF`
# against exact expected values -- through the exact code paths the two fuzz targets exercise
# (the .foz container reader/writer, and StateReplayer::parse()/StateRecorder round-trip). A
# neutered binary prints NOTHING (constructor exits before main), so the grep fails and this
# script fails -- unlike the upstream binaries' bare exit code, this cannot be satisfied by doing
# nothing. Probes are unconditional: a missing binary is a FAILURE, never a skip.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

BUILD_ROOT="$SRC/mayhem-build/test"
PASSED=0
FAILED=0
FAILURES=()

run_upstream_test() {
  local name="$1" bin="$BUILD_ROOT/$1"
  if [ ! -x "$bin" ]; then
    echo "MISSING: $bin -- run mayhem/build.sh first" >&2
    FAILED=$((FAILED + 1)); FAILURES+=("$name: missing binary")
    return
  fi
  echo "=== running: $bin ==="
  if "$bin" > "/tmp/${name}.out" 2>&1; then
    echo "PASS: $name"
    PASSED=$((PASSED + 1))
  else
    echo "FAIL: $name (rc=$?)"
    tail -20 "/tmp/${name}.out" | sed 's/^/    /'
    FAILED=$((FAILED + 1)); FAILURES+=("$name: nonzero exit")
  fi
}

for t in fossilize-test varint-test application-info-filter-test object-cache-test futex-test; do
  run_upstream_test "$t"
done

# ── KAT probes: unconditional exact-value assertions (see header). ──────────────────────────────
run_kat() {
  local name="$1" bin="$BUILD_ROOT/$1"; shift
  if [ ! -x "$bin" ]; then
    echo "MISSING: $bin -- run mayhem/build.sh first" >&2
    FAILED=$((FAILED + 1)); FAILURES+=("$name: missing binary")
    return
  fi
  local out rc=0
  out="$("$bin" 2>&1)" || rc=$?
  echo "=== $name output ==="
  printf '%s\n' "$out"

  local ok=1
  for expected in "$@"; do
    if ! printf '%s\n' "$out" | grep -qxF "$expected"; then
      echo "FAIL: $name -- expected exact line not found: $expected" >&2
      ok=0
    fi
  done
  if [ "$rc" -ne 0 ]; then
    echo "FAIL: $name exited $rc" >&2
    ok=0
  fi

  if [ "$ok" -eq 1 ]; then
    echo "PASS: $name"
    PASSED=$((PASSED + 1))
  else
    FAILED=$((FAILED + 1)); FAILURES+=("$name: KAT assertion failed")
  fi
}

run_kat kat_foz_db \
  "KAT_FOZ_SHADER_COUNT=1" \
  "KAT_FOZ_SAMPLER_COUNT=1" \
  "KAT_FOZ_SHADER_BYTES=18" \
  "KAT_FOZ_SHADER_CONTENT_OK=1"

run_kat kat_state_json \
  "KAT_STATE_PARSE_OK=1" \
  "KAT_STATE_SAMPLERS=1" \
  "KAT_STATE_SHADER_MODULES=1"

run_kat kat_varint \
  "KAT_VARINT_ENCODED_SIZE=26" \
  "KAT_VARINT_ROUNDTRIP_OK=1"

echo "=== results: $((PASSED + FAILED)) total, $PASSED passed, $FAILED failed ==="
if [ "$FAILED" -gt 0 ]; then
  printf 'failures:\n'; printf '  - %s\n' "${FAILURES[@]:-}"
fi
emit_ctrf "fossilize-upstream+kat" "$PASSED" "$FAILED"
