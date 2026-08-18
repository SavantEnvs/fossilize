#!/usr/bin/env bash
#
# fossilize/mayhem/build.sh -- build two libFuzzer harnesses over Fossilize's untrusted-input
# surfaces (+ standalone reproducers), AND Fossilize's own upstream test suite + three KAT probes
# (mayhem/kat/*.cpp) for mayhem/test.sh.
#
#   fuzz_foz_db     -- create_stream_archive_database()/DatabaseInterface::prepare()/
#                      get_hash_list_for_resource_tag()/read_entry() over an untrusted ".foz"
#                      stream-archive file (header + entry table + miniz-compressed blobs) --
#                      the container format Steam's shader pre-caching reads from disk.
#   fuzz_state_json -- StateReplayer::parse() over an untrusted serialized blob (JSON, optionally
#                      followed by a NUL + binary varint payload) that reconstructs Vulkan
#                      *CreateInfo structs. NullStateCreator (mayhem/harnesses/fuzz_state_json.cpp)
#                      is a stub StateCreatorInterface -- there is no VkInstance/VkDevice/GPU
#                      anywhere in this build, by design (Fossilize is built exactly to allow this).
#
# Fossilize itself (fossilize.cpp/fossilize_db.cpp/varint.cpp/path.cpp/fossilize_application_filter.cpp
# + vendored miniz.c) is compiled here directly with $SANITIZER_FLAGS (+ -fsanitize=fuzzer-no-link
# UNCONDITIONALLY, independent of $SANITIZER_FLAGS, so SanitizerCoverage is present even under an
# explicit empty --build-arg SANITIZER_FLAGS= build) -- not just the harness translation unit --
# so the fuzzed library code carries SanCov + ASan/UBSan instrumentation.
#
# No CMake here: Fossilize's own CMakeLists.txt pulls in cli/SPIRV-Tools + cli/SPIRV-Cross (for the
# fossilize-replay/-disasm CLI tools) which this integration does not need -- neither fuzz target
# nor any of the 5 upstream test binaries we run touches SPIR-V disassembly/cross-compilation, so a
# handful of direct clang(++) invocations over the library + test .cpp files is both simpler and
# much faster than configuring the full CMake project. rapidjson (header-only) and the vendored
# khronos/ Vulkan headers are used directly from the working tree; no vcpkg/conan/FetchContent.
#
# Two upstream test binaries are deliberately NOT built: feature-filter-test and
# multi-instance-and-device-test link cli-utils (device.cpp/volk), which is about Vulkan
# driver-capability filtering -- orthogonal to the two fuzz targets here (database-container +
# state-deserializer parsing) and not part of Fossilize's `add_test()` set (ctest never runs
# multi-instance-and-device-test upstream either). The 5 tests built below (fossilize-test,
# varint-test, application-info-filter-test, object-cache-test, futex-test) are exactly Fossilize's
# own `add_test()` list minus those two, and are all fully self-contained (no external fixture
# files -- confirmed by reading each test's sources).
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) -- must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# `=` (not `:=`) for SANITIZER_FLAGS so an explicit empty --build-arg builds with NO sanitizers.
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# Always ensure the LIBRARY gets SanitizerCoverage instrumentation, regardless of the base image's
# default or an empty override (see header comment).
case "$SANITIZER_FLAGS" in
  *fuzzer-no-link*) ;;  # already present
  *) SANITIZER_FLAGS="$SANITIZER_FLAGS -fsanitize=fuzzer-no-link" ;;
esac
# DWARF <= 3 (SPEC 6.2 item 10): clang-19's plain -g emits DWARF-5; be explicit.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE STANDALONE_FUZZ_MAIN MAYHEM_JOBS COVERAGE_FLAGS
: "${SRC:=/mayhem}"
cd "$SRC"

# ── 0) The rapidjson submodule (header-only, the only submodule this build actually compiles
#       against). A checkout that already carries it (CI's `actions/checkout` with
#       `submodules: recursive`, or a prior build baked into this same image) needs no network at
#       all. verify-repo.sh's local docker-build check clones the checkout with a plain
#       `git clone .` (no --recursive), which leaves rapidjson/ as an empty gitlink dir -- fetch it
#       here, once, the first time build.sh runs against such a checkout. This only ever happens at
#       ordinary (networked) `docker build` time, never during the air-gapped re-run: once fetched,
#       the submodule content is baked into the image layer, so a later
#       `docker run --network none ... bash mayhem/build.sh` finds it already present. ────────────
if [ ! -f "$SRC/rapidjson/include/rapidjson/document.h" ]; then
  echo "rapidjson/ not populated -- fetching it once (online build only) ..."
  git -C "$SRC" submodule update --init -- rapidjson
fi
[ -f "$SRC/rapidjson/include/rapidjson/document.h" ] || { echo "FATAL: rapidjson/include/rapidjson/document.h still missing after submodule init" >&2; exit 1; }

# Fossilize's own library sources (matches the `fossilize` CMake target minus the Windows-only /
# Android-only / Vulkan-layer-only files this repo never touches) + vendored miniz.
FOSSILIZE_SOURCES=(
  fossilize.cpp
  fossilize_application_filter.cpp
  varint.cpp
  fossilize_db.cpp
  path.cpp
  miniz/miniz.c
)
COMMON_INC=(-I"$SRC" -I"$SRC/miniz" -I"$SRC/rapidjson/include" -I"$SRC/khronos")
COMMON_LIBS=(-lpthread -lrt)
CXXBASE=(-std=c++14 -fno-exceptions -fvisibility=hidden)

