#!/usr/bin/env bash
# 알림 왕복의 시점별 지연을 커널 추적 지점으로 모아 "지표,값" 줄로 낸다 (scripts/run_bench.sh용).
# perf bench sched seccomp-notify 1회를 돌리는 동안 kernel.seccomp.notif_timing을 켜고
# seccomp:seccomp_notify_roundtrip, seccomp:seccomp_notify_recv 이벤트를 읽는다.
#
# 사용법: bench/notify_timing.sh [perf 인자...]   예: bench/notify_timing.sh --sync-mode
#   PERF=경로      perf 실행 파일 (기본: perf)
#   LOOPS=횟수     왕복 횟수 (기본: 100000). 추적 버퍼에 다 들어가는 크기여야 한다
#   BUF_KB=크기    CPU당 추적 버퍼 크기 (기본: 32768)
#
# 지표 (시간은 ns, p50은 중앙값):
#   post_ns   게시 → 감독이 받으러 깨어남      (방향 ① 지연, L1)
#   svc_ns    감독이 받음 → 응답               (감독 처리 시간, S)
#   reply_ns  응답 → 대상이 깨어남             (방향 ② 지연, L2)
#   rt_ns     게시 → 대상이 깨어남             (왕복 전체)
#   gap_ns    응답 → 감독이 다음 받기로 돌아옴 (복귀 시간, G)
#   same_cpu_post_pct / same_cpu_reply_pct  깨운 쪽과 깨어난 쪽의 CPU가 같았던 비율
#
# 추적 자체의 비용이 섞이므로 usecs_per_op는 계측을 끈 측정(bench/notify_perf.sh)보다 크다.
set -euo pipefail

T=/sys/kernel/tracing
INST=$T/instances/seccomp_timing
CTL=/proc/sys/kernel/seccomp/notif_timing
PERF=${PERF:-perf}
LOOPS=${LOOPS:-100000}
BUF_KB=${BUF_KB:-32768}

# 추적 버퍼와 sysctl을 만지므로 root가 필요하다. run_bench.sh는 일반 사용자로 부르므로 스스로 올린다.
[ "$(id -u)" -eq 0 ] || exec sudo PERF="$PERF" LOOPS="$LOOPS" BUF_KB="$BUF_KB" "$0" "$@"
[ -e "$CTL" ] || { echo "$CTL 없음: 계측 패치가 들어간 커널이 아니다" >&2; exit 1; }
[ -d "$T/events/seccomp" ] || { echo "seccomp 추적 지점 없음" >&2; exit 1; }

old=$(cat "$CTL")
tmp=$(mktemp -d)
cleanup() {
	echo "$old" > "$CTL"
	if [ -d "$INST" ]; then
		echo 0 > "$INST/events/seccomp/enable" 2>/dev/null || true
		rmdir "$INST" 2>/dev/null || true
	fi
	rm -rf "$tmp"
}
trap cleanup EXIT

[ -d "$INST" ] && rmdir "$INST"
mkdir "$INST"
echo "$BUF_KB" > "$INST/buffer_size_kb"
echo 1 > "$INST/events/seccomp/enable"
echo 1 > "$CTL"

"$PERF" bench sched seccomp-notify -l "$LOOPS" "$@" > "$tmp/perf.out"

echo 0 > "$INST/events/seccomp/enable"
echo "$old" > "$CTL"
cat "$INST/trace" > "$tmp/trace"
lost=$(cat "$INST"/per_cpu/cpu*/stats | awk '/^overrun:|^dropped events:/ { s += $NF } END { print s + 0 }')

awk '/usecs\/op/ { print "usecs_per_op," $1 }' "$tmp/perf.out"

# 이벤트 줄에서 이름=값 쌍을 뽑아 지표별 파일로 나눈다.
awk -v d="$tmp" '
	function val(key,    i, kv) {
		for (i = 1; i <= NF; i++) {
			split($i, kv, "=")
			if (kv[1] == key)
				return kv[2]
		}
		return ""
	}
	/seccomp_notify_roundtrip:/ {
		p = val("post_ns"); s = val("svc_ns"); r = val("reply_ns")
		print p > (d "/post_ns")
		print s > (d "/svc_ns")
		print r > (d "/reply_ns")
		print p + s + r > (d "/rt_ns")
		n++
		if (val("cpu_post") == val("cpu_recv")) sp++
		if (val("cpu_send") == val("cpu_wake")) sr++
	}
	/seccomp_notify_recv:/ {
		g = val("gap_ns")
		if (g > 0)
			print g > (d "/gap_ns")
	}
	END {
		print "samples," n + 0
		if (n) {
			printf "same_cpu_post_pct,%.1f\n", 100 * sp / n
			printf "same_cpu_reply_pct,%.1f\n", 100 * sr / n
		}
	}' "$tmp/trace"
echo "events_lost,$lost"

# 정렬한 값에서 중앙값(p50)과 p90을 낸다.
for m in post_ns svc_ns reply_ns rt_ns gap_ns; do
	[ -s "$tmp/$m" ] || continue
	sort -n "$tmp/$m" | awk -v m="$m" '
		{ v[NR] = $1 }
		END {
			print m "_p50," v[int((NR + 1) / 2)]
			i = int(NR * 0.9); if (i < 1) i = 1
			print m "_p90," v[i]
		}'
done
