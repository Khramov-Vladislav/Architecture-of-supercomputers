#!/bin/sh
# Сборка программы и запуск всех экспериментов; результаты в results/
set -e
cd "$(dirname "$0")"
mkdir -p build results
clang -O2 -Wall -o build/cache src/cache.c
sysctl machdep.cpu.brand_string hw.perflevel0 hw.cachelinesize hw.pagesize > results/sysctl.txt
./build/cache size 64          > results/size.csv
./build/cache size 128         > results/size_l2.csv
./build/cache line 256         > results/line_ram.csv
./build/cache line 8           > results/line_l2.csv
./build/cache assoc 128 32   > results/assoc_l1.csv
./build/cache assoc 16384 32 > results/assoc_l2.csv
mkdir -p build/matplotlib
MPLCONFIGDIR=build/matplotlib python3 plot.py
