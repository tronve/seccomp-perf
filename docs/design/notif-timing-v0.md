# 알림 왕복 시각 계측 설계 v0 (초안)

- 작성: 임대철 (B, 알림 경로) · 2026-11-26
- 기준 소스: Linux v6.18 (`7d0a66e`), `kernel/seccomp.c`. 행 번호는 모두 이 커밋 기준이다.
- 상태: 11/26 회의 공유용 초안. 구현은 #14에서 한다 (11/27~28).
- 관련 문서: [정책 명세서 v0](auto-sync-policy-v0.md) 3장, [알림·깨우기 경로 분석](notify-path-analysis.md)

## 0. 요약

- 알림 한 번의 왕복에서 **다섯 시점**의 시각을 기록해 방향별 깨우기 지연(L1, L2), 감독 처리 시간(S), 복귀 시간(G)을 구한다.
- 이 단계에서는 **깨우는 방식을 바꾸지 않는다** (동작 변화 없음). 정책(C)이 이 값을 입력으로 쓴다.
- 시점의 이름과 위치는 정책 명세서 v0를 따른다. 이 문서가 더하는 것은 **CPU 번호 기록**, **추적 지점으로 내보내기**, **끄고 켜는 방법**, **검증 방법**이다.

## 1. 시각을 찍는 위치

| 시점 | 의미 | 위치 | 실행 문맥 | 저장 |
| --- | --- | --- | --- | --- |
| `t_post` 게시 | 대상이 감독을 깨우기 직전 | `seccomp_do_user_notification()` 1187행 바로 앞 | 대상, `notify_lock` 보유 | `knotif` |
| `t_enter` 받기 진입 | 감독이 다음 알림을 받으러 돌아옴 | `seccomp_notify_recv()`의 `recv_wait_event()` 호출 직전 (1568행) | 감독, 잠금 없음 | 지역 변수 |
| `t_recv` 받음 | 감독이 대기에서 돌아옴 | `recv_wait_event()` 반환 직후 (1568행 뒤) | 감독, 잠금 없음 | 지역 변수 → 알림을 고른 뒤 `knotif` |
| `t_send` 응답 | 감독이 대상을 깨우기 직전 | `seccomp_notify_send()` 1663행 바로 앞 | 감독, `notify_lock` 보유 | `knotif`, `notif->last_send` |
| `t_wake` 깨어남 | 대상이 대기에서 돌아옴 | `wait_for_completion_*()` 반환 직후 (1200·1202행 뒤, 잠금 재획득 전) | 대상, 잠금 없음 | 지역 변수 |

- `t_recv`와 `t_wake`는 잠금을 잡기 **전에** 찍는다. `notify_lock` 대기 시간이 깨우기 지연에 섞이지 않게 하기 위해서다.
- 대상의 대기 루프(1195~1220행)는 addfd 메시지 때문에 여러 번 돌 수 있다. 돌 때마다 지역 변수에 찍고, `REPLIED`로 끝난 마지막 값을 `t_wake`로 쓴다.
- 계획서의 네 시점에 `t_enter`를 더했다. 복귀 시간(E5)을 `t_recv`로 재면 대상이 실행한 시간까지 섞이기 때문이다 (명세서 N6과 같은 결론).

## 2. 파생 값

| 값 | 계산 | 쓰는 곳 |
| --- | --- | --- |
| L1 (방향 ① 지연) | `t_recv − t_post` | 방향 ① 표본 |
| S (감독 처리 시간) | `t_send − t_recv` | E4 이동 평균 |
| L2 (방향 ② 지연) | `t_wake − t_send` | 방향 ② 표본 |
| G (복귀 시간) | `t_enter − 직전 last_send` | E5 이동 평균 |
| RT (왕복 전체) | `t_wake − t_post` | 벤치마크 결과와 대조 |

표본에서 빼는 경우:

- 감독이 자고 있지 않았을 때의 L1. 깨우기 지연이 아니라 감독의 바쁜 시간이다 (명세서 N5).
- 신호로 중단된 왕복. `interrupted` 레이블로 빠져 `t_wake`가 응답과 짝이 맞지 않는다.
- `SECCOMP_ADDFD_FLAG_SEND` 응답 (1788행). `seccomp_notify_send()`를 거치지 않아 `t_send`가 0이다 (명세서 N4).

