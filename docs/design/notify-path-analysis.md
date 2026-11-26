# 6.18 사용자 알림 경로와 스케줄러 깨우기 경로 분석

- 작성: 임대철 (B, 알림 경로) · 2026-11-26
- 기준 소스: Linux v6.18 (`7d0a66e`). 행 번호는 모두 이 커밋에서 직접 확인했다.
- 관련 문서: [정책 명세서 v0](auto-sync-policy-v0.md), [시각 계측 설계 v0](notif-timing-v0.md)

## 1. 한 번의 왕복

| 순서 | 누가 | 함수 (`kernel/seccomp.c`) | 하는 일 |
|---|---|---|---|
| 1 | 대상 | `seccomp_do_user_notification()` 1163 | `notify_lock`을 잡고 `struct seccomp_knotif n`을 목록에 넣음, `requests++` (1186) |
| 2 | 대상 | 1187~1190 | **깨우기 ①**: 플래그가 있으면 `wake_up_poll_on_current_cpu(&match->wqh, EPOLLIN)`, 없으면 `wake_up_poll()` |
| 3 | 대상 | 1200 / 1202 | 락을 풀고 `wait_for_completion_interruptible(&n.ready)` (또는 killable)로 잠듦 |
| 4 | 감독 | `seccomp_notify_recv()` 1552 → `recv_wait_event()` 1524 | `requests`를 하나 가져감. 없으면 `filter->wqh`에서 잠듦 |
| 5 | 감독 | 1594~1595 | `INIT`인 첫 알림을 `SENT`로 바꾸고 `wake_up_poll(EPOLLOUT)`, 사용자 버퍼로 복사 |
| 6 | 감독 | `seccomp_notify_send()` 1625 | 알림을 찾아 `REPLIED`로 바꾸고 결과값 저장 |
| 7 | 감독 | 1663~1666 | **깨우기 ②**: 플래그가 있으면 `complete_on_current_cpu(&knotif->ready)`, 없으면 `complete()` |
| 8 | 대상 | 1203 이후 | 깨어나 `notify_lock`을 다시 잡고 `REPLIED`를 확인, 반환값 설정 |

플래그는 `seccomp_notify_set_flags()` (1696)에서 `filter->notif->flags`에 저장된다.

## 2. 깨우기 호출이 스케줄러에 닿기까지

### 깨우기 ① (대상 → 감독)

`wake_up_poll_on_current_cpu()` (`include/linux/wait.h` 240) → `__wake_up_on_current_cpu()` (`kernel/sched/wait.c` 150) → `__wake_up_common_lock(nr_exclusive=1, WF_CURRENT_CPU)` → 대기 항목마다 등록된 깨우기 함수 호출.

감독이 **어떻게 기다리느냐**에 따라 깨우기 함수가 달라지고, 플래그 전달도 달라진다.

| 감독의 대기 방식 | 깨우기 함수 | `WF_CURRENT_CPU` |
|---|---|---|
| `ioctl(NOTIF_RECV)`에서 블록 | `recv_wake_function()` 1515 → `autoremove_wake_function()` → `default_wake_function()` | 그대로 전달 |
| `poll()` / `select()` | `pollwake()` → `__pollwake()` (`fs/select.c` 182) → `default_wake_function()` | 그대로 전달 |
| `epoll_wait()` | `ep_poll_callback()` (`fs/eventpoll.c` 1247) → `sync`가 0이 아니면 `wake_up_sync(&ep->wq)` (1318) | **`WF_SYNC`로 바뀜** |

### 깨우기 ② (감독 → 대상)

`complete_on_current_cpu()` (`kernel/sched/completion.c` 33) → `complete_with_flags(WF_CURRENT_CPU)` → `swake_up_locked()` (`kernel/sched/swait.c` 22) → `try_to_wake_up(task, TASK_NORMAL, WF_CURRENT_CPU)`.

대기자는 대상 하나뿐이고 중간에 플래그가 바뀌는 곳이 없다.

## 3. 스케줄러에서 플래그의 의미

| 플래그 | 처리 위치 | 동작 |
|---|---|---|
| `WF_CURRENT_CPU` (0x40) | `select_task_rq_fair()` (`kernel/sched/fair.c` 8583~8585) | 현재 CPU가 깨울 태스크의 허용 CPU에 있으면 **바로 현재 CPU를 반환**. `wake_wide`, `wake_affine`, `select_idle_sibling`을 모두 건너뜀. 현재 CPU가 바쁜지는 보지 않음 |
| `WF_SYNC` (0x10) | `wake_affine_idle()` (`fair.c` 7347~7352) | 현재 CPU의 실행 가능 태스크가 1개면 현재 CPU를 후보로 냄. 그 뒤 `select_idle_sibling()`을 다시 거치므로 **강제가 아니라 힌트** |

