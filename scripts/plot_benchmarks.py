#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path


PALETTE = ["#1f77b4", "#d62728", "#2ca02c", "#ff7f0e", "#9467bd", "#8c564b"]


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def scale(value: float, low: float, high: float, out_low: float, out_high: float) -> float:
    if high <= low:
        return (out_low + out_high) / 2.0
    ratio = (value - low) / (high - low)
    return out_low + ratio * (out_high - out_low)


def svg_header(width: int, height: int) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<style>text { font-family: Helvetica, Arial, sans-serif; fill: #222; } .small { font-size: 12px; } .title { font-size: 18px; font-weight: 600; } .axis { stroke: #444; stroke-width: 1.2; } .grid { stroke: #d8d8d8; stroke-width: 1; } .legend { font-size: 12px; }</style>',
    ]


def polyline(points: list[tuple[float, float]], color: str) -> str:
    serialized = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    return f'<polyline fill="none" stroke="{color}" stroke-width="2.5" points="{serialized}"/>'


def draw_series_markers(points: list[tuple[float, float]], color: str) -> list[str]:
    return [f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.8" fill="{color}"/>' for x, y in points]


def add_axes(parts: list[str], left: float, top: float, width: float, height: float, x_label: str, y_label: str, title: str) -> None:
    parts.append(f'<text x="{left}" y="{top - 18}" class="title">{title}</text>')
    parts.append(f'<line x1="{left}" y1="{top + height}" x2="{left + width}" y2="{top + height}" class="axis"/>')
    parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + height}" class="axis"/>')
    parts.append(f'<text x="{left + width / 2}" y="{top + height + 36}" text-anchor="middle" class="small">{x_label}</text>')
    parts.append(
        f'<text x="{left - 56}" y="{top + height / 2}" text-anchor="middle" transform="rotate(-90 {left - 56},{top + height / 2})" class="small">{y_label}</text>'
    )


