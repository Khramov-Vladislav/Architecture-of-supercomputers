# Построение графиков по results/*.csv в report/images/
import csv
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results")
IMG = os.path.join(HERE, "report", "images")
os.makedirs(IMG, exist_ok=True)

BLUE = "#2a5fa8"
ORANGE = "#c8622a"
MUTED = "#6b6b6b"

plt.rcParams.update({
    "font.size": 11,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.edgecolor": MUTED,
    "axes.grid": True,
    "grid.color": "#dddddd",
    "grid.linewidth": 0.6,
    "lines.linewidth": 2,
    "lines.markersize": 4,
})


def read(name):
    with open(os.path.join(RES, name)) as f:
        return list(csv.DictReader(f))


def human(b):
    for unit, k in (("M", 1 << 20), ("K", 1 << 10)):
        if b >= k:
            return f"{b // k}{unit}"
    return str(b)


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(IMG, name), dpi=200)
    plt.close(fig)


# 1. Размер кэша
size_curves = (
    ("size.csv", 64, BLUE),
    ("size_l2.csv", 128, ORANGE),
)
fig, ax = plt.subplots(figsize=(8, 4.2))
for name, step, color in size_curves:
    rows = read(name)
    x = [int(r["size_bytes"]) for r in rows]
    for key, label, style in (("min_ns_per_access", "минимум", "-"),
                              ("avg_ns_per_access", "среднее", "--")):
        y = [float(r[key]) for r in rows]
        ax.plot(x, y, color=color, linestyle=style, marker="o",
                label=f"шаг {step} Б, {label}")
ax.set_xscale("log", base=2)
ax.set_yscale("log")
ticks = [1 << k for k in range(12, 29, 2)]
ax.set_xticks(ticks, [human(t) for t in ticks])
ax.set_yticks([1, 2, 5, 10, 20, 50, 100], ["1", "2", "5", "10", "20", "50", "100"])
for b, label in ((128 << 10, "128 КБ"), (16 << 20, "16 МБ")):
    ax.axvline(b, color=ORANGE, linestyle="--", linewidth=1)
    ax.text(b * 1.08, 1.4, label, color="black")
ax.set_xlabel("Размер массива, байт")
ax.set_ylabel("Время доступа, нс")
ax.legend(fontsize=8, ncol=2)
save(fig, "size.png")

# 2. Размер строки
# Метод А используется для оценки скорости обработки, а метод Б — для размера строки.
for name, method, out, line in (
    ("line_ram.csv", "A", "line_a.png", 128),
    ("line_ram.csv", "B", "line_b.png", 128),
    ("line_l2.csv", "B", "line_l2_b.png", 64),
):
    pts = [r for r in read(name) if r["method"] == method]
    x = [int(r["stride_bytes"]) for r in pts]
    fig, ax = plt.subplots(figsize=(6.5, 3.6))
    for key, label, style in (("min_value", "минимум", "-"),
                              ("avg_value", "среднее", "--")):
        y = [float(r[key]) for r in pts]
        ax.plot(x, y, color=BLUE, linestyle=style, marker="o", label=label)
    ax.set_xscale("log", base=2)
    if method == "B":
        ticks = [4, 16, 32, 48, 64, 96, 128, 192]
        ax.set_xticks(ticks, [str(v) for v in ticks])
    else:
        ax.set_xticks(x, [str(v) for v in x])
    ax.axvline(line, color=ORANGE, linestyle="--", linewidth=1)
    ax.set_ylim(bottom=0)
    if method == "A":
        ax.set_xlabel("Шаг, байт")
        ax.set_ylabel("Время прохода массива, мс")
        ax.legend()
    else:
        ax.set_xlabel("Расстояние d, байт")
        ax.set_ylabel("Время на блок (2 чтения), нс")
        ax.legend()
    save(fig, out)

# 3. Ассоциативность
for name, out, k in (("assoc_l1.csv", "assoc_l1.png", 8), ("assoc_l2.csv", "assoc_l2.png", None)):
    rows = read(name)
    x = [int(r["fragments"]) for r in rows]
    fig, ax = plt.subplots(figsize=(6.5, 3.6))
    for key, label, style in (("min_ns_per_access", "минимум", "-"),
                              ("avg_ns_per_access", "среднее", "--")):
        y = [float(r[key]) for r in rows]
        ax.plot(x, y, color=BLUE, linestyle=style, marker="o", label=label)
    if k:
        ax.axvline(k + 0.5, color=ORANGE, linestyle="--", linewidth=1)
    ax.set_xticks(range(0, max(x) + 1, 4))
    ax.set_ylim(bottom=0)
    ax.set_xlabel("Число фрагментов N")
    ax.set_ylabel("Время доступа, нс")
    ax.legend()
    save(fig, out)
