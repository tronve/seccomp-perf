#!/usr/bin/env bash
# results/ 규칙 검사 (README 7장): 실험 폴더마다 README.md, CSV 첫 줄은 condition,run,metric,value
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
fail=0
for dir in "$REPO"/results/*/; do
	[ -d "$dir" ] || continue
	name=${dir#"$REPO"/}
	if [ ! -f "$dir/README.md" ]; then
		echo "실패: ${name}README.md 없음 (측정 조건을 적을 것)"; fail=1
	fi
	for csv in "$dir"*.csv; do
		[ -e "$csv" ] || continue
		head=$(head -1 "$csv" | tr -d '\r')
		if [ "$head" != "condition,run,metric,value" ]; then
			echo "실패: ${csv#"$REPO"/} 첫 줄이 '$head'"; fail=1
		fi
		if awk -F, 'NR>1 && NF!=4 {exit 1}' "$csv"; then :; else
			echo "실패: ${csv#"$REPO"/} 열이 4개가 아닌 줄이 있음"; fail=1
		fi
	done
done
[ $fail -eq 0 ] && echo "results/ 규칙 통과"
exit $fail