def add_ticks(
    parts: list[str],
    left: float,
    top: float,
    width: float,
    height: float,
    x_values: list[float],
    y_values: list[float],
    x_formatter,
    y_formatter,
) -> None:
    x_min, x_max = min(x_values), max(x_values)
    y_min, y_max = min(y_values), max(y_values)

    for tick in range(5):
        x_value = x_min + (x_max - x_min) * tick / 4 if x_max > x_min else x_min
        x = scale(x_value, x_min, x_max, left, left + width)
        parts.append(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{top + height}" class="grid"/>')
        parts.append(f'<text x="{x:.1f}" y="{top + height + 18}" text-anchor="middle" class="small">{x_formatter(x_value)}</text>')

        y_value = y_min + (y_max - y_min) * tick / 4 if y_max > y_min else y_min
        y = scale(y_value, y_min, y_max, top + height, top)
        parts.append(f'<line x1="{left}" y1="{y:.1f}" x2="{left + width}" y2="{y:.1f}" class="grid"/>')
        parts.append(f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end" class="small">{y_formatter(y_value)}</text>')


def write_svg(path: Path, parts: list[str]) -> None:
    parts.append("</svg>")
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def plot_thread_scaling(rows: list[dict[str, str]], output_dir: Path) -> None:
    seq_rows = [row for row in rows if row["strategy"] == "seq"]
    omp_rows = [row for row in rows if row["strategy"] == "omp"]
    omp_rows.sort(key=lambda row: int(row["threads"]))

    width = 1100
    height = 420
    parts = svg_header(width, height)

    panels = [
        (70, 70, 420, 260, "Runtime vs Thread Count", "Threads", "Mean runtime (s)"),
        (590, 70, 420, 260, "OpenMP Speedup vs Sequential", "Threads", "Speedup"),
    ]

    seq_mean = float(seq_rows[0]["mean_seconds"]) if seq_rows else 0.0

    thread_values = [float(row["threads"]) for row in omp_rows]
    runtime_values = [float(row["mean_seconds"]) for row in omp_rows] + ([seq_mean] if seq_rows else [])
    speedup_values = [float(row["speedup_vs_seq"]) for row in omp_rows]

    for panel_index, (left, top, panel_width, panel_height, title, x_label, y_label) in enumerate(panels):
        y_values = runtime_values if panel_index == 0 else speedup_values
        add_axes(parts, left, top, panel_width, panel_height, x_label, y_label, title)
        add_ticks(
            parts,
            left,
            top,
            panel_width,
            panel_height,
            thread_values,
            y_values,
            lambda value: f"{int(round(value))}",
            lambda value: f"{value:.2f}",
        )

    if seq_rows:
        y = scale(seq_mean, min(runtime_values), max(runtime_values), 70 + 260, 70)
        parts.append(f'<line x1="70" y1="{y:.1f}" x2="490" y2="{y:.1f}" stroke="#666666" stroke-width="2" stroke-dasharray="7 5"/>')
        parts.append('<text x="485" y="62" text-anchor="end" class="legend">Sequential baseline</text>')

    runtime_points = [
        (
            scale(float(row["threads"]), min(thread_values), max(thread_values), 70, 490),
            scale(float(row["mean_seconds"]), min(runtime_values), max(runtime_values), 330, 70),
        )
        for row in omp_rows
    ]
    parts.append(polyline(runtime_points, PALETTE[0]))
    parts.extend(draw_series_markers(runtime_points, PALETTE[0]))

    speedup_points = [
        (
            scale(float(row["threads"]), min(thread_values), max(thread_values), 590, 1010),
            scale(float(row["speedup_vs_seq"]), min(speedup_values), max(speedup_values), 330, 70),
        )
        for row in omp_rows
    ]
    parts.append(polyline(speedup_points, PALETTE[1]))
    parts.extend(draw_series_markers(speedup_points, PALETTE[1]))

    write_svg(output_dir / "thread_scaling.svg", parts)


def plot_size_scaling(rows: list[dict[str, str]], output_dir: Path) -> None:
    profiles = sorted({row["profile"] for row in rows if row["strategy"] == "omp"})
    width = 1200
    row_height = 260
    height = 60 + row_height * max(1, len(profiles))
    parts = svg_header(width, height)

    for profile_index, profile in enumerate(profiles):
        profile_rows = [row for row in rows if row["profile"] == profile and row["strategy"] == "omp" and int(row["threads"]) in {1, 4, 8}]
        if not profile_rows:
            continue

        grouped: dict[int, list[dict[str, str]]] = {}
        for row in profile_rows:
            grouped.setdefault(int(row["threads"]), []).append(row)
        for values in grouped.values():
            values.sort(key=lambda row: int(row["file_size_bytes"]))

        top = 50 + profile_index * row_height
        left_runtime = 70
        left_speedup = 650
        panel_width = 420
        panel_height = 150

        size_values = [float(row["file_size_bytes"]) / (1024 * 1024) for row in profile_rows]
        runtime_values = [float(row["mean_seconds"]) for row in profile_rows]
        speedup_values = [float(row["speedup_vs_seq"]) for row in profile_rows]

        add_axes(parts, left_runtime, top, panel_width, panel_height, "Input size (MB)", "Mean runtime (s)", f"{profile}: runtime vs size")
        add_ticks(
            parts,
            left_runtime,
            top,
            panel_width,
            panel_height,
            size_values,
            runtime_values,
            lambda value: f"{value:.2f}",
            lambda value: f"{value:.2f}",
        )

        add_axes(parts, left_speedup, top, panel_width, panel_height, "Input size (MB)", "Speedup", f"{profile}: speedup vs seq")
        add_ticks(
            parts,
            left_speedup,
            top,
            panel_width,
            panel_height,
            size_values,
            speedup_values,
            lambda value: f"{value:.2f}",
            lambda value: f"{value:.2f}",
        )

        for color_index, (thread_count, thread_rows) in enumerate(sorted(grouped.items())):
            color = PALETTE[color_index % len(PALETTE)]
            runtime_points = [
                (
                    scale(float(row["file_size_bytes"]) / (1024 * 1024), min(size_values), max(size_values), left_runtime, left_runtime + panel_width),
                    scale(float(row["mean_seconds"]), min(runtime_values), max(runtime_values), top + panel_height, top),
                )
                for row in thread_rows
            ]
            speedup_points = [
                (
                    scale(float(row["file_size_bytes"]) / (1024 * 1024), min(size_values), max(size_values), left_speedup, left_speedup + panel_width),
                    scale(float(row["speedup_vs_seq"]), min(speedup_values), max(speedup_values), top + panel_height, top),
                )
                for row in thread_rows
            ]
            parts.append(polyline(runtime_points, color))
            parts.extend(draw_series_markers(runtime_points, color))
            parts.append(polyline(speedup_points, color))
            parts.extend(draw_series_markers(speedup_points, color))
            legend_x = left_speedup + panel_width - 110
            legend_y = top + 18 + color_index * 18
            parts.append(f'<line x1="{legend_x}" y1="{legend_y}" x2="{legend_x + 20}" y2="{legend_y}" stroke="{color}" stroke-width="3"/>')
            parts.append(f'<text x="{legend_x + 28}" y="{legend_y + 4}" class="legend">{thread_count} threads</text>')

    write_svg(output_dir / "size_scaling_by_profile.svg", parts)


def plot_throughput(rows: list[dict[str, str]], output_dir: Path) -> None:
    omp_rows = [row for row in rows if row["strategy"] == "omp"]
    if not omp_rows:
        raise ValueError("No OpenMP rows found in size benchmark CSV.")
    target_thread = max(int(row["threads"]) for row in omp_rows)
    target_rows = [row for row in omp_rows if int(row["threads"]) == target_thread]
    profiles = sorted({row["profile"] for row in target_rows})

    width = 900
    height = 420
    left = 70
    top = 70
    panel_width = 760
    panel_height = 260

    size_values = [float(row["file_size_bytes"]) / (1024 * 1024) for row in target_rows]
    throughput_values = [float(row["throughput_mb_s"]) for row in target_rows]

    parts = svg_header(width, height)
    add_axes(
        parts,
        left,
        top,
        panel_width,
        panel_height,
        "Input size (MB)",
        "Throughput (MB/s)",
        f"{target_thread}-thread OpenMP throughput by workload",
    )
    add_ticks(
        parts,
        left,
        top,
        panel_width,
        panel_height,
        size_values,
        throughput_values,
        lambda value: f"{value:.2f}",
        lambda value: f"{value:.1f}",
    )

    for color_index, profile in enumerate(profiles):
        profile_rows = [row for row in target_rows if row["profile"] == profile]
        profile_rows.sort(key=lambda row: int(row["file_size_bytes"]))
        color = PALETTE[color_index % len(PALETTE)]
        points = [
            (
                scale(float(row["file_size_bytes"]) / (1024 * 1024), min(size_values), max(size_values), left, left + panel_width),
                scale(float(row["throughput_mb_s"]), min(throughput_values), max(throughput_values), top + panel_height, top),
            )
            for row in profile_rows
        ]
        parts.append(polyline(points, color))
        parts.extend(draw_series_markers(points, color))
        legend_y = top + 18 + color_index * 18
        parts.append(f'<line x1="{left + panel_width - 120}" y1="{legend_y}" x2="{left + panel_width - 98}" y2="{legend_y}" stroke="{color}" stroke-width="3"/>')
        parts.append(f'<text x="{left + panel_width - 90}" y="{legend_y + 4}" class="legend">{profile}</text>')

    write_svg(output_dir / "throughput_by_profile.svg", parts)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate SVG plots from parJSON benchmark CSV files.")
    parser.add_argument("--thread-results", type=Path, default=Path("results/benchmark.csv"))
    parser.add_argument("--size-results", type=Path, default=Path("results/size_thread_matrix.csv"))
    parser.add_argument("--output-dir", type=Path, default=Path("results/plots"))
    args = parser.parse_args()

    thread_results = args.thread_results.resolve()
    size_results = args.size_results.resolve()
    output_dir = args.output_dir.resolve()

    if not thread_results.is_file():
        raise FileNotFoundError(f"Thread benchmark CSV not found: {thread_results}")
    if not size_results.is_file():
        raise FileNotFoundError(f"Size benchmark CSV not found: {size_results}")

    output_dir.mkdir(parents=True, exist_ok=True)
    plot_thread_scaling(read_csv(thread_results), output_dir)
    plot_size_scaling(read_csv(size_results), output_dir)
    plot_throughput(read_csv(size_results), output_dir)
    print(f"Wrote plots to {output_dir}")


if __name__ == "__main__":
    main()
