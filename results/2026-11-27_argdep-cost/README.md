# argdep-cost

- 날짜: 2026-11-27 02:47:03, 측정한 사람: Minchan Kwon
- 커널: 6.18.0-seccomp+ #7 SMP Thu Nov 26 12:52:58 UTC 2026
- 기계: aarch64, 4 CPU
- 저장소: fc13bf9
- 측정 명령: `/tmp/argdep_bench bench/filters/docker_default_arm64.bpf`
- 반복: 10회 (예열 1회 제외), 매 회 조건 순서를 무작위로 섞음. 걸린 시간 22초
- 요약: summary.txt (n, 평균, 중앙값, 최소, 최대)

## 조건 (f2.conds)

```
F2 kernel.seccomp.cache_mode=2
```

## 회차별 순서

```
1: F2 
2: F2 
3: F2 
4: F2 
5: F2 
6: F2 
7: F2 
8: F2 
9: F2 
10: F2 
```

## 메모

필터 없음(none_)과 Docker 기본 필터(docker_)를 같은 프로세스에서 차례로 잼. 차이 = 필터 비용, 인자 인식 캐시가 줄일 수 있는 상한. 해석은 docs/design/arg-cache-v0.md
