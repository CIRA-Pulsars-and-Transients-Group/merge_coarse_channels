#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
project_dir="$(cd "$script_dir/.." && pwd -P)"
results_parent="$PWD"
runs=3
duration_list="30,100,300"
executable=""
current_dir=""
input_dir=""
coarse_channels=24

usage() {
    cat <<'EOF'
Usage: bash scripts/profile_scaling.sh [options]

Generate and profile 24 contiguous, 8-bit filterbanks at 100 us resolution,
with 128 fine channels per file and -0.01 MHz channel spacing. Each duration
is timed three times by default. Generated inputs and merged outputs are
removed after each duration; timing summaries and logs are retained.
Gnuplot plots median elapsed and CPU times, time per million spectra, and peak
resident memory; error bars show the minimum-to-maximum run range.
Gnuplot with the pngcairo terminal is required.

  --results-dir DIR  Existing parent for a new results directory (default: cwd)
  --durations LIST   Comma-separated whole seconds (default: 30,100,300)
  --runs N           Timed runs per duration (default: 3)
  --executable FILE  Reuse an existing merger instead of building one
  -h, --help         Show this help

The largest default case temporarily needs about 46.1 GB for its input and
output data, in addition to existing files. Sampling is disabled.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --results-dir|--durations|--runs|--executable)
            if [ "$#" -lt 2 ]; then
                echo "ERROR: $1 requires an argument" >&2
                exit 1
            fi
            case "$1" in
                --results-dir) results_parent="$2" ;;
                --durations) duration_list="$2" ;;
                --runs) runs="$2" ;;
                --executable) executable="$2" ;;
            esac
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ ! "$runs" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: --runs must be a positive integer" >&2
    exit 1
fi
if [[ ! "$duration_list" =~ ^[1-9][0-9]*(,[1-9][0-9]*)*$ ]]; then
    echo "ERROR: --durations must be comma-separated positive whole seconds" >&2
    exit 1
