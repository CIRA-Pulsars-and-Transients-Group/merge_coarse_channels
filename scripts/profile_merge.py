#!/usr/bin/env python3
"""Profile repeated merges of a representative set of filterbank files."""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]


def positive_int(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return number


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build and profile merges of a comma-separated list of input files.",
        epilog="Pass merger options after --, for example: -- -o -r 32/27 -F",
    )
    parser.add_argument("--runs", type=positive_int, default=3, help="timed runs (default: 3)")
    parser.add_argument(
        "--results-dir", type=pathlib.Path, default=pathlib.Path.cwd(),
        help="existing parent directory for a new results directory (default: current directory)",
    )
    parser.add_argument(
        "--executable", type=pathlib.Path,
        help="use an existing executable instead of building RelWithDebInfo",
    )
    parser.add_argument(
        "--sample-seconds", type=int, default=10,
        help="sample a separate run with ps for this many seconds; 0 disables sampling",
    )
    parser.add_argument("inputs", help="comma-separated input filterbank files")
    parser.add_argument("merge_options", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.sample_seconds < 0:
        parser.error("--sample-seconds must be non-negative")
    if args.sample_seconds and not shutil.which("ps"):
        parser.error("ps command not found; use --sample-seconds 0 to disable sampling")
    if not args.results_dir.is_dir():
        parser.error("--results-dir must be an existing directory")
    if args.executable and not args.executable.is_file():
        parser.error("--executable must be an existing file")

    paths = [path.strip() for path in args.inputs.split(",")]
    if not all(paths):
        parser.error("inputs must be a non-empty comma-separated list")
    inputs = [pathlib.Path(path).resolve() for path in paths]
    for path in inputs:
        if not path.is_file():
            parser.error(f"input file not found: {path}")
    args.inputs = ",".join(map(str, inputs))

    options = args.merge_options
    if options and options[0] == "--":
        options = options[1:]
    merge_parser = argparse.ArgumentParser(add_help=False)
    merge_parser.add_argument("-o", action="store_true")
    merge_parser.add_argument("-F", action="store_true")
    merge_parser.add_argument("-r")
    merge_parser.add_argument("-s")
    merge_parser.parse_args(options)
    args.merge_options = options
    return args


def main():
    args = parse_args()
    if sys.platform == "darwin":
        rss_multiplier = 1
    elif sys.platform.startswith("linux"):
        rss_multiplier = 1024
    else:
        raise RuntimeError("peak resident memory is supported only on Linux and macOS")
    results = pathlib.Path(tempfile.mkdtemp(prefix="merge-profile-", dir=args.results_dir)).resolve()
    print(f"Results: {results}", flush=True)

    if args.executable:
        executable = args.executable.resolve()
    else:
        build = results / "build"
        subprocess.run(
            ["cmake", "-S", str(ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
             "-DBUILD_TESTING=OFF"],
            check=True,
        )
        subprocess.run(["cmake", "--build", str(build), "--config", "RelWithDebInfo"], check=True)
        executable = build / "merge_coarse_channels"
        if not executable.is_file():
            executable = build / "RelWithDebInfo" / "merge_coarse_channels"

    command = [
        str(executable), args.inputs, *args.merge_options, "-O", str(results / "output.fil")
    ]
    measurements = []
    for run in range(1, args.runs + 1):
        log_path = results / f"run-{run}.log"
        with log_path.open("w") as log:
            start = time.perf_counter()
            process = subprocess.Popen(command, cwd=results, stdout=log, stderr=subprocess.STDOUT)
            _, status, usage = os.wait4(process.pid, 0)
            elapsed = time.perf_counter() - start
            process.returncode = os.waitstatus_to_exitcode(status)
        if process.returncode:
            raise RuntimeError(f"run {run} failed (exit {process.returncode}); see {log_path}")
        measurements.append({
            "elapsed_seconds": elapsed,
            "user_seconds": usage.ru_utime,
            "system_seconds": usage.ru_stime,
            "peak_rss_bytes": usage.ru_maxrss * rss_multiplier,
        })
        print(
            f"Run {run}: elapsed {elapsed:.3f}s, user {usage.ru_utime:.3f}s, "
            f"system {usage.ru_stime:.3f}s, "
            f"peak RSS {usage.ru_maxrss * rss_multiplier / 1048576:.2f} MiB", flush=True,
        )

    summary = {"command": command, "runs": measurements}
    (results / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    if args.sample_seconds:
        sample_path = results / "sample.csv"
        run_log = results / "sample-run.log"
        print(f"Sampling a separate run for up to {args.sample_seconds} seconds...", flush=True)
        samples = []
        with run_log.open("w") as output:
            process = subprocess.Popen(command, cwd=results, stdout=output, stderr=subprocess.STDOUT)
            start = time.perf_counter()
            try:
                while process.poll() is None:
                    elapsed = time.perf_counter() - start
                    if elapsed >= args.sample_seconds:
                        break
                    sample = subprocess.run(
                        ["ps", "-p", str(process.pid), "-o", "state=", "-o", "pcpu=",
                         "-o", "rss="],
                        capture_output=True, text=True,
                    )
                    if sample.returncode:
                        if process.poll() is not None:
                            break
                        raise RuntimeError(f"ps failed: {sample.stderr.strip()}")
                    fields = sample.stdout.split()
                    if len(fields) != 3:
                        raise RuntimeError(f"unexpected ps output: {sample.stdout!r}")
                    samples.append((elapsed, *fields))
                    try:
                        process.wait(timeout=min(1.0, args.sample_seconds - elapsed))
                    except subprocess.TimeoutExpired:
                        pass
            finally:
                status = process.wait()
        if status:
            raise RuntimeError(f"sampled merge failed (exit {status}); see {run_log}")
        if not samples:
            raise RuntimeError("no process samples collected; the merge may have finished too quickly")
        with sample_path.open("w") as output:
            output.write("elapsed_seconds,state,cpu_percent,rss_kib\n")
            for elapsed, state, cpu, rss in samples:
                output.write(f"{elapsed:.3f},{state},{cpu},{rss}\n")
        print(f"Process samples: {sample_path} ({len(samples)} samples)")

    print(f"Timing summary: {results / 'summary.json'}")
    print("Compare repeated runs cautiously: filesystem caching can affect timings.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
