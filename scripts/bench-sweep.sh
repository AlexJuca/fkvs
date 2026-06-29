#!/usr/bin/env bash
#
# bench-sweep.sh — find fkvs's throughput-at-p99 "knee".
#
# Sweeps the open-model benchmark (-R) across a list of target rates and, for
# each, records achieved throughput and coordinated-omission-corrected latency
# percentiles. Reports the highest rate the server sustains within a p99 SLO —
# the only honest capacity number at multi-million req/s (a bare peak hides the
# tail). See docs/benchmarking.md.
#
# The server should already be running (ideally on a *separate* machine, or at
# least pinned to cores disjoint from this client). Use -L to launch a local
# server for a quick smoke test.
#
# Usage:
#   scripts/bench-sweep.sh [options] [rate ...]
#
# Options:
#   -H HOST    server host         (default 127.0.0.1)
#   -p PORT    server port         (default 5995)
#   -c N       concurrent clients  (default 16)
#   -t TYPE    command: ping|set   (default set)
#   -r         unique key per request (stresses allocation/cache)
#   -d SECS    measurement seconds per rate (default 4)
#   -s MS      p99 SLO in milliseconds (default 1.0)
#   -L         launch a local ./fkvs-server -c server.conf for the run
#   -b PATH    path to fkvs-benchmark (default: alongside this script's repo)
#   -h         show this help
#
# Positional args override the default rate list. Rates accept k/m suffixes.
#
# Examples:
#   scripts/bench-sweep.sh                       # default sweep, SET, p99<1ms
#   scripts/bench-sweep.sh -t set -r -s 2 200k 400k 800k 1m 2m 4m
#   scripts/bench-sweep.sh -H 10.0.0.5 -c 64 -d 10 -s 0.5 1m 2m 3m 4m
#
set -u

# --- locate repo / binary ---------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

HOST=127.0.0.1
PORT=5995
CLIENTS=16
CMD=set
RANDOM_KEYS=0
DURATION=4
SLO_MS=1.0
LAUNCH_LOCAL=0
BENCH="$REPO_DIR/fkvs-benchmark"

usage() { sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

# parse a rate token with optional k/m/g suffix -> integer req/s
# (kept POSIX/bash-3.2 friendly: macOS ships bash 3.2, no ${var,,})
parse_rate() {
  local t mult=1
  t="$(printf '%s' "$1" | tr 'A-Z' 'a-z')"
  case "$t" in
    *k) mult=1000;       t="${t%k}";;
    *m) mult=1000000;    t="${t%m}";;
    *g) mult=1000000000; t="${t%g}";;
  esac
  # support decimals like 1.5m
  awk -v v="$t" -v m="$mult" 'BEGIN{printf "%d", (v*m)+0.5}'
}

RATES=()
while [ $# -gt 0 ]; do
  case "$1" in
    -H) HOST="$2"; shift 2;;
    -p) PORT="$2"; shift 2;;
    -c) CLIENTS="$2"; shift 2;;
    -t) CMD="$2"; shift 2;;
    -r) RANDOM_KEYS=1; shift;;
    -d) DURATION="$2"; shift 2;;
    -s) SLO_MS="$2"; shift 2;;
    -L) LAUNCH_LOCAL=1; shift;;
    -b) BENCH="$2"; shift 2;;
    -h|--help) usage 0;;
    -*) echo "unknown option: $1" >&2; usage 1;;
    *) RATES+=("$(parse_rate "$1")"); shift;;
  esac
done

if [ ${#RATES[@]} -eq 0 ]; then
  RATES=(50000 100000 200000 400000 800000 1200000 1600000 2000000 3000000 4000000)
fi

[ -x "$BENCH" ] || { echo "benchmark binary not found/executable: $BENCH" >&2
                     echo "build it (make -f Makefile.fkvs setup-and-build) or pass -b PATH" >&2
                     exit 1; }

SLO_US="$(awk -v ms="$SLO_MS" 'BEGIN{printf "%d", ms*1000}')"
RFLAG=""; [ "$RANDOM_KEYS" = 1 ] && RFLAG="-r"

# --- optional local server --------------------------------------------------
SRV_PID=""
cleanup() { [ -n "$SRV_PID" ] && kill "$SRV_PID" 2>/dev/null; }
trap cleanup EXIT INT TERM

if [ "$LAUNCH_LOCAL" = 1 ]; then
  echo "Launching local server: $REPO_DIR/fkvs-server -c server.conf"
  ( cd "$REPO_DIR" && ./fkvs-server -c server.conf >/tmp/fkvs-sweep-srv.log 2>&1 ) &
  SRV_PID=$!
  sleep 1
fi

# --- sweep ------------------------------------------------------------------
echo "fkvs throughput-at-p99 sweep"
echo "  server   : $HOST:$PORT   clients: $CLIENTS   cmd: $CMD ${RFLAG}"
echo "  per-rate : ${DURATION}s   SLO: p99 < ${SLO_MS} ms (${SLO_US} us)"
echo
printf "%-12s %-12s %-7s %-10s %-10s %-10s %-8s\n" \
       "target" "achieved" "hit%" "p50(us)" "p99(us)" "p99.9(us)" "verdict"
printf "%-12s %-12s %-7s %-10s %-10s %-10s %-8s\n" \
       "------" "--------" "----" "-------" "-------" "---------" "-------"

knee_rate=0; knee_ach=0; knee_p99=0
for R in "${RATES[@]}"; do
  N=$(( R * DURATION ))
  out="$("$BENCH" -h "$HOST" -p "$PORT" -c "$CLIENTS" -t "$CMD" $RFLAG -R "$R" -n "$N" 2>/dev/null)"

  ach="$(printf '%s\n' "$out" | awk -F'Achieved:' '/Achieved:/{print $2}' | awk '{print $1}')"
  p50="$(printf '%s\n' "$out" | awk '$1=="p50"{print $3}')"
  p99="$(printf '%s\n' "$out" | awk '$1=="p99"{print $3}')"
  p999="$(printf '%s\n' "$out" | awk '$1=="p99.9"{print $3}')"
  ach="${ach:-0}"; p50="${p50:-0}"; p99="${p99:-0}"; p999="${p999:-0}"

  hit="$(awk -v a="$ach" -v t="$R" 'BEGIN{ if (t>0) printf "%.1f", a*100.0/t; else printf "0.0" }')"
  # pass = p99 within SLO AND achieved within 5% of target (kept up)
  verdict="$(awk -v p="$p99" -v slo="$SLO_US" -v a="$ach" -v t="$R" \
      'BEGIN{ if (p<=slo && a>=0.95*t) print "OK"; else print "FAIL" }')"

  printf "%-12s %-12s %-7s %-10s %-10s %-10s %-8s\n" \
         "$R" "$ach" "${hit}%" "$p50" "$p99" "$p999" "$verdict"

  if [ "$verdict" = "OK" ]; then
    knee_rate="$R"; knee_ach="$ach"; knee_p99="$p99"
  fi
done

echo
if [ "$knee_rate" -gt 0 ]; then
  echo "Knee: ${knee_rate} req/s sustained at p99 < ${SLO_MS} ms" \
       "(achieved ${knee_ach} req/s, p99 ${knee_p99} us)"
else
  echo "Knee: none of the swept rates held p99 < ${SLO_MS} ms." \
       "Lower the start rate, raise -c, or relax -s."
fi
