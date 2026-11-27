# filter-modes-p7

- 날짜: 2026-11-27 03:58:23, 측정한 사람: Minchan Kwon
- 커널: 6.18.0-seccomp+ #8 SMP Fri Nov 27 03:55:30 UTC 2026
- 기계: aarch64, 4 CPU
- 저장소: 7af990b
- 측정 명령: `bench/filter_bench.sh`
- 반복: 10회 (예열 1회 제외), 매 회 조건 순서를 무작위로 섞음. 걸린 시간 81초
- 요약: summary.txt (n, 평균, 중앙값, 최소, 최대)

## 조건 (filter.conds)

```
# 필터 경로 (거부 캐시). 형식: 이름 sysctl목록(쉼표로 여러 개, 없으면 -) [측정 명령에 붙일 인자...]
F0 kernel.seccomp.cache_mode=0
F1 kernel.seccomp.cache_mode=1
F2 kernel.seccomp.cache_mode=2
```

## 회차별 순서

```
1: F2 F1 F0 
2: F0 F1 F2 
3: F0 F1 F2 
4: F1 F2 F0 
5: F0 F2 F1 
6: F0 F2 F1 
7: F0 F1 F2 
8: F0 F2 F1 
9: F1 F2 F0 
10: F1 F2 F0 
```

## 메모

패치 7(캐시 확인을 범위 확인·csdb 한 번으로) 커널. 같은 날 패치 6 커널 결과 results/2026-11-27_filter-modes와 비교: F2 거부 97.2 → 89.2ns(−8ns), 허용 그대로. arm64 csdb 1회 약 6.6ns(사용자 공간 측정 3회). selftest pass 99/fail 0/skip 12, cache_fuzz 5만 건 불일치 0
