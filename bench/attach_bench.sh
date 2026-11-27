#!/usr/bin/env bash
# 필터 붙이는 시간 측정 1회를 "지표,값" 줄로 낸다 (scripts/run_bench.sh용).
#   bench/attach_bench.c: 필터마다 seccomp() 200회 중앙값 → attach_us@<필터>
# 프로그램은 처음 실행할 때 /tmp/seccomp-perf-bench/에 컴파일한다 (소스가 바뀌면 다시).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
BIN=${TMPDIR:-/tmp}/seccomp-perf-bench
mkdir -p "$BIN"
if [ ! -x "$BIN/attach_bench" ] || [ "$REPO/bench/attach_bench.c" -nt "$BIN/attach_bench" ]; then
	gcc -O2 -Wall -o "$BIN/attach_bench" "$REPO/bench/attach_bench.c"
fi

case "$(uname -m)" in
aarch64) FILTER=$REPO/bench/filters/docker_default_arm64.bpf ;;
x86_64)  FILTER=$REPO/bench/filters/docker_default_amd64.bpf ;;
esac

"$BIN/attach_bench" 200 "$FILTER" | awk -F, 'NR > 1 { print "attach_us@" $1 "," $3 }'
