# 필터 붙이는 시간: 패치 6 전(P5) / 후(P6)

- 커널: 6.18 + patches/filter-cache 0001~0005 (P5), 0001~0006 (P6)
- 프로그램: bench/attach_bench.c, 필터마다 새 프로세스에서 seccomp() 1회, 200회 중앙값 (µs)
- 필터: pad<N> = 인공 필터 (명령 N+7개), docker = bench/filters/docker_default_arm64.bpf (713개)
- 결과: Docker 921 → 566µs (39%), 4,007개 5,181 → 3,537µs (32%)