## 3. 저장 위치

```c
/* struct seccomp_knotif에 추가 */
	u64 t_post, t_recv, t_send;
	int cpu_post, cpu_recv, cpu_send;     /* 각 시점의 CPU 번호 */

/* struct notification에 추가 */
	u64 last_send;                        /* 직전 t_send, G 계산용 */
```

- `seccomp_knotif`는 대상의 스택에 있고 왕복 동안 유효하므로 추가 할당이 없다.
- CPU 번호는 같은 CPU 깨우기가 **실제로 일어났는지** 확인하는 데 쓴다 (`cpu_post == cpu_recv`, `cpu_send == 깨어난 CPU`). CPU 고정이나 epoll 경로처럼 플래그가 무시되거나 약해지는 경우를 표본에서 가려낼 수 있다.
- 평균과 흔들림(μ, σ), `elig[]`, `sync[]`, `inflight`, `recv_sleepers`는 C의 공통 틀 소관이다. 계측은 원시 시각만 만든다.

## 4. 시계

`ktime_get_ns()` (CLOCK_MONOTONIC). 두 시점이 서로 다른 CPU에서 찍히므로 CPU 간 비교가 되는 시계가 필요하다. 왕복당 5번 부르는 비용은 실험 ⑧(정책 비용)에서 잰다.

## 5. 끄고 켜기

static key 하나로 감싼다. 꺼져 있으면 지점마다 분기 하나만 남는다. C의 `auto_sync` 스위치가 0이 아니거나 계측 스위치가 1이면 켠다. 계측 스위치의 sysctl 이름은 C의 스위치 이름이 정해지면 맞춘다 (예: `kernel.seccomp.notif_timing`).

## 6. 값 내보내기 (제안)

6.18에는 seccomp 추적 지점이 없다 (`include/trace/events/seccomp.h` 없음). 추적 지점 하나를 새로 만든다.

- 이름: `seccomp:seccomp_notify_roundtrip`
- 시점: 대상이 `REPLIED`를 확인한 직후
- 필드: 알림 id, L1, S, L2, G, CPU 네 개(게시, 받음, 응답, 깨어남)

수집은 `/sys/kernel/tracing` 또는 `perf record -e seccomp:seccomp_notify_roundtrip`으로 한다. 명세서 12장(관찰 계획)의 L1, L2, S, G 분포를 이 추적 지점으로 모을 수 있다.

| 대안 | 택하지 않은 이유 |
| --- | --- |
| `trace_printk()` | 부팅 로그에 경고 배너가 뜨고 형식이 고정되지 않는다 |
| debugfs 누적 통계 | 표본별 분포를 볼 수 없다 |

## 7. "동작 변화 없음" 확인 방법

1. 계측을 끈 상태와 켠 상태에서 seccomp selftest가 기준 결과와 같다.
2. `perf bench sched seccomp-notify`의 B0, B1 수치가 계측 전 커널과 오차 범위 안이다.
3. 추적 지점의 RT 평균이 벤치마크의 왕복 시간과 맞는다.

## 8. 열린 질문 (11/26 회의)

1. 내보내기를 추적 지점으로 해도 되는가 (A의 수집 방법과 맞는지).
2. `t_post`, `t_recv`, `t_send` 필드는 B의 계측 패치가 먼저 넣고, C의 공통 틀이 그 위에 `elig[]`, `sync[]`를 더하는 순서로 가는가. 반대 순서면 계측 패치가 공통 틀을 기다려야 한다.
3. epoll 감독은 받기로 돌아갈 때 `RECV`가 아니라 `epoll_wait()`로 간다. 이 경우 `t_enter`를 `seccomp_notify_poll()` 호출 시각으로 대신할 것인가 (명세서 열린 질문 1과 함께).
4. CPU 번호 기록을 유지할 것인가. `knotif`가 12바이트 커진다.
