# 거부 vs 허용 시스템 콜 지연 (F2, 거부 캐시)

- 커널: 6.18.0-seccomp+ #5, patches/filter-cache 0001~0004 적용
- VM: UTM arm64, 4 vCPU, 4GB
- 프로그램: bench/deny_bench.c (gcc -O2), 조건마다 100만 회 평균, 1회 실행 (탐색용)
- metric: allow_ns@N / deny_ns@N = 필터 명령 N개일 때 getppid / getpgid 1회 지연. N=0은 필터 없음
- 비교: results/2026-11-23_deny-bench-f1/ (패치 없음). F1과 F2는 다른 커널로 재부팅해 측정함
