#!/usr/bin/env python3

import argparse
import csv
import statistics
import subprocess
import time
from pathlib import Path


def benchmark(
    binary: Path,
    input_path: Path,
    chunk_size: int,
    strategy: str,
    threads: int,
    repeats: int,
) -> list[float]:
    durations = []
    for _ in range(repeats):
        start = time.perf_counter()
        subprocess.run(
            [
                str(binary),
                "--input",
                str(input_path),
                "--output",
                "/tmp/parjson-out.json",
                "--mode",
                "summary",
                "--strategy",
                strategy,
                "--chunk-size",
                str(chunk_size),
                "--threads",
                str(threads),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        durations.append(time.perf_counter() - start)
    return durations


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run repeatable sequential vs OpenMP scaling experiments for parJSON."
    )
    parser.add_argument("--binary", type=Path, default=Path("./parjson"))
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--chunk-size", type=int, default=1 << 14)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4, 8])
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--output", type=Path, default=Path("results/benchmark.csv"))
    parser.add_argument("--label", default="custom")
    args = parser.parse_args()

    binary_path = args.binary.resolve()
    input_path = args.input.resolve()
    output_path = args.output.resolve()

    if args.chunk_size <= 0:
        raise ValueError("--chunk-size must be greater than zero.")
    if args.repeats <= 0:
        raise ValueError("--repeats must be greater than zero.")
    if any(thread <= 0 for thread in args.threads):
        raise ValueError("All thread counts must be greater than zero.")
    if not binary_path.is_file():
        raise FileNotFoundError(f"Binary not found: {binary_path}")
    if not input_path.is_file():
        raise FileNotFoundError(f"Input file not found: {input_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    file_size_bytes = input_path.stat().st_size

    rows = []
    seq_durations = benchmark(binary_path, input_path, args.chunk_size, "seq", 1, args.repeats)
    seq_mean = statistics.mean(seq_durations)
    seq_stdev = statistics.stdev(seq_durations) if len(seq_durations) > 1 else 0.0
    rows.append(
        {
            "label": args.label,
            "input_bytes": file_size_bytes,
            "strategy": "seq",
            "threads": 1,
            "runs": args.repeats,
            "mean_seconds": f"{seq_mean:.6f}",
            "stdev_seconds": f"{seq_stdev:.6f}",
            "throughput_mb_s": f"{((file_size_bytes / (1024 * 1024)) / seq_mean):.3f}",
            "speedup_vs_seq": "1.000",
            "speedup_vs_omp_1": "1.000",
        }
    )

    omp1_mean = None
    for thread_count in args.threads:
        durations = benchmark(binary_path, input_path, args.chunk_size, "omp", thread_count, args.repeats)
        mean = statistics.mean(durations)
        stdev = statistics.stdev(durations) if len(durations) > 1 else 0.0
        if thread_count == 1:
            omp1_mean = mean
        rows.append(
            {
                "label": args.label,
                "input_bytes": file_size_bytes,
                "strategy": "omp",
                "threads": thread_count,
                "runs": args.repeats,
                "mean_seconds": f"{mean:.6f}",
                "stdev_seconds": f"{stdev:.6f}",
                "throughput_mb_s": f"{((file_size_bytes / (1024 * 1024)) / mean):.3f}",
                "speedup_vs_seq": f"{(seq_mean / mean):.3f}",
                "speedup_vs_omp_1": "0.000",
            }
        )

    if omp1_mean is not None:
        for row in rows:
            if row["strategy"] != "omp":
                continue
            mean = float(row["mean_seconds"])
            row["speedup_vs_omp_1"] = f"{(omp1_mean / mean) if mean > 0 else 0.0:.3f}"

    with output_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(
            csv_file,
            fieldnames=[
                "label",
                "input_bytes",
                "strategy",
                "threads",
                "runs",
                "mean_seconds",
                "stdev_seconds",
                "throughput_mb_s",
                "speedup_vs_seq",
                "speedup_vs_omp_1",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote results to {output_path}")
    print(
        "label,input_bytes,strategy,threads,runs,mean_seconds,stdev_seconds,throughput_mb_s,speedup_vs_seq,speedup_vs_omp_1"
    )
    for row in rows:
        print(
            ",".join(
                [
                    str(row["label"]),
                    str(row["input_bytes"]),
                    str(row["strategy"]),
                    str(row["threads"]),
                    str(row["runs"]),
                    row["mean_seconds"],
                    row["stdev_seconds"],
                    row["throughput_mb_s"],
                    row["speedup_vs_seq"],
                    row["speedup_vs_omp_1"],
                ]
            )
        )


if __name__ == "__main__":
    main()
