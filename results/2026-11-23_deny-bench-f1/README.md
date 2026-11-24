# 거부 vs 허용 시스템 콜 지연 (F1, 6.18 그대로)

- 커널: 6.18.0-seccomp+, 패치 없음
- VM: UTM arm64, 4 vCPU, 4GB
- 프로그램: bench/deny_bench.c (gcc -O2), 조건마다 100만 회 평균, 1회 실행 (탐색용)
- metric: allow_ns@N / deny_ns@N = 필터 명령 N개일 때 getppid / getpgid 1회 지연. N=0은 필터 없음
