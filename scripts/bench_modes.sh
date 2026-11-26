#!/usr/bin/env bash
# 캐시 모드(F1, F2)를 매번 섞은 순서로 번갈아 반복 측정한다 (정식 측정용).
#
# 사용법: scripts/bench_modes.sh [반복 횟수=10] [결과 폴더]
#   - bench/deny_bench.c: 길이를 바꾼 인공 필터에서 허용/거부 지연
#   - bench/bpf_bench.c + bench/filters/docker_default_<arch>.bpf: Docker 기본 필터에서 허용/거부 지연
#   필요: gcc, sudo (cache_mode를 바꾸기 위해)
# 결과: <결과 폴더>/bench.csv (condition,run,metric,value), summary.txt (평균, 최소, 최대), README.md
set -euo pipefail

RUNS=${1:-10}
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${2:-$REPO/results/$(date +%F)_bench-modes}
MODE=/proc/sys/kernel/seccomp/cache_mode
case "$(uname -m)" in
aarch64) FILTER=$REPO/bench/filters/docker_default_arm64.bpf ;;
x86_64)  FILTER=$REPO/bench/filters/docker_default_amd64.bpf ;;
*)       FILTER= ;;
esac

BIN=$(mktemp -d)
for b in deny_bench bpf_bench; do
	gcc -O2 -Wall -o "$BIN/$b" "$REPO/bench/$b.c"
done

if [ -w "$MODE" ] || sudo -n true 2>/dev/null || sudo -v; then :; fi
if [ ! -e "$MODE" ]; then
	echo "주의: $MODE 없음 → 모드를 바꾸지 못함 (스크립트 점검용으로만 의미 있음)"
fi
set_mode() { [ -e "$MODE" ] && echo "$1" | sudo tee "$MODE" > /dev/null || true; }
trap 'set_mode 2; rm -rf "$BIN"' EXIT

if [ -z "$FILTER" ] || [ ! -f "$FILTER" ]; then
	echo "주의: 이 아키텍처용 Docker 필터 없음 → Docker 측정은 건너뜀"
	FILTER=
fi

mkdir -p "$OUT"
CSV=$OUT/bench.csv
echo "condition,run,metric,value" > "$CSV"
start=$(date +%s)

for run in $(seq 1 "$RUNS"); do
	if (( RANDOM % 2 )); then order="1 2"; else order="2 1"; fi
	for m in $order; do
		set_mode "$m"
		"$BIN/deny_bench" | awk -F, -v c="F$m" -v r="$run" \
			'NR > 1 { print c "," r ",allow_ns@" $2 "," $3; print c "," r ",deny_ns@" $2 "," $4 }' >> "$CSV"
		if [ -n "$FILTER" ]; then
			"$BIN/bpf_bench" "$FILTER" | awk -F, -v c="F$m" -v r="$run" \
				'{ print c "," r ",docker_allow_ns," $1; print c "," r ",docker_deny_ns," $2 }' >> "$CSV"
		fi
	done
	echo "run $run/$RUNS 끝 (순서: F${order/ /, F})"
done

# 조건과 지표별 평균, 최소, 최대
{
	echo "condition,metric,mean,min,max,n"
	awk -F, 'NR > 1 {
		k = $1 "," $3; n[k]++; s[k] += $4
		if (!(k in lo) || $4 < lo[k]) lo[k] = $4
		if (!(k in hi) || $4 > hi[k]) hi[k] = $4
	} END { for (k in n) printf "%s,%.1f,%.1f,%.1f,%d\n", k, s[k] / n[k], lo[k], hi[k], n[k] }' "$CSV" |
		sort -t, -k2,2V -k1,1
} > "$OUT/summary.txt"

FNAME=${FILTER:+bench/filters/$(basename "$FILTER")}
FNAME=${FNAME:-측정 안 함}
cat > "$OUT/README.md" <<EOT
# 캐시 모드 교차 반복 측정

- 커널: $(uname -r) $(uname -v | cut -d' ' -f1)
- 기계: $(uname -m), $(nproc) CPU
- 조건: kernel.seccomp.cache_mode = 1 (F1, 허용 캐시만), 2 (F2, 허용 + 거부 캐시)
- 반복: ${RUNS}회, 매 회 F1과 F2의 순서를 무작위로 섞음
- 지표: allow_ns@N, deny_ns@N = 인공 필터(명령 N개)에서 getppid, getpgid 1회 지연 (bench/deny_bench.c)
        docker_allow_ns, docker_deny_ns = Docker 기본 필터에서 getppid, acct 1회 지연 (bench/bpf_bench.c, ${FNAME})
- 요약: summary.txt (평균, 최소, 최대). 걸린 시간 $(( $(date +%s) - start ))초
EOT

echo
awk -F, '$1 == "condition" || $2 ~ /docker|@0$|@507$|@4007$/ {
	printf "%-10s %-18s %8s %8s %8s\n", $1, $2, $3, $4, $5
}' "$OUT/summary.txt"
echo
echo "결과: $OUT"
