#!/usr/bin/env bash
# perf bench sched seccomp-notify 1회를 "지표,값" 줄로 바꿔 낸다 (scripts/run_bench.sh용).
# 사용법: bench/notify_perf.sh [perf 인자...]   예: bench/notify_perf.sh --sync-mode
# perf 경로는 PERF로 바꿀 수 있다 (기본: perf)
set -euo pipefail
${PERF:-perf} bench sched seccomp-notify "$@" | awk '
	/Total time:/ { print "total_time_sec," $3 }
	/usecs\/op/   { print "usecs_per_op," $1 }
	/ops\/sec/    { print "ops_per_sec," $1 }'
