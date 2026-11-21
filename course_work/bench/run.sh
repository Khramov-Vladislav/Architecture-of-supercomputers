#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "$0")/.."
read -r -a sizes <<< "${SIZES:-257 512 1024 2048}"
repeats=${REPEATS:-7}
omp_threads=${OMP_BENCH_THREADS:-4}
hybrid_threads=${HYBRID_BENCH_THREADS:-2}
export OMP_DYNAMIC=FALSE
export OMP_PROC_BIND=FALSE

mkdir -p results
temp_dir=$(mktemp -d results/.run.XXXXXX)
trap 'rm -r -- "$temp_dir"' EXIT
samples=$temp_dir/samples.csv
placements=$temp_dir/placement.txt
printf 'mode,n,ranks,threads_per_rank,repeat,seconds,verification\n' > "$samples"

run() {
    if ! "$@" >> "$samples" 2>> "$placements"; then
        cat "$placements" >&2
        return 1
    fi
}

for n in "${sizes[@]}"; do
    printf 'N = %s\n' "$n" >&2
    reference=$temp_dir/reference.bin
    run ./build/matmul-seq "$n" "$repeats" --write-reference "$reference"
    OMP_NUM_THREADS=$omp_threads run ./build/matmul-omp "$n" "$repeats" \
        --read-reference "$reference"
    run mpirun -n 2 --map-by ppr:1:node --bind-to none "$@" \
        ./build/matmul-mpi "$n" "$repeats" --read-reference "$reference"
    OMP_NUM_THREADS=$hybrid_threads run mpirun -n 2 --map-by ppr:1:node --bind-to none \
        -x OMP_NUM_THREADS -x OMP_DYNAMIC -x OMP_PROC_BIND "$@" \
        ./build/matmul-hybrid "$n" "$repeats" --read-reference "$reference"
done

mv -- "$samples" "$placements" results/
