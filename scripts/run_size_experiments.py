#!/usr/bin/env python3

import argparse
import csv
import statistics
import subprocess
import time
from pathlib import Path


def generate_input(
    generator: Path,
    output_path: Path,
    objects: int,
    text_length: int,
    seed: int,
    profile: str,
) -> None:
    subprocess.run(
        [
            "python3",
            str(generator),
            str(output_path),
            "--objects",
            str(objects),
            "--text-length",
            str(text_length),
            "--seed",
            str(seed),
            "--profile",
            profile,
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def benchmark(
    binary: Path, input_path: Path, chunk_size: int, strategy: str, threads: int, repeats: int
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


def count_chars(path: Path) -> int:
    with path.open("r", encoding="utf-8") as handle:
        return len(handle.read())


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run workload-by-size scaling experiments for parJSON and save CSV results."
    )
    parser.add_argument("--binary", type=Path, default=Path("./parjson"))
    parser.add_argument("--generator", type=Path, default=Path("scripts/generate_benchmark_json.py"))
    parser.add_argument("--chunk-size", type=int, default=1 << 14)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4, 8])
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--objects", type=int, nargs="+", default=[5000, 10000, 20000, 40000])
    parser.add_argument("--profiles", nargs="+", default=["mixed", "flat", "nested", "string-heavy", "numeric-heavy"])
    parser.add_argument("--text-length", type=int, default=256)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--input-dir", type=Path, default=Path("benchmark/size_inputs"))
    parser.add_argument("--output", type=Path, default=Path("results/size_thread_matrix.csv"))
    args = parser.parse_args()

    binary_path = args.binary.resolve()
    generator_path = args.generator.resolve()
    input_dir = args.input_dir.resolve()
    output_path = args.output.resolve()

    if args.chunk_size <= 0:
        raise ValueError("--chunk-size must be greater than zero.")
    if args.repeats <= 0:
        raise ValueError("--repeats must be greater than zero.")
    if args.text_length <= 0:
        raise ValueError("--text-length must be greater than zero.")
    if any(count <= 0 for count in args.objects):
        raise ValueError("All --objects values must be greater than zero.")
    if any(thread <= 0 for thread in args.threads):
        raise ValueError("All --threads values must be greater than zero.")
    if not binary_path.is_file():
        raise FileNotFoundError(f"Binary not found: {binary_path}")
    if not generator_path.is_file():
        raise FileNotFoundError(f"Generator script not found: {generator_path}")

    input_dir.mkdir(parents=True, exist_ok=True)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    rows = []
    for profile in args.profiles:
        for object_count in args.objects:
            input_path = input_dir / f"{profile}_{object_count}.json"
            generate_input(generator_path, input_path, object_count, args.text_length, args.seed, profile)
            file_size_bytes = input_path.stat().st_size
            num_chars = count_chars(input_path)

            seq_durations = benchmark(binary_path, input_path, args.chunk_size, "seq", 1, args.repeats)
            seq_mean = statistics.mean(seq_durations)
            seq_stdev = statistics.stdev(seq_durations) if len(seq_durations) > 1 else 0.0

            rows.append(
                {
                    "profile": profile,
                    "objects": object_count,
                    "num_chars": num_chars,
                    "file_size_bytes": file_size_bytes,
                    "strategy": "seq",
                    "threads": 1,
                    "runs": args.repeats,
                    "mean_seconds": f"{seq_mean:.6f}",
                    "stdev_seconds": f"{seq_stdev:.6f}",
                    "throughput_mb_s": f"{((file_size_bytes / (1024 * 1024)) / seq_mean):.3f}",
                    "speedup_vs_seq": "1.000",
                }
            )

            for thread_count in args.threads:
                durations = benchmark(binary_path, input_path, args.chunk_size, "omp", thread_count, args.repeats)
                mean = statistics.mean(durations)
                stdev = statistics.stdev(durations) if len(durations) > 1 else 0.0
                rows.append(
                    {
                        "profile": profile,
                        "objects": object_count,
                        "num_chars": num_chars,
                        "file_size_bytes": file_size_bytes,
                        "strategy": "omp",
                        "threads": thread_count,
                        "runs": args.repeats,
                        "mean_seconds": f"{mean:.6f}",
                        "stdev_seconds": f"{stdev:.6f}",
                        "throughput_mb_s": f"{((file_size_bytes / (1024 * 1024)) / mean):.3f}",
                        "speedup_vs_seq": f"{(seq_mean / mean):.3f}",
                    }
                )

    with output_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(
            csv_file,
            fieldnames=[
                "profile",
                "objects",
                "num_chars",
                "file_size_bytes",
                "strategy",
                "threads",
                "runs",
                "mean_seconds",
                "stdev_seconds",
                "throughput_mb_s",
                "speedup_vs_seq",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote results to {output_path}")
    print(
        "profile,objects,num_chars,file_size_bytes,strategy,threads,runs,mean_seconds,stdev_seconds,throughput_mb_s,speedup_vs_seq"
    )
    for row in rows:
        print(
            ",".join(
                [
                    str(row["profile"]),
                    str(row["objects"]),
                    str(row["num_chars"]),
                    str(row["file_size_bytes"]),
                    str(row["strategy"]),
                    str(row["threads"]),
                    str(row["runs"]),
                    row["mean_seconds"],
                    row["stdev_seconds"],
                    row["throughput_mb_s"],
                    row["speedup_vs_seq"],
                ]
            )
        )


if __name__ == "__main__":
    main()
