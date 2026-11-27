# 거부 캐시 설계 v1 (구현 기준)

- 담당: D 권민찬
- 기준: Linux `v6.18`, `kernel/seccomp.c`
- 구현: `patches/filter-cache/0001~0006` (develop, 이슈 #3)
- 상태: 구현과 정확성 시험 완료 (M2의 D 몫). 이 문서는 구현된 그대로를 적는다. 초안과 분석은 [v0](deny-cache-v0.md)

## 요약

리눅스 6.18은 "필터 결과가 항상 ALLOW"인 시스템 콜만 캐시한다. 거부 캐시는 결과가 **시스템 콜 번호만으로 정해지는** 거부(`KILL_*`, `TRAP`, `ERRNO`, `LOG`)도 결과 값과 log 플래그를 저장해 두고, 다음부터는 필터를 실행하지 않고 바로 돌려준다.

같은 커널에서 교차 10회 잰 결과, 거부 시스템 콜 지연은 필터 길이와 상관없이 약 98ns로 일정해졌다(4,007개 명령 필터에서 549 → 98ns, Docker 기본 필터에서 127 → 97ns). 허용 쪽은 그대로다. 무작위 필터 5만 건 정확성 시험에서 불일치 0, seccomp selftest는 기준과 같다.

## 1. v0에서 바뀐 것

| 항목 | v0 (초안) | v1 (구현) | 이유 |
| --- | --- | --- | --- |
| 자료구조 | 비트맵 3개 + `u32` 배열 | `struct seccomp_cache_entry {valid, log, ret}` 배열 | 찾기 한 번에 한 항목만 읽음. 코드가 단순함 |
| 메모리 | 필터당 약 3.5KB | 필터당 약 7.5KB (arm64, compat 포함) | 항목 8바이트(패딩 포함) × 시스템 콜 수 × 2 arch |
| 스위치 | `kernel.seccomp_cache_mode`, 0~3 | `kernel.seccomp.cache_mode`, 0~2 | 기존 `kernel.seccomp.*` 폴더를 따름. F3(인자 인식)은 별도 설계 |
| 캐시 만들기 | 허용 비트맵과 거부를 따로 흉내 실행 | 한 번의 흉내 실행으로 둘 다 (패치 6) | 필터 붙이는 시간이 Docker에서 921 → 566µs |
| 정하지 못한 것 (v0 4장) | 4건 | 모두 구현대로 확정 (3차 회의 미정 4번) | 아래 5장 |

## 2. 패치 구성

| # | 제목 | 하는 일 |
| --- | --- | --- |
| 1 | split constant-result emulation out of seccomp_is_const_allow | 흉내 실행이 ALLOW 여부 대신 상수 결과 값 자체를 돌려주게 함 (`seccomp_emulate_const()`). 동작 변화 없음 |
| 2 | cache constant non-allow results per syscall | 필터를 붙일 때 거부 결과와 log 플래그를 기록. 아직 쓰지 않음 |
| 3 | show cached non-allow results in seccomp_cache | `/proc/<pid>/seccomp_cache`에 `DENY <결과> [LOG]` 출력 (`CONFIG_SECCOMP_CACHE_DEBUG`) |
| 4 | return cached non-allow results without running filters | 시스템 콜마다 허용 캐시 다음에 거부 캐시를 보고, 맞으면 바로 반환. log 플래그 전달 |
| 5 | add kernel.seccomp.cache_mode sysctl | 0 = 캐시 없음, 1 = 허용만 (6.18 그대로), 2 = 허용 + 거부 (기본값) |
| 6 | build the allow and deny caches in a single pass | 허용 비트맵과 거부 캐시를 한 번에 채움. `seccomp_cache_prepare_bitmap()`, `seccomp_is_const_allow()` 제거 |

각 패치는 따로 빌드되고 selftest를 통과한다(`git am`으로 순서대로 적용, CI에서 checkpatch와 컴파일 확인).

## 3. 구현

### 3.1 자료구조

```c
struct seccomp_cache_entry {
	bool valid;	/* ret이 이 시스템 콜의 상수 결과인가 */
	bool log;	/* 결과를 낸 필터가 SECCOMP_FILTER_FLAG_LOG로 붙었는가 */
	u32 ret;	/* 합쳐진 결과 (동작 + DATA) */
};

struct action_cache {
	DECLARE_BITMAP(allow_native, SECCOMP_ARCH_NATIVE_NR);
	struct seccomp_cache_entry deny_native[SECCOMP_ARCH_NATIVE_NR];
#ifdef SECCOMP_ARCH_COMPAT
	DECLARE_BITMAP(allow_compat, SECCOMP_ARCH_COMPAT_NR);
	struct seccomp_cache_entry deny_compat[SECCOMP_ARCH_COMPAT_NR];
#endif
};
```

캐시는 필터와 함께 할당되고(0으로 초기화), 필터는 붙인 뒤 바뀌지 않으므로 무효화가 필요 없다.

### 3.2 만들기: `seccomp_cache_prepare_arch()` (필터를 붙일 때)

시스템 콜 번호마다 한 번 흉내 실행해서 허용 비트맵과 거부 캐시를 함께 채운다.

```
for nr in 0..NR-1:
    # 앞 필터까지 합친 결과
    if 앞 필터 없음 or allow_prev[nr]:  prev = (ALLOW, log=false)
    elif deny_prev[nr].valid:          prev = (deny_prev[nr].ret, deny_prev[nr].log)
    else:                              continue          # 앞에서 이미 모름

    if not seccomp_emulate_const(새 필터, nr, &ret):  continue   # 인자를 봄 → 모름

    # seccomp_run_filters()와 같은 규칙: 앞 필터는 동작이 "더 작을 때만" 이긴다
    if ACTION_ONLY(prev.ret) < ACTION_ONLY(ret):  ret, log = prev.ret, prev.log
    else:                                         log = 새 필터.log

    if ret == ALLOW:                   __set_bit(nr, allow)
    elif seccomp_cache_deny_ok(ret):   deny[nr] = {true, log, ret}
    # USER_NOTIF, TRACE는 모름으로 둠
```

- `seccomp_cache_deny_ok()`: `KILL_PROCESS`, `KILL_THREAD`, `TRAP`, `ERRNO`, `LOG`만 캐시한다. `USER_NOTIF`, `TRACE`는 결과를 낸 필터 자체(알림 큐, ptrace)가 필요해서 뺀다.
- 같은 동작끼리는 더 최근 필터가 이기므로 DATA(errno 값 등)와 log 플래그도 최근 필터 것이 된다.
- 필터가 아직 다른 스레드에 보이지 않을 때 실행되므로 원자적 비트 연산이 필요 없다.

### 3.3 찾기: `seccomp_run_filters()` (시스템 콜마다)

```c
	mode = READ_ONCE(seccomp_cache_mode);
	if (mode >= 1 && seccomp_cache_check_allow(f, sd))
		return SECCOMP_RET_ALLOW;
	if (mode >= 2 && seccomp_cache_check_deny(f, sd, &ret, log))
		return ret;
	/* 아니면 지금처럼 모든 필터 실행 */
```

- 맨 위(가장 최근) 필터의 캐시만 본다. 앞 필터들의 결과는 만들 때 이미 합쳐져 있다.
- 번호 범위를 확인한 뒤 `array_index_nospec()`으로 인덱스를 막는다(Spectre v1). 허용 캐시와 같은 방식이다.
- 캐시가 맞으면 `match`가 NULL로 남는다. 그래서 `seccomp_run_filters()`에 `bool *log`를 추가해 캐시의 log 플래그를 돌려주고, `__seccomp_filter()`는 `match ? match->log : cache_log`로 감사 로그 여부를 정한다. 캐시하는 동작 중 `match`를 쓰는 곳은 이 log뿐이다.
- 스위치는 찾기에서만 본다. 캐시는 항상 만들어 두므로 실행 중에 바꿔도 안전하다.

### 3.4 확인용 출력

`CONFIG_SECCOMP_CACHE_DEBUG`가 켜져 있으면 `/proc/<pid>/seccomp_cache`가 시스템 콜마다 한 줄을 낸다.

```
aarch64 173 ALLOW
aarch64 89 DENY 0x00050001
aarch64 220 FILTER
```

Docker 기본 필터의 예: `getppid`(173)는 허용, `acct`(89)는 `ERRNO(EPERM)`, `clone`(220)은 플래그 인자를 봐서 필터 실행. 결과를 낸 필터가 `SECCOMP_FILTER_FLAG_LOG`로 붙었으면 `DENY` 줄 끝에 ` LOG`가 붙는다. `FILTER`는 캐시가 없어서(인자를 보거나 `USER_NOTIF`·`TRACE`) 필터를 실행하는 번호다.

## 4. 검증과 결과

### 4.1 정확성

| 시험 | 방법 | 결과 | 자료 |
| --- | --- | --- | --- |
| seccomp selftest | 패치마다 `tools/testing/selftests/seccomp` | pass 99, fail 0, skip 12 (패치 전과 같음) | `results/2026-11-22_selftest-base/` |
| 무작위 필터 | 무작위 필터 1~4개를 쌓고 시스템 콜 10개를 mode 1과 2에서 불러 결과(성공, errno, TRAP 데이터, KILL) 비교 | 5만 건 불일치 0 (패치 6 전후 모두) | `results/2026-11-26_cache-fuzz/`, `bench/cache_fuzz.c` |
| 시험의 검출력 | 합치기 규칙에 일부러 버그(같은 등급이면 먼저 필터가 이김)를 넣은 커널에서 같은 시험 | 5만 건 중 불일치 2,311건으로 실패 → 버그를 잡음. 원래 커널로 되돌리면 다시 0 | `results/2026-11-27_fuzz-mutant/` |
| 감사 로그 | `SECCOMP_FILTER_FLAG_LOG` 필터의 거부를 mode 1과 2에서 불러 감사 로그(type 1326) 비교 | 같음 | `bench/log_check.c` |
| 캐시 내용 | 필터 1개, 2개(합치기)에서 `/proc/<pid>/seccomp_cache` 확인 | 예상과 같음 | `bench/hold_filter.c`, `bench/hold_two.c` |

### 4.2 지연 (같은 커널, cache_mode 교차 10회, 평균 ns)

`results/2026-11-26_bench-modes/`. 패치 1~5 커널에서 잼(패치 6은 찾기 경로를 바꾸지 않음).

| 필터 | F1 허용 | F2 허용 | F1 거부 | F2 거부 |
| --- | --- | --- | --- | --- |
| 필터 없음 | 83.6 | 82.0 | 82.4 | 82.0 |
| 인공 7개 | 101.8 | 101.2 | 92.0 | 97.2 |
| 인공 507개 | 100.8 | 102.5 | 150.6 | 98.0 |
| 인공 4,007개 | 105.2 | 101.3 | 549.0 | 97.7 |
| Docker 기본 (713개) | 101.9 | 101.1 | 127.1 | 97.4 |

- 거부: F2는 필터 길이와 상관없이 약 98ns. 4,007개에서 5.6배, Docker에서 23% 빠르다.
- 허용: F1과 F2가 오차 안에서 같다.

### 4.3 필터를 붙이는 시간 (중앙값 µs, 200회)

`results/2026-11-26_attach-time/`

| 필터 | 패치 1~5 | 패치 1~6 |
| --- | --- | --- |
| Docker 기본 | 921 | 566 |
| 인공 4,007개 | 5,181 | 3,537 |

## 5. 확정한 것 (v0 4장의 질문)

1. `USER_NOTIF`, `TRACE`는 캐시하지 않는다.
2. 필터당 약 7.5KB를 받아들인다. 필터는 프로세스당 몇 개뿐이고, 거부 항목이 있을 때만 따로 할당하면 찾기에 분기가 하나 더 생긴다.
3. log 전달은 `bool *log` 인자로 한다. 알림 경로(B·C)는 `seccomp_run_filters()`의 인자를 바꾸지 않아 크게 겹치지 않는다. 통합 순서는 auto-sync → filter-cache.
4. 스위치는 `kernel.seccomp.cache_mode`. C의 스위치도 같은 폴더(`kernel.seccomp.auto_sync` 제안)에 둔다.

## 6. 한계

- **효과는 거부가 얼마나 자주 일어나느냐에 달렸다.** 보통의 프로그램은 거부되는 시스템 콜을 드물게 부른다. 예를 들어 glibc는 `clone3`가 `ENOSYS`로 거부되면 그 사실을 기억해서 두 번째부터는 부르지 않는다. 이득이 큰 경우는 긴 필터에서 거부가 반복될 때다(샌드박스 안에서 기능을 탐색하는 프로그램, 거부를 정상 흐름으로 쓰는 gVisor 같은 런타임).
- **아주 짧은 필터에서는 약간 느려진다.** 명령 7개 필터의 거부는 F1 92.0ns, F2 97.2ns다. 필터 실행이 캐시 확인보다 싸기 때문이다. 실제 프로필(Docker 713개)은 이 영역 밖이다.
- **인자를 보는 시스템 콜은 캐시되지 않는다.** Docker 기본 필터(arm64)에서 470개 중 4개다. 이를 다루는 인자 인식 캐시는 별도 설계(3차 회의 미정 5번)로 남긴다.
- **필터를 붙이는 시간은 실제 프로필에서 원본 6.18과 같다.** 패치 없는 6.18과 패치 1~7 커널에서 각각 10회 잰 중앙값이 Docker 기본 필터 508.6 vs 508.4µs다. 인공 필터는 1,007개까지 2% 안쪽이고, 4,007개에서만 2,839 → 3,145µs(+11%)로 늘어난다(`results/2026-11-27_attach-base/`, `_attach-p7/`). 흉내 실행을 한 번만 하는 것은 원본과 같으므로, 긴 필터에서 남은 차이는 흉내 실행 한 단계의 비용 차이로 보이고 원인은 확인하지 않았다.
