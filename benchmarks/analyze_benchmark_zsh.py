#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def percentile(values: list[int], fraction: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return float(ordered[lower])
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} RAW.csv SUMMARY.csv", file=sys.stderr)
        return 2
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    groups: dict[tuple[str, str, int, int], list[int]] = defaultdict(list)
    with source.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            key = (
                row["implementation"],
                row["metric"],
                int(row["line_count"]),
                int(row["workers"]),
            )
            groups[key].append(int(row["latency_ns"]))

    fields = [
        "implementation", "metric", "line_count", "workers", "samples",
        "mean_ns", "p50_ns", "p95_ns", "p99_ns", "line_ops_per_second",
        "gpioctl_vs_libgpiod_percent",
    ]
    rows = []
    means = {key: statistics.fmean(values) for key, values in groups.items()}
    for key in sorted(groups):
        implementation, metric, line_count, workers = key
        values = groups[key]
        mean_ns = means[key]
        baseline_key = ("libgpiod", metric, line_count, workers)
        ratio = ""
        if implementation == "gpioctl_zsh" and baseline_key in means:
            ratio = f"{means[baseline_key] / mean_ns * 100.0:.2f}"
        rows.append({
            "implementation": implementation,
            "metric": metric,
            "line_count": line_count,
            "workers": workers,
            "samples": len(values),
            "mean_ns": f"{mean_ns:.2f}",
            "p50_ns": f"{percentile(values, 0.50):.2f}",
            "p95_ns": f"{percentile(values, 0.95):.2f}",
            "p99_ns": f"{percentile(values, 0.99):.2f}",
            "line_ops_per_second": f"{line_count * workers * 1e9 / mean_ns:.2f}",
            "gpioctl_vs_libgpiod_percent": ratio,
        })
    with destination.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
