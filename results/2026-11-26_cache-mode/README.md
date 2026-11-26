# 캐시 모드별 시스템 콜 지연 (같은 커널에서 비교)

- 커널: 6.18.0-seccomp+ #6, patches/filter-cache 0001~0005 적용
- VM: UTM arm64, 4 vCPU, 4GB
- 조건: kernel.seccomp.cache_mode = 0 (F0, 캐시 없음), 1 (F1, 허용만), 2 (F2, 허용+거부)
- 순서: 0 → 1 → 2 차례로 1회씩 (탐색용, 정식 측정은 교차 10회로 다시)
- 프로그램: bench/deny_bench.c (gcc -O2), 조건마다 100만 회 평균
- metric: allow_ns@N / deny_ns@N = 필터 명령 N개일 때 getppid / getpgid 1회 지연. N=0은 필터 없음
