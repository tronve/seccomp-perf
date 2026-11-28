# notify-timing-prelim

- 날짜: 2026-11-28 08:19:53, 측정한 사람: limdaecheol
- 커널: 6.18.0-seccomp-dev #2 SMP PREEMPT_DYNAMIC Sat Nov 28 07:29:00 UTC 2026
- 기계: x86_64, 3 CPU
- 저장소: 6daf16b
- 측정 명령: `bench/notify_timing.sh`
- 반복: 3회 (예열 1회 제외), 매 회 조건 순서를 무작위로 섞음. 걸린 시간 201초
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
2: B0 B1 
3: B0 B1 
```

## 메모

- 목적: 계측 패치(#14)와 `bench/notify_timing.sh`가 도는지 확인하는 예비 측정. 수치는 다른 결과와 비교하지 않는다.
- VM: VirtualBox x86_64, 3 vCPU, 3.8GB (팀 기준 4 vCPU와 다름). 커널은 v6.18 + auto-sync 0001만 넣은 것(filter-cache 패치 없음).
- 왕복 10만 번(`LOOPS=100000`)을 추적 지점으로 모두 모았다. 잃은 이벤트 0.
- 시점별 중앙값은 B0, B1이 거의 같다(게시→받음 약 3.3µs, 응답→깨어남 약 3.0µs, 처리 약 0.9µs, 복귀 약 7.5µs).
  이 회차들에서는 B0도 98%가 같은 CPU에서 깨어났기 때문이다(`same_cpu_*_pct`).
- B0의 `usecs_per_op`가 16~125µs로 크게 흔들린 것은 나머지 몇 %의 다른 CPU 깨우기가 매우 느려서다.
  같은 날 다른 회차에서 B0의 92%가 다른 CPU로 깨어났을 때는 게시→받음 153µs, 응답→깨어남 154µs(중앙값)였다.
- `rt_ns`(게시→깨어남)는 perf의 `usecs_per_op`보다 작다. perf 쪽은 시스템 콜 진입·복귀, 필터 실행, 추적 비용까지 포함한다.
- `usecs_per_op`는 추적을 켠 상태의 값이라 `bench/notify_perf.sh` 결과와 비교하지 않는다.
- 이 VM은 측정 전에 강제 재시작했고, 그 뒤 패치 없는 커널에서도 B0가 45~132µs로 재시작 전(약 12µs)보다 느려졌다. 패치와 무관한 환경 변화다.