- 경로: `try_to_wake_up()`이 `WF_TTWU`를 더하고 (`core.c` 4148) `select_task_rq()` (4281)를 부른다. CPU가 바뀌면 `set_task_cpu()` 후 `ttwu_queue()` (4293).
- `select_task_rq()` (`core.c` 3567)는 허용 CPU가 2개 이상일 때만 클래스의 선택 함수를 부른다.
- `default_wake_function()` (`core.c` 7267)은 `WF_SYNC`, `WF_CURRENT_CPU` 외의 플래그가 오면 경고한다.
- `kernel/sched/features.h`에 SIS 계열은 `SIS_UTIL`뿐이고 `SIS_CURRENT`는 없다 (계획서의 전제 재확인).

## 4. 정책 설계에 영향을 주는 발견

1. **epoll 감독은 방향 ①에서 플래그가 약해진다.** `ep_poll_callback()`이 `WF_CURRENT_CPU`를 `WF_SYNC`로 바꿔 깨운다. 플래그를 켠 B1도 epoll 기반 감독에서는 효과가 다를 수 있다. 5주차 "epoll 경우"와 "`WF_SYNC` 비교"의 출발점이다. 효과 차이는 아직 측정하지 않았다.
2. **`ADDFD_FLAG_SEND` 응답은 플래그가 있어도 일반 깨우기다.** `seccomp_notify_addfd()` 1788행은 항상 `complete()`를 쓴다. fd를 넣으면서 응답하는 감독은 방향 ②에서 동기 모드를 못 탄다. 정책에 이 경로를 넣을지 정해야 한다.
3. **`WF_CURRENT_CPU`는 현재 CPU가 바쁜지 확인하지 않는다.** 그래서 E3(`single_task_running()`, `core.c` 5360)을 seccomp 쪽에서 직접 확인해야 한다. 이 함수는 선점 가능 구간에서 부르면 값이 바로 낡을 수 있어 힌트로만 쓴다.
4. **두 방향은 깨우는 쪽의 다음 행동이 다르다.** 방향 ①의 대상은 깨운 직후 잠들어 CPU를 비운다. 방향 ②의 감독은 `ioctl`에서 돌아와 계속 실행하므로, 대상은 감독이 다시 잠들 때까지 같은 CPU에서 기다린다. E5가 방향 ②에만 필요한 이유다.
5. **`notify_lock` 경합이 지연 차이의 원인 후보다.** 대상은 깨어나자마자 `notify_lock`을 잡는데 (1203), 감독은 깨우기 ② 시점에 아직 그 락을 쥐고 있다 (1668에서 해제). 다른 CPU에서 깨어나면 락을 기다리고, 같은 CPU면 감독이 락을 푼 뒤에 실행된다. 측정으로 확인할 가설이다.
6. **감독 스레드가 여럿이면 모두 깨어난다.** `recv_wait_event()`의 대기는 비배타적이라, `nr_exclusive=1`이어도 `RECV`에서 자는 스레드가 전부 깨어나 현재 CPU로 몰린다. "여러 알림" 경우의 E2 판단에서 고려한다.
7. **CPU 고정된 태스크에는 플래그가 무시된다.** 현재 CPU가 허용 마스크에 없으면 일반 경로로 간다. 정책이 켜도 효과가 없으므로 이득 확인(PROBE)에서 걸러져야 한다.
8. **신호로 중단된 왕복은 깨어남 시각이 없다.** `interrupted` 레이블로 빠지므로 계측 표본에서 뺀다.

## 5. 팀 계획과 다른 임대철의 환경

| 항목 | 계획서 | 임대철 VM |
|---|---|---|
| 아키텍처 / 가상화 | ARM64, UTM | x86_64, VirtualBox |
| vCPU / 메모리 | 4개 / 4GB | 3개 / 3.8GB |
| 시계 소스 | (미기재) | `tsc` |
| 선점 설정 | (미기재) | `PREEMPT_VOLUNTARY` (+ `PREEMPT_DYNAMIC`), `HZ=250` |

코드 분석과 패치 작성에는 영향이 없다. vCPU는 3개로 유지하므로, 이 VM의 측정 수치는 팀 기준(4 vCPU) 결과와 직접 비교하지 않고 조건을 명시한다.
