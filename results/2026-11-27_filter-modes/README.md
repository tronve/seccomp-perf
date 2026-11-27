# filter-modes

- 날짜: 2026-11-27 03:09:59, 측정한 사람: Minchan Kwon
- 커널: 6.18.0-seccomp+ #7 SMP Thu Nov 26 12:52:58 UTC 2026
- 기계: aarch64, 4 CPU
- 저장소: f81f59f (커밋 안 된 변경 있음)
- 측정 명령: `bench/filter_bench.sh`
- 반복: 10회 (예열 1회 제외), 매 회 조건 순서를 무작위로 섞음. 걸린 시간 89초
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
1: F1 F2 F0 
2: F1 F2 F0 
3: F2 F1 F0 
4: F2 F0 F1 
5: F1 F0 F2 
6: F1 F0 F2 
7: F1 F2 F0 
8: F2 F0 F1 
9: F2 F1 F0 
10: F1 F0 F2 
```

## 메모

F0/F1/F2 교차 10회. F0을 넣어 처음 확인한 것: 짧은 필터(7줄)에서는 필터 실행(F0)이 캐시 적중보다 빠름(허용 94.0 vs 100.7ns, 거부 81.6 vs 97.2ns). 캐시 확인 1회당 약 8~10ns로 추정, 원인 조사 중(arm64 csdb 가설)
