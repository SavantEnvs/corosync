#!/usr/bin/env bash
#
# mayhem/test.sh — the functional oracle for corosync's configuration parser and daemon CLI. It RUNS what
# mayhem/build.sh built with the project's normal flags (never compiles): a known-answer probe that dumps
# the icmap a fixture corosync.conf parses to (diffed against a golden file), the exact parser error for a
# truncated config, and the real `corosync` daemon's -v / -t paths. Every probe is unconditional and checks
# output, so a program that just exits 0 fails.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
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


fail=0; pass=0
ok()   { pass=$((pass + 1)); echo "PASS $1"; }
bad()  { fail=$((fail + 1)); echo "FAIL $1" >&2; }
need() { [ -x "$1" ] || { echo "FATAL: $1 missing; mayhem/build.sh should have built it" >&2; emit_ctrf corosync-kat 0 1 0; exit 1; }; }

KAT=./coroparse_kat
DAEMON=mayhem/build-test/exec/corosync
need "$KAT"; need "$DAEMON"

# 1) the fixture config parses to exactly the golden icmap dump
out="$("$KAT" mayhem/kat/good.conf 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && [ "$out" = "$(cat mayhem/kat/good.expected)" ]; then ok "parser dumps the golden icmap"
else bad "parser dump differs from mayhem/kat/good.expected (rc=$rc)"; diff <(printf '%s\n' "$out") mayhem/kat/good.expected | head -20 >&2; fi

# 2) spot values, so a regenerated or emptied golden file cannot hide a regression
for line in \
  "totem.cluster_name (str) = katcluster" \
  "totem.token (u32) = 3000" \
  "nodelist.node.1.ring0_addr (str) = 192.168.0.2" \
  "nodelist.node.2.quorum_votes (u32) = 2" \
  "quorum.expected_votes (u32) = 3" \
  "logging.logger_subsys.QUORUM.debug (str) = on"; do
  if printf '%s\n' "$out" | grep -qxF "$line"; then ok "parsed: $line"; else bad "missing parsed value: $line"; fi
done

# 3) a truncated config is rejected with the exact line-numbered parser error
out="$("$KAT" mayhem/kat/bad.conf 2>&1)"; rc=$?
if [ "$rc" -eq 1 ] && [ "$out" = "parse error: parser error: mayhem/kat/bad.conf:7: Missing closing brace" ]; then
  ok "truncated config rejected with the exact error"
else bad "bad.conf: rc=$rc out=$out"; fi

# 4) the daemon's version banner
out="$("$DAEMON" -v 2>&1)"; rc=$?
first="$(printf '%s\n' "$out" | sed -n 1p)"; second="$(printf '%s\n' "$out" | sed -n 2p)"
if [ "$rc" -eq 0 ] && printf '%s\n' "$first" | grep -qE "^Corosync Cluster Engine, version '[0-9]+\.[0-9]+[^']*'$" \
   && [ "$second" = "Copyright (c) 2006-2021 Red Hat, Inc." ] && printf '%s\n' "$out" | grep -q '^Built-in features:'; then
  ok "corosync -v banner"
else bad "corosync -v: rc=$rc first=$first"; fi

# 5) the daemon's config test (-t) reports the same parser error through its own exit status
out="$("$DAEMON" -t -c mayhem/kat/bad.conf 2>&1)"; rc=$?
if [ "$rc" -eq 8 ] && printf '%s\n' "$out" | grep -qF "parser error: mayhem/kat/bad.conf:7: Missing closing brace"; then
  ok "corosync -t rejects the truncated config (exit 8)"
else bad "corosync -t bad.conf: rc=$rc out=$out"; fi

emit_ctrf corosync-kat "$pass" "$fail" 0
