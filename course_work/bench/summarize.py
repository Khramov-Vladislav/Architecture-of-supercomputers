"""Validate the recorded experiment and generate the tables used by the report."""

import csv
import math
import re
from collections import defaultdict
from pathlib import Path
from statistics import median


ROOT = Path(__file__).resolve().parents[1]
MODES = {"sequential": 1, "openmp": 1, "mpi": 2, "hybrid": 2}
PLACEMENT = re.compile(
    r"(\w+) n=(\d+) rank=(\d+)/(\d+) threads=(\d+)(?: host=(\S+))?"
)


def summarize():
    measurements = defaultdict(dict)
    configurations = defaultdict(set)
    with (ROOT / "results/samples.csv").open(newline="") as source:
        for row in csv.DictReader(source):
            mode = row["mode"]
            n, ranks, threads, repeat = (
                int(row[column]) for column in ("n", "ranks", "threads_per_rank", "repeat")
            )
            seconds = float(row["seconds"])
            key = (n, mode)
            if (
                row["verification"] != "exact"
                or ranks != MODES[mode]
                or (threads < 2 if mode in ("openmp", "hybrid") else threads != 1)
                or not math.isfinite(seconds)
                or seconds <= 0
                or repeat in measurements[key]
            ):
                raise ValueError(f"Invalid measurement: {row}")
            measurements[key][repeat] = seconds * 1000
            configurations[key].add(threads)

    sizes = sorted({n for n, _ in measurements})
    if not sizes:
        raise ValueError("No measurements found")
    repeat_ids = list(range(1, len(measurements[sizes[0], "sequential"]) + 1))
    placements = defaultdict(dict)
    for line in (ROOT / "results/placement.txt").read_text().splitlines():
        match = PLACEMENT.fullmatch(line)
        if match:
            mode, n, rank, ranks, threads, host = match.groups()
            key = (int(n), mode)
            if int(ranks) != MODES[mode] or int(rank) in placements[key]:
                raise ValueError(f"Invalid rank placement: {line}")
            placements[key][int(rank)] = (int(threads), host)

    rows = []
    for n in sizes:
        baseline = median(measurements[n, "sequential"].values())
        for mode, ranks in MODES.items():
            key = (n, mode)
            samples = measurements[key]
            if sorted(samples) != repeat_ids or len(configurations[key]) != 1:
                raise ValueError(f"Incomplete or inconsistent measurements: {key}")
            (threads,) = configurations[key]
            placement = placements[key]
            if (
                set(placement) != set(range(ranks))
                or any(t != threads for t, _ in placement.values())
                or (ranks > 1 and len({host for _, host in placement.values() if host}) != ranks)
            ):
                raise ValueError(f"Invalid thread or host placement: {key}, {placement}")
            values = samples.values()
            center = median(values)
            speedup = baseline / center
            rows.append(
                (n, mode, ranks, threads, len(values), center, min(values), max(values),
                 speedup, speedup / (ranks * threads))
            )
    return rows


def write_outputs(rows):
    with (ROOT / "results/summary.csv").open("w", newline="") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(
            ("n", "mode", "ranks", "threads_per_rank", "samples", "median_ms", "min_ms",
             "max_ms", "speedup", "efficiency")
        )
        for row in rows:
            writer.writerow((*row[:5], *(f"{value:.6f}" for value in row[5:])))

    directory = ROOT / "report/build"
    directory.mkdir(exist_ok=True)
    indexed = {(row[0], row[1]): row for row in rows}
    sizes = sorted({row[0] for row in rows})
    with (directory / "measurements.tex").open("w") as output:
        output.write(r"\newcommand{\TimeRows}{%" + "\n")
        for n in sizes:
            values = [f"{indexed[n, mode][5]:.3f}" for mode in MODES]
            output.write(" & ".join((str(n), *values)).replace(".", "{,}") + r" \\" + "\n")
        output.write("}\n")
    with (directory / "speedup.csv").open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(("n", *MODES))
        writer.writerows((n, *(indexed[n, mode][8] for mode in MODES)) for n in sizes)


if __name__ == "__main__":
    write_outputs(summarize())
