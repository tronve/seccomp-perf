# notify-rt-prelim

- 날짜: 2026-11-27 05:35:59, 측정한 사람: Minchan Kwon
- 커널: 6.18.0-seccomp+ #8 SMP Fri Nov 27 03:55:30 UTC 2026
- 기계: aarch64, 4 CPU
- 저장소: 0d05dfd
- 측정 명령: `bench/notify_perf.sh`
- 반복: 3회 (예열 1회 제외), 매 회 조건 순서를 무작위로 섞음. 걸린 시간 102초
- 요약: summary.txt (n, 평균, 중앙값, 최소, 최대)

## 조건 (notify.conds)

```
# 알림 경로. 형식: 이름 sysctl목록(쉼표로 여러 개, 없으면 -) [측정 명령에 붙일 인자...]
# auto_sync 스위치가 들어오면 B0, B1의 - 를 kernel.seccomp.auto_sync=0 으로 바꾸고 C 줄의 # 을 지운다.
B0 -
B1 - --sync-mode
# C  kernel.seccomp.auto_sync=3
```

## 회차별 순서

```
1: B0 B1 
2: B1 B0 
3: B0 B1 
```

## 메모

예비 측정. perf는 커널 트리에서 빌드(make NO_LIBTRACEEVENT=1 NO_JEVENTS=1 NO_LIBPYTHON=1), PERF=~/proj/linux-6.18/tools/perf/perf로 실행. B0 24.8µs, B1 0.96µs(26배). 추가 확인: taskset -c 0으로 CPU 하나에 묶은 B0은 1.28µs(1회) → 이 VM에서 B0 시간의 약 95%는 다른 CPU를 깨우는 비용. 다른 VM(승완, B0 40.4/B1 16.4µs)과 차이가 커서 VM끼리 비교하면 안 됨
