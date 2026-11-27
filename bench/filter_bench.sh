#!/usr/bin/env bash
# 필터 경로 측정 1회를 "지표,값" 줄로 낸다 (scripts/run_bench.sh용).
#   bench/deny_bench.c: 인공 필터(명령 N개)의 허용/거부 지연 → allow_ns@N, deny_ns@N
#   bench/bpf_bench.c + Docker 기본 필터: docker_allow_ns, docker_deny_ns
# 두 프로그램은 처음 실행할 때 /tmp/seccomp-perf-bench/에 컴파일한다 (소스가 바뀌면 다시).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
BIN=${TMPDIR:-/tmp}/seccomp-perf-bench
mkdir -p "$BIN"
for b in deny_bench bpf_bench; do
	if [ ! -x "$BIN/$b" ] || [ "$REPO/bench/$b.c" -nt "$BIN/$b" ]; then
		gcc -O2 -Wall -o "$BIN/$b" "$REPO/bench/$b.c"
	fi
done

case "$(uname -m)" in
aarch64) FILTER=$REPO/bench/filters/docker_default_arm64.bpf ;;
x86_64)  FILTER=$REPO/bench/filters/docker_default_amd64.bpf ;;
*)       FILTER= ;;
esac

"$BIN/deny_bench" | awk -F, 'NR > 1 { print "allow_ns@" $2 "," $3; print "deny_ns@" $2 "," $4 }'
if [ -n "$FILTER" ]; then
	"$BIN/bpf_bench" "$FILTER" | awk -F, '{ print "docker_allow_ns," $1; print "docker_deny_ns," $2 }'
fi
