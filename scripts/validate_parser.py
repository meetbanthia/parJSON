#!/usr/bin/env python3

import argparse
import json
import subprocess
from pathlib import Path


def count_values(node, counts: dict[str, int]) -> None:
    if isinstance(node, dict):
        counts["object_opens"] += 1
        counts["string_literals"] += len(node.keys())
        counts["colon_count"] += len(node)
        counts["comma_count"] += max(0, len(node) - 1)
        for key, value in node.items():
            count_values(value, counts)
    elif isinstance(node, list):
        counts["array_opens"] += 1
        counts["comma_count"] += max(0, len(node) - 1)
        for value in node:
            count_values(value, counts)
    elif isinstance(node, str):
        counts["string_literals"] += 1
    elif isinstance(node, bool):
        if node:
            counts["true_literals"] += 1
        else:
            counts["false_literals"] += 1
    elif node is None:
        counts["null_literals"] += 1
    elif isinstance(node, (int, float)):
        counts["number_literals"] += 1


def max_depth(node) -> int:
    if isinstance(node, dict):
        if not node:
            return 1
        return 1 + max(max_depth(value) for value in node.values())
    if isinstance(node, list):
        if not node:
            return 1
        return 1 + max(max_depth(value) for value in node)
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate parJSON summary counts against Python json.")
    parser.add_argument("--binary", type=Path, default=Path("./parjson"))
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--strategy", choices=["seq", "omp"], default="omp")
    parser.add_argument("--threads", type=int, default=4)
    args = parser.parse_args()

    binary = args.binary.resolve()
    input_path = args.input.resolve()
    summary_path = Path("/tmp/parjson-validate-summary.json")

    completed = subprocess.run(
        [
            str(binary),
            "--input",
            str(input_path),
            "--output",
            str(summary_path),
            "--mode",
            "summary",
            "--strategy",
            args.strategy,
            "--threads",
            str(args.threads),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    assert completed.returncode == 0

    parser_summary = json.loads(summary_path.read_text(encoding="utf-8"))
    document = json.loads(input_path.read_text(encoding="utf-8"))

    counts = {
        "object_opens": 0,
        "array_opens": 0,
        "string_literals": 0,
        "number_literals": 0,
        "true_literals": 0,
        "false_literals": 0,
        "null_literals": 0,
        "colon_count": 0,
        "comma_count": 0,
    }
    count_values(document, counts)
    counts["max_depth"] = max_depth(document)

    mismatches = []
    for key, expected in counts.items():
        observed = parser_summary.get(key)
        if observed != expected:
            mismatches.append((key, expected, observed))

    if mismatches:
        for key, expected, observed in mismatches:
            print(f"Mismatch for {key}: expected {expected}, observed {observed}")
        raise SystemExit(1)

    print(f"Validated parser summary for {input_path}")


if __name__ == "__main__":
    main()