fi
IFS=',' read -r -a durations <<< "$duration_list"
for ((i = 0; i < ${#durations[@]}; i++)); do
    for ((j = 0; j < i; j++)); do
        if [ "${durations[$i]}" = "${durations[$j]}" ]; then
            echo "ERROR: duplicate duration: ${durations[$i]}" >&2
            exit 1
        fi
    done
done
if [ ! -d "$results_parent" ]; then
    echo "ERROR: --results-dir must be an existing directory" >&2
    exit 1
fi
results_parent="$(cd "$results_parent" && pwd -P)"
if [ -n "$executable" ]; then
    if [ ! -f "$executable" ] || [ ! -x "$executable" ]; then
        echo "ERROR: --executable must be an executable file" >&2
        exit 1
    fi
    executable="$(cd "$(dirname "$executable")" && pwd -P)/$(basename "$executable")"
fi

if ! command -v gnuplot >/dev/null 2>&1; then
    echo "ERROR: gnuplot is required to generate plots" >&2
    exit 1
fi
if ! gnuplot -e 'set terminal pngcairo' >/dev/null 2>&1; then
    echo "ERROR: gnuplot must support the pngcairo terminal" >&2
    exit 1
fi

results_dir="$(mktemp -d "$results_parent/merge-scaling-XXXXXXXX")"
echo "Results: $results_dir"

cleanup_data() {
    local directory i
    if [ -n "$current_dir" ]; then
        for directory in "$current_dir"/merge-profile-*; do
            [ -d "$directory" ] || continue
            rm -f -- "$directory/output.fil" "$directory/output.avg_spectrum"
        done
    fi
    if [ -n "$input_dir" ] && [ -d "$input_dir" ]; then
        for ((i = 0; i < coarse_channels; i++)); do
            rm -f -- "$input_dir/filterbank$i.fil"
        done
        rmdir "$input_dir"
    fi
}
trap cleanup_data EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ -z "$executable" ]; then
    cmake -S "$project_dir" -B "$results_dir/build" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF
    cmake --build "$results_dir/build" --config RelWithDebInfo
    executable="$results_dir/build/merge_coarse_channels"
    if [ ! -f "$executable" ]; then
        executable="$results_dir/build/RelWithDebInfo/merge_coarse_channels"
    fi
fi

for duration in "${durations[@]}"; do
    current_dir="$results_dir/${duration}s"
    input_dir="$current_dir/inputs"
    mkdir "$current_dir"
    mkdir "$input_dir"
    echo "Generating ${duration}s of data..."
    python3 "$script_dir/generate_filterbanks.py" \
        --output-dir "$input_dir" --tsamp 0.0001 --duration "$duration" \
        --channels-per-file 128 --coarse-channels "$coarse_channels" --foff -0.01 \
        > "$current_dir/generation.log"
    inputs=""
    for ((i = 0; i < coarse_channels; i++)); do
        if [ -n "$inputs" ]; then
            inputs="$inputs,"
        fi
        inputs="${inputs}${input_dir}/filterbank$i.fil"
    done
    echo "Profiling ${duration}s of data ($runs runs)..."
    python3 "$script_dir/profile_merge.py" --results-dir "$current_dir" \
        --runs "$runs" --sample-seconds 0 --executable "$executable" "$inputs" \
        | tee "$current_dir/profile.log"
    cleanup_data
    current_dir=""
    input_dir=""
done

python3 - "$results_dir" "${durations[@]}" <<'PY'
import csv
import json
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1])
with (root / "comparison.csv").open("w", newline="") as output:
    writer = csv.writer(output)
    writer.writerow(["duration_seconds", "spectra", "median_elapsed_seconds",
                     "median_user_seconds", "median_system_seconds",
                     "median_seconds_per_million_spectra",
                     "min_elapsed_seconds", "max_elapsed_seconds",
                     "min_user_seconds", "max_user_seconds",
                     "min_system_seconds", "max_system_seconds",
                     "min_seconds_per_million_spectra", "max_seconds_per_million_spectra",
                     "median_peak_rss_mib", "min_peak_rss_mib", "max_peak_rss_mib"])
    for duration in sorted(map(int, sys.argv[2:])):
        case = root / f"{duration}s"
        summaries = list(case.glob("merge-profile-*/summary.json"))
        if len(summaries) != 1:
            raise RuntimeError(f"expected one timing summary in {case}")
        runs = json.loads(summaries[0].read_text())["runs"]
        elapsed_times = [run["elapsed_seconds"] for run in runs]
        user_times = [run["user_seconds"] for run in runs]
        system_times = [run["system_seconds"] for run in runs]
        peak_memory = [run["peak_rss_bytes"] / 1048576 for run in runs]
        elapsed = statistics.median(elapsed_times)
        user = statistics.median(user_times)
        system = statistics.median(system_times)
        spectra = duration * 10000
        scale = 1e6 / spectra
        writer.writerow([duration, spectra, elapsed, user, system, elapsed * scale,
                         min(elapsed_times), max(elapsed_times),
                         min(user_times), max(user_times),
                         min(system_times), max(system_times),
                         min(elapsed_times) * scale, max(elapsed_times) * scale,
                         statistics.median(peak_memory), min(peak_memory), max(peak_memory)])
        print(f"{duration}s: median {elapsed:.3f}s "
              f"({elapsed * 1e6 / spectra:.3f}s per million spectra)")
PY

(
    cd "$results_dir"
    gnuplot <<'GNUPLOT'
set terminal pngcairo size 1800,600
set output 'scaling.png'
set datafile separator comma
set multiplot layout 1,3 title 'Merge scaling (median and min-max range)'
set grid
set bars 0.6
set xlabel 'Input duration (seconds)'
set ylabel 'Time (seconds)'
set title 'Elapsed and CPU time'
set key top left
set yrange [0:*]
plot 'comparison.csv' every ::1 using 1:3:7:8 with yerrorlines lw 2 pt 7 title 'Elapsed', \
     '' every ::1 using 1:4:9:10 with yerrorlines lw 2 pt 5 title 'User CPU', \
     '' every ::1 using 1:5:11:12 with yerrorlines lw 2 pt 9 title 'System CPU'
set title 'Time per million spectra'
set ylabel 'Elapsed seconds per million spectra'
unset key
plot 'comparison.csv' every ::1 using 1:6:13:14 with yerrorlines lw 2 pt 7
set title 'Peak resident memory'
set ylabel 'Peak RSS (MiB)'
plot 'comparison.csv' every ::1 using 1:15:16:17 with yerrorlines lw 2 pt 7
unset multiplot
unset output
GNUPLOT
)

echo "Comparison: $results_dir/comparison.csv"
echo "Plot: $results_dir/scaling.png"
echo "Filesystem caching and delayed writes can affect these measurements."
