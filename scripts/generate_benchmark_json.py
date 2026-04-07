#!/usr/bin/env python3

import argparse
import json
import random
import string
from pathlib import Path


def random_text(rng: random.Random, length: int) -> str:
    alphabet = string.ascii_letters + string.digits + "     _-/:"
    return "".join(rng.choice(alphabet) for _ in range(length)).strip() or "x"


def flat_payload(rng: random.Random, index: int, text_length: int) -> dict[str, object]:
    return {
        "id": index,
        "name": random_text(rng, 12),
        "active": index % 2 == 0,
        "score": round(rng.uniform(0, 1000), 3),
        "message": random_text(rng, text_length),
        "owner": random_text(rng, 10),
        "status": random_text(rng, 8),
    }


def nested_payload(rng: random.Random, index: int, text_length: int) -> dict[str, object]:
    return {
        "id": index,
        "meta": {
            "created_by": random_text(rng, 10),
            "region": random_text(rng, 6),
            "flags": [index % 2 == 0, index % 3 == 0, index % 5 == 0],
        },
        "payload": {
            "title": random_text(rng, 18),
            "details": {
                "summary": random_text(rng, text_length // 2),
                "description": random_text(rng, text_length),
                "history": [
                    {
                        "version": version,
                        "comment": random_text(rng, 24),
                    }
                    for version in range(3)
                ],
            },
        },
        "metrics": [round(rng.uniform(-500, 500), 4) for _ in range(10)],
    }


def string_heavy_payload(rng: random.Random, index: int, text_length: int) -> dict[str, object]:
    return {
        "id": index,
        "title": random_text(rng, text_length // 2),
        "subtitle": random_text(rng, text_length // 2),
        "message": random_text(rng, text_length),
        "details": random_text(rng, text_length),
        "tags": [random_text(rng, 16) for _ in range(8)],
        "notes": [random_text(rng, 20) for _ in range(6)],
    }


def numeric_heavy_payload(rng: random.Random, index: int, text_length: int) -> dict[str, object]:
    width = max(16, text_length // 8)
    return {
        "id": index,
        "name": random_text(rng, 10),
        "samples": [round(rng.uniform(-100000, 100000), 6) for _ in range(width)],
        "histogram": {f"b{bucket}": rng.randint(0, 1000000) for bucket in range(12)},
        "ratios": [round(rng.random(), 8) for _ in range(width)],
        "active": index % 2 == 0,
    }


def build_dataset(profile: str, objects: int, text_length: int, seed: int) -> object:
    rng = random.Random(seed)

    builders = {
        "flat": flat_payload,
        "nested": nested_payload,
        "string-heavy": string_heavy_payload,
        "numeric-heavy": numeric_heavy_payload,
    }

    if profile == "mixed":
        names = ["flat", "nested", "string-heavy", "numeric-heavy"]
        return {
            "profile": profile,
            "records": [
                builders[names[index % len(names)]](rng, index, text_length) for index in range(objects)
            ],
        }

    return [builders[profile](rng, index, text_length) for index in range(objects)]


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate synthetic JSON files for parJSON benchmarks."
    )
    parser.add_argument("output", type=Path, help="Path to the generated JSON file.")
    parser.add_argument(
        "--objects",
        type=int,
        default=20000,
        help="Number of top-level records to generate.",
    )
    parser.add_argument(
        "--text-length",
        type=int,
        default=256,
        help="Approximate length of generated string fields.",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Random seed so benchmark inputs are reproducible.",
    )
    parser.add_argument(
        "--profile",
        choices=["mixed", "flat", "nested", "string-heavy", "numeric-heavy"],
        default="mixed",
        help="Workload shape to generate.",
    )
    args = parser.parse_args()

    if args.objects <= 0:
        raise ValueError("--objects must be greater than zero.")
    if args.text_length <= 0:
        raise ValueError("--text-length must be greater than zero.")

    dataset = build_dataset(args.profile, args.objects, args.text_length, args.seed)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as output:
        json.dump(dataset, output, separators=(",", ":"))
        output.write("\n")


if __name__ == "__main__":
    main()
