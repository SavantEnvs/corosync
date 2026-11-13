#!/usr/bin/env bash
#
# Builds corosync's fuzz targets and the oracle. Runs in the commit image as `mayhem` in /mayhem and is
# re-run offline at the PATCH tier. corosync is a daemon (no exec library), so the harnesses link the
# sanitized exec/ objects directly: fuzz_coroparse the configuration parser, fuzz_totemsrp the totem
# single-ring protocol (it #includes exec/totemsrp.c to reach the static state machine).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
M="$SRC/mayhem"

# A fork can carry no upstream tags (CI's checkout then has none), and git-version-gen fails without one:
# the daemon would report an empty version. Pin a fallback only in that case.
if ! build-aux/git-version-gen .tarball-version .gitarchivever >/dev/null 2>&1; then
  printf '%s\n' 3.2.0 > .tarball-version
fi

# The autotools output is gitignored, so a `git clean -ffdX` tree must regenerate it here.
if [ ! -x configure ] || [ ! -f Makefile.in ]; then
  sh ./autogen.sh
fi

# Keep the log readable: make's output goes to a file and is shown only if the step fails.
quiet() { local log; log="$(mktemp)"; "$@" >"$log" 2>&1 || { tail -60 "$log"; rm -f "$log"; return 1; }; rm -f "$log"; }

CONF_ARGS="--disable-systemd --disable-monitoring --disable-watchdog --disable-dbus --disable-snmp --disable-augeas"
INC_DIRS="include include/corosync"

# 1) The sanitized, SanCov-instrumented build of the code the harnesses fuzz (out of tree, gitignored).
rm -rf "$M/build-san" && mkdir -p "$M/build-san"
(cd "$M/build-san" && ../../configure CC="$CC" \
    CFLAGS="$SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS -O1" \
    LDFLAGS="$SANITIZER_FLAGS" --enable-user-flags $CONF_ARGS >configure.log 2>&1 || { tail -50 configure.log; exit 1; })
quiet make -C "$M/build-san/common_lib" -j"$MAYHEM_JOBS"
quiet make -C "$M/build-san/exec" -j"$MAYHEM_JOBS" \
    corosync-coroparse.o corosync-icmap.o corosync-logsys.o corosync-util.o corosync-totemip.o

B="$M/build-san"
CPPFLAGS_ALL="-DHAVE_CONFIG_H -I$B/include -I$SRC/include -I$B/include/corosync -I$SRC/include/corosync -I$B -I$SRC"
OBJS="$B/exec/corosync-icmap.o $B/exec/corosync-logsys.o $B/exec/corosync-util.o $B/exec/corosync-totemip.o $B/common_lib/.libs/libcorosync_common.a"
LIBS="-lqb -lknet"

# 2) LeakSanitizer off (leaks are not the bug class fuzzed here; ASan and UBSan stay on).
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$M/lsan_off.cc" -o "$B/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.o"

# 3) Harnesses, each as a libFuzzer binary and a run-once standalone reproducer.
#    fuzz_totemsrp relaxes only -fsanitize=alignment: totem messages are packed structs whose members
#    (struct srp_addr, ...) are read at odd offsets, which is benign on the supported targets and would
#    abort the very first ring-formation packet.
build_harness() { # <name> <extra compile flags> <objects>
  local name="$1" extra="$2" objs="$3"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $extra -fsanitize=fuzzer-no-link -O1 $CPPFLAGS_ALL \
      -c "$M/$name.c" -o "$B/$name.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $extra $LIB_FUZZING_ENGINE "$B/$name.o" "$B/lsan_off.o" $objs $LIBS \
      -o "$SRC/$name"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $extra "$B/$name.o" "$B/standalone_main.o" "$B/lsan_off.o" $objs $LIBS \
      -o "$SRC/$name-standalone"
}
build_harness fuzz_coroparse "" "$B/exec/corosync-coroparse.o $OBJS"
build_harness fuzz_totemsrp "-fno-sanitize=alignment" "$OBJS"

# 4) The oracle: the normal-flags daemon plus the parser known-answer probe (test.sh only runs them).
rm -rf "$M/build-test" && mkdir -p "$M/build-test"
(cd "$M/build-test" && env -u CFLAGS -u LDFLAGS ../../configure CC="$CC" CFLAGS="-O2 $COVERAGE_FLAGS" \
    LDFLAGS="$COVERAGE_FLAGS" $CONF_ARGS >configure.log 2>&1 || { tail -50 configure.log; exit 1; })
quiet make -C "$M/build-test/common_lib" -j"$MAYHEM_JOBS"
quiet make -C "$M/build-test/exec" -j"$MAYHEM_JOBS" corosync
T="$M/build-test"
$CC -O2 $COVERAGE_FLAGS -DHAVE_CONFIG_H -I$T/include -I$SRC/include -I$T/include/corosync -I$SRC/include/corosync -I$T -I$SRC \
    "$M/kat/coroparse_kat.c" $T/exec/corosync-coroparse.o $T/exec/corosync-icmap.o $T/exec/corosync-logsys.o \
    $T/exec/corosync-util.o $T/exec/corosync-totemip.o $T/common_lib/.libs/libcorosync_common.a $LIBS \
    $COVERAGE_FLAGS -o "$SRC/coroparse_kat"
