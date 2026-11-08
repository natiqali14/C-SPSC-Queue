#!/usr/bin/env bash
# run_benchmarks.sh
# Runs the benchmark once per (queue size, payload, queue) and saves the results.
#
# Every benchmark run tests only ONE queue with ONE payload type, so the two
# queues never share a process. For each queue size the order is:
#   rigtorp 8bit, cold_lf 8bit, rigtorp 64bit, cold_lf 64bit, rigtorp big, cold_lf big
#
# Usage:
#   benchmarks/run_benchmarks.sh [path/to/cold_lf_spsc_benchmark]
#   (default binary: build/benchmarks/cold_lf_spsc_benchmark)
#
# Output, in benchmark_results/ (new files every time the script runs):
#   <date_time>_results.csv  one line per run: queue,payload,q_size,min,p10,median,p90,max
#                            (numbers are M items/s, same as the benchmark prints)
#                            then a blank line and a table saying which queue was
#                            faster for every q_size + payload (by median)
#   <date_time>_full.log     everything the benchmark printed, for every run

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCH_BIN="${1:-$ROOT_DIR/build/benchmarks/cold_lf_spsc_benchmark}"

Q_SIZES=(1 8 64 256 1024 8192 32768)
PAYLOADS=(8bit 64bit big)
QUEUES=(rigtorp cold_lf)

if [[ ! -x "$BENCH_BIN" ]]; then
  echo "benchmark binary not found: $BENCH_BIN" >&2
  echo "build it first (Release, with -DCOLD_LF_BUILD_BENCHMARKING=ON)" >&2
  exit 1
fi

RESULTS_DIR="$ROOT_DIR/benchmark_results"
mkdir -p "$RESULTS_DIR"
STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
CSV="$RESULTS_DIR/${STAMP}_results.csv"
LOG="$RESULTS_DIR/${STAMP}_full.log"

echo "queue,payload,q_size,min,p10,median,p90,max" > "$CSV"

total=$(( ${#Q_SIZES[@]} * ${#PAYLOADS[@]} * ${#QUEUES[@]} ))
run_number=0

for q_size in "${Q_SIZES[@]}"; do
  for payload in "${PAYLOADS[@]}"; do
    for queue in "${QUEUES[@]}"; do
      run_number=$(( run_number + 1 ))
      echo "[$run_number/$total] $queue $payload q_size=$q_size"

      output="$("$BENCH_BIN" "$queue" "$payload" "$q_size" 2>&1)"
      status=$?
      echo "===== $queue $payload q_size=$q_size =====" >> "$LOG"
      echo "$output" >> "$LOG"

      # The result line looks like:
      #   Rigtorp Q [std::uint8_t] { min: 2.71 p10: 2.76 median: 2.79 p90: 2.84 max: 2.90 } M items/s
      # This pulls out the five numbers and joins them with commas: 2.71,2.76,2.79,2.84,2.90
      numbers="$(echo "$output" | grep '} M items/s' \
        | sed -E 's/.*min: ([0-9.]+) p10: ([0-9.]+) median: ([0-9.]+) p90: ([0-9.]+) max: ([0-9.]+).*/\1,\2,\3,\4,\5/')"

      if [[ $status -ne 0 || -z "$numbers" ]]; then
        echo "  FAILED (exit code $status), see $LOG" >&2
        numbers="FAILED,,,,"
      fi
      echo "$queue,$payload,$q_size,$numbers" >> "$CSV"
    done
  done
done

# Comparison table, added after a blank line at the end of the CSV.
# One row per (q_size, payload): the two medians and which queue was faster.
# faster_by_percent = how much higher the faster median is than the slower one.
# The table is built into a variable first, because awk must finish reading
# the CSV before we start appending to it.
comparison="$(awk -F, '
  NR == 1 { next }           # skip the header line
  $6 == "" { next }          # skip failed runs, they have no median
  {
    key = $3 "," $2          # q_size,payload
    if (!(key in seen)) { seen[key] = 1; order[++count] = key }
    median[key "," $1] = $6 + 0
  }
  END {
    for (i = 1; i <= count; i++) {
      key = order[i]
      if (!((key ",rigtorp") in median) || !((key ",cold_lf") in median)) {
        print key ",,,missing,"
        continue
      }
      rig = median[key ",rigtorp"]
      lf = median[key ",cold_lf"]
      if (lf > rig)      { faster = "cold_lf"; pct = (lf - rig) / rig * 100 }
      else if (rig > lf) { faster = "rigtorp"; pct = (rig - lf) / lf * 100 }
      else               { faster = "tie";     pct = 0 }
      printf "%s,%.2f,%.2f,%s,%.2f\n", key, rig, lf, faster, pct
    }
  }' "$CSV")"

{
  echo
  echo "q_size,payload,rigtorp_median,cold_lf_median,faster,faster_by_percent"
  echo "$comparison"
} >> "$CSV"

echo
echo "results:  $CSV"
echo "full log: $LOG"