BUILD_ROOT="$SRC/mayhem-build"
FUZZ_OBJ_DIR="$BUILD_ROOT/fuzz-obj"
TEST_OBJ_DIR="$BUILD_ROOT/test-obj"
mkdir -p "$FUZZ_OBJ_DIR" "$TEST_OBJ_DIR" "$BUILD_ROOT/test"

# ── 1) Sanitized Fossilize library objects (SanCov + ASan/UBSan + DWARF-3). One .o per source,
#       reused by both harnesses/standalones -- no .a archiving needed for a library this small. ──
FUZZ_OBJS=()
for src in "${FOSSILIZE_SOURCES[@]}"; do
  obj="$FUZZ_OBJ_DIR/$(basename "${src%.*}").o"
  case "$src" in
    *.c) compiler=("$CC" -x c) ;;
    *)   compiler=("$CXX" "${CXXBASE[@]}") ;;
  esac
  "${compiler[@]}" $SANITIZER_FLAGS $DEBUG_FLAGS "${COMMON_INC[@]}" -c "$src" -o "$obj"
  FUZZ_OBJS+=("$obj")
done

# Standalone driver object, built once, linked into every harness's -standalone binary. Compiled
# as C (-x c): a C++ harness otherwise mangles its LLVMFuzzerTestOneInput symbol.
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c -x c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_OBJ_DIR/standalone_main.o"

for h in fuzz_foz_db fuzz_state_json; do
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${CXXBASE[@]}" "${COMMON_INC[@]}" \
      "$SRC/mayhem/harnesses/$h.cpp" "${FUZZ_OBJS[@]}" $LIB_FUZZING_ENGINE "${COMMON_LIBS[@]}" \
      -o "/mayhem/$h"

  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${CXXBASE[@]}" "${COMMON_INC[@]}" \
      "$SRC/mayhem/harnesses/$h.cpp" "${FUZZ_OBJS[@]}" "$FUZZ_OBJ_DIR/standalone_main.o" "${COMMON_LIBS[@]}" \
      -o "/mayhem/$h-standalone"

  echo "built $h (+ standalone)"
done

# ── 2) Fossilize's OWN upstream test suite + 3 KAT probes, a SEPARATE clean build tree, the
#       project's NORMAL flags (no sanitizer, no DWARF override) -- an honest, non-triage oracle
#       build. Coexists fine with step 1: separate obj dir, no make-clean/stash dance needed. ────
TEST_OBJS=()
for src in "${FOSSILIZE_SOURCES[@]}"; do
  obj="$TEST_OBJ_DIR/$(basename "${src%.*}").o"
  case "$src" in
    *.c) compiler=("$CC" -x c) ;;
    *)   compiler=("$CXX" "${CXXBASE[@]}") ;;
  esac
  "${compiler[@]}" -O2 $COVERAGE_FLAGS "${COMMON_INC[@]}" -c "$src" -o "$obj"
  TEST_OBJS+=("$obj")
done

declare -A TEST_BINS=(
  [fossilize-test]=test/fossilize_test.cpp
  [varint-test]=test/varint_test.cpp
  [application-info-filter-test]=test/application_info_filter_test.cpp
  [object-cache-test]=test/object_cache_test.cpp
  [futex-test]=test/futex_test.cpp
)
for name in "${!TEST_BINS[@]}"; do
  $CXX -O2 $COVERAGE_FLAGS "${CXXBASE[@]}" "${COMMON_INC[@]}" \
      "${TEST_BINS[$name]}" "${TEST_OBJS[@]}" "${COMMON_LIBS[@]}" \
      -o "$BUILD_ROOT/test/$name"
done

declare -A KAT_BINS=(
  [kat_foz_db]=mayhem/kat/kat_foz_db.cpp
  [kat_state_json]=mayhem/kat/kat_state_json.cpp
)
for name in "${!KAT_BINS[@]}"; do
  $CXX -O2 "${CXXBASE[@]}" "${COMMON_INC[@]}" \
      "${KAT_BINS[$name]}" "${TEST_OBJS[@]}" "${COMMON_LIBS[@]}" \
      -o "$BUILD_ROOT/test/$name"
done
# kat_varint only needs varint.cpp (no exceptions disabled requirement either way).
$CXX -O2 "${CXXBASE[@]}" -I"$SRC" mayhem/kat/kat_varint.cpp varint.cpp -o "$BUILD_ROOT/test/kat_varint"

# All test/KAT binaries MUST be dynamically linked so verify-repo's LD_PRELOAD sabotage shim can
# neuter them -- a statically-linked binary would survive sabotage and make mayhem/test.sh a
# reward-hackable oracle (SPEC 6.3). Plain clang/clang++ links dynamically by default; assert it so
# a toolchain change can't silently flip this.
for bin in "$BUILD_ROOT"/test/*; do
  if ! file "$bin" | grep -q 'dynamically linked'; then
    echo "FATAL: $bin is not dynamically linked -- the sabotage check could not neuter it" >&2
    file "$bin" >&2
    exit 1
  fi
done

echo "build.sh complete:"
ls -la /mayhem/fuzz_foz_db /mayhem/fuzz_state_json \
       /mayhem/fuzz_foz_db-standalone /mayhem/fuzz_state_json-standalone \
       "$BUILD_ROOT"/test/* 2>&1 || true
