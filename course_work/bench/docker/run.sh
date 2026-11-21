#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "$0")/../.."

image=course-work-matmul:local
prefix=course-work-matmul-$$
cpuset=${CPUSET_CPUS:-0-3}
mkdir -p results
staging=$(mktemp -d results/.docker.XXXXXX)
containers=()
network=
volume=

cleanup() {
    for container in "${containers[@]:-}"; do
        if [[ -n "$container" ]]; then docker rm -f "$container" >/dev/null || true; fi
    done
    if [[ -n "$volume" ]]; then docker volume rm "$volume" >/dev/null || true; fi
    if [[ -n "$network" ]]; then docker network rm "$network" >/dev/null || true; fi
    rm -r -- "$staging"
}
trap cleanup EXIT

docker build --tag "$image" bench/docker
docker network create --internal "$prefix" >/dev/null
network=$prefix
volume=$(docker volume create "$prefix")
for node in node-a node-b; do
    container=$prefix-$node
    docker run --detach --init --name "$container" --hostname "$node" \
        --network "$network" --network-alias "$node" --cpuset-cpus "$cpuset" \
        --mount "type=volume,source=$volume,target=/launcher" \
        "$image" sleep infinity >/dev/null
    containers+=("$container")
done
primary=${containers[0]}
secondary=${containers[1]}

docker exec "$primary" mkfifo /launcher/command
docker exec --detach "$secondary" sh -c \
    'while IFS= read -r command </launcher/command; do sh -c "$command"; done'

docker cp Makefile "$primary:/work/Makefile"
docker cp src "$primary:/work/src"
docker exec "$primary" mkdir bench
docker cp bench/run.sh "$primary:/work/bench/run.sh"
docker exec "$primary" make -j4
docker cp "$primary:/work/build" "$staging/build"
docker cp "$staging/build" "$secondary:/work/build"

{
    docker version --format 'Docker Engine {{.Server.Version}}'
    docker image inspect --format 'Image {{.Id}} ({{.Os}}/{{.Architecture}})' "$image"
    printf 'CPU set allowed on both nodes: %s\n' "$cpuset"
    docker exec "$primary" sh -c \
        '. /etc/os-release; printf "%s\n" "$PRETTY_NAME"; uname -m; g++ --version | head -1; mpirun --version | head -1'
} > "$staging/environment.txt"

docker exec -e SIZES="${SIZES:-}" -e REPEATS="${REPEATS:-}" \
    -e OMP_BENCH_THREADS="${OMP_BENCH_THREADS:-}" \
    -e HYBRID_BENCH_THREADS="${HYBRID_BENCH_THREADS:-}" \
    "$primary" bash bench/run.sh --host node-a:1,node-b:1 \
    --mca plm_rsh_agent /usr/local/bin/launch-rank --mca plm_rsh_assume_same_shell 1 \
    --mca oob_tcp_if_include eth0 --mca btl self,tcp --mca btl_tcp_if_include eth0

docker cp "$primary:/work/results/samples.csv" "$staging/samples.csv"
docker cp "$primary:/work/results/placement.txt" "$staging/placement.txt"
mv -- "$staging/samples.csv" "$staging/placement.txt" "$staging/environment.txt" results/
