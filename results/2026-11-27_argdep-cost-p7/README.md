# argdep-cost-p7

- 날짜: 2026-11-27 05:07:45, 측정한 사람: Minchan Kwon
- 커널: 6.18.0-seccomp+ #8 SMP Fri Nov 27 03:55:30 UTC 2026
- 기계: aarch64, 4 CPU
- 저장소: 7af990b
- 측정 명령: `/tmp/argdep_bench bench/filters/docker_default_arm64.bpf`
- 반복: 10회 (예열 1회 제외), 매 회 조건 순서를 무작위로 섞음. 걸린 시간 7초
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

패치 7 커널. 패치 6 커널 결과 results/2026-11-27_argdep-cost와 비교: 캐시에 없는 personality가 140.7 → 134.7ns(csdb 한 번만큼). socket+close는 흔들림이 커서(최대 1,137ns) 비교에서 뺌
