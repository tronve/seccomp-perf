# attach-base

- 날짜: 2026-11-27 05:54:21, 측정한 사람: Minchan Kwon
- 커널: 6.18.0-seccomp+ #9 SMP Fri Nov 27 05:53:04 UTC 2026
- 기계: aarch64, 4 CPU
- 저장소: 67d9c88
- 측정 명령: `bench/attach_bench.sh`
- 반복: 10회 (예열 1회 제외), 매 회 조건 순서를 무작위로 섞음. 걸린 시간 17초
- 요약: summary.txt (n, 평균, 중앙값, 최소, 최대)

## 조건 (base.conds)

```
BASE -
```

## 회차별 순서

```
1: BASE 
2: BASE 
3: BASE 
4: BASE 
5: BASE 
6: BASE 
7: BASE 
8: BASE 
9: BASE 
10: BASE 
```

## 메모

같은 날 원본 6.18(attach-base)과 패치 1~7(attach-p7) 커널에서 각각 잼. Docker 508.6 vs 508.4µs로 같음, 인공 4,007개만 2,839 → 3,145µs(+11%)

