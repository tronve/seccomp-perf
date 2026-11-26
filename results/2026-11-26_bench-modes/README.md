# 캐시 모드 교차 반복 측정

- 커널: 6.18.0-seccomp+ #6
- 기계: aarch64, 4 CPU
- 조건: kernel.seccomp.cache_mode = 1 (F1, 허용 캐시만), 2 (F2, 허용 + 거부 캐시)
- 반복: 10회, 매 회 F1과 F2의 순서를 무작위로 섞음
- 지표: allow_ns@N, deny_ns@N = 인공 필터(명령 N개)에서 getppid, getpgid 1회 지연 (bench/deny_bench.c)
        docker_allow_ns, docker_deny_ns = Docker 기본 필터에서 getppid, acct 1회 지연 (bench/bpf_bench.c, bench/filters/docker_default_arm64.bpf)
- 요약: summary.txt (평균, 최소, 최대). 걸린 시간 42초
