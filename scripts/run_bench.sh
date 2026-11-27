#!/usr/bin/env bash
# 공통 측정 실행기: 조건을 sysctl과 인자로 바꿔 가며, 매 회 순서를 섞어 반복 측정한다.
# 규칙: docs/measurement-rules.md
#
# 사용법: scripts/run_bench.sh <조건 파일> <결과 이름> [반복=10] -- <측정 명령...>
#   예: scripts/run_bench.sh bench/conds/notify.conds notify-rt 10 -- bench/notify_perf.sh
#   측정 명령은 한 번 실행에 한 번 재고, 표준 출력에 "지표,값" 줄만 낸다.
# 결과: results/<오늘>_<결과 이름>/ 아래 bench.csv, summary.txt, README.md
set -euo pipefail

[ $# -ge 4 ] || { sed -n '4,7p' "$0"; exit 1; }
CONDS=$1 NAME=$2
RUNS=10
if [ "$3" != "--" ]; then RUNS=$3; shift; fi
shift 3
CMD=("$@")
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=$REPO/results/$(date +%F)_$NAME
SUDO=; [ "$(id -u)" -eq 0 ] || SUDO=sudo

# 조건 파일 읽기: "이름 sysctl목록(쉼표, 없으면 -) [붙일 인자...]"
names=() sysctls=() args=()
while read -r n s a; do
	[[ -z "$n" || "$n" == \#* ]] && continue
	names+=("$n") sysctls+=("$s") args+=("$a")
done < "$CONDS"
[ ${#names[@]} -gt 0 ] || { echo "조건이 없음: $CONDS"; exit 1; }

# 측정 전 sysctl 값을 저장해 두고 끝나면 되돌린다
declare -A saved
for s in "${sysctls[@]}"; do
	[ "$s" = - ] && continue
	for kv in ${s//,/ }; do
		k=${kv%%=*}
		saved[$k]=$(sysctl -n "$k")
	done
done
restore() { for k in "${!saved[@]}"; do $SUDO sysctl -qw "$k=${saved[$k]}"; done; }
trap restore EXIT

run_one() {	# 조건 번호, 회차
	local i=$1 r=$2
	if [ "${sysctls[$i]}" != - ]; then
		for kv in ${sysctls[$i]//,/ }; do $SUDO sysctl -qw "$kv"; done
	fi
	# shellcheck disable=SC2086 # 조건의 인자는 공백으로 나눠 넘긴다
	"${CMD[@]}" ${args[$i]} | awk -F, -v c="${names[$i]}" -v r="$r" '
		NF != 2 { print "형식 오류 (지표,값 이어야 함): " $0 > "/dev/stderr"; exit 1 }
		{ print c "," r "," $1 "," $2 }'
}

mkdir -p "$OUT"
CSV=$OUT/bench.csv
echo "condition,run,metric,value" > "$CSV"
start=$(date +%s)

run_one 0 0 > /dev/null		# 예열 1회 (기록 안 함)
orders=()
for r in $(seq 1 "$RUNS"); do
	order=$(seq 0 $(( ${#names[@]} - 1 )) | shuf | tr '\n' ' ')
	for i in $order; do run_one "$i" "$r" >> "$CSV"; done
	o=; for i in $order; do o+="${names[$i]} "; done
	orders+=("$r: $o")
	echo "run $r/$RUNS 끝 (순서: $o)"
done

# 조건·지표별 n, 평균, 중앙값, 최소, 최대
{
	printf "%-10s %-24s %4s %12s %12s %12s %12s\n" condition metric n mean median min max
	tail -n +2 "$CSV" | sort -t, -k1,1 -k3,3V -k4,4g | awk -F, '
		function flush() {
			if (n == 0) return
			med = (n % 2) ? v[(n + 1) / 2] : (v[n / 2] + v[n / 2 + 1]) / 2
			printf "%-10s %-24s %4d %12.3f %12.3f %12.3f %12.3f\n", pc, pm, n, s / n, med, v[1], v[n]
		}
		$1 != pc || $3 != pm { flush(); pc = $1; pm = $3; n = 0; s = 0 }
		{ v[++n] = $4; s += $4 }
		END { flush() }'
} > "$OUT/summary.txt"

dirty=$(git -C "$REPO" diff --quiet HEAD 2>/dev/null || echo " (커밋 안 된 변경 있음)")
cat > "$OUT/README.md" <<EOT
# $NAME

- 날짜: $(date '+%F %T'), 측정한 사람: $(git -C "$REPO" config user.name || whoami)
- 커널: $(uname -r) $(uname -v)
- 기계: $(uname -m), $(nproc) CPU
- 저장소: $(git -C "$REPO" rev-parse --short HEAD 2>/dev/null)$dirty
- 측정 명령: \`${CMD[*]}\`
- 반복: ${RUNS}회 (예열 1회 제외), 매 회 조건 순서를 무작위로 섞음. 걸린 시간 $(( $(date +%s) - start ))초
- 요약: summary.txt (n, 평균, 중앙값, 최소, 최대)

## 조건 ($(basename "$CONDS"))

\`\`\`
$(grep -v '^[[:space:]]*$' "$CONDS")
\`\`\`

## 회차별 순서

\`\`\`
$(printf '%s\n' "${orders[@]}")
\`\`\`

## 메모

(측정 목적, 눈에 띈 점, 다른 작업을 끄지 못한 것 등을 적는다)
EOT

echo
cat "$OUT/summary.txt"
echo
echo "결과: $OUT"
