# 거부 캐시 설계 초안 v0

- 담당: D 권민찬
- 기준: Linux `v6.18`, `kernel/seccomp.c`
- 상태: 11/26 회의 리뷰용 초안. M1(11/28)에 설계 리뷰를 끝내고 구현을 시작한다.

## 요약

지금 리눅스는 "필터 결과가 항상 ALLOW"인 시스템 콜만 캐시한다. 그래서 거부되는 시스템 콜은 매번 필터를 끝까지 실행한다. 거부 캐시는 결과가 **시스템 콜 번호만으로 정해지는** 경우 그 결과(동작 + DATA)를 저장해 두고, 다음부터는 필터를 실행하지 않고 바로 돌려준다.

Docker 기본 프로필처럼 "목록에 없으면 `ERRNO(EPERM)`"인 허용 목록 필터에서는 목록 밖 시스템 콜 전부가 이 경우에 해당한다.

## 0. 왜 필요한가: 6.18 측정 (F1)

`bench/deny_bench.c`로 필터 길이를 바꿔 가며 허용(`getppid`)과 거부(`getpgid` → `ERRNO`) 시스템 콜 1회 지연을 쟀다. 필터의 모든 판단은 시스템 콜 번호만 본다. 팀 VM(6.18.0-seccomp, UTM arm64, 4 vCPU, 4GB)에서 100만 회 평균, 1회 실행 결과다.

| 필터 명령 수 | 허용 (ns) | 거부 (ns) |
| --- | --- | --- |
| 필터 없음 | 95 | 91 |
| 7 | 125 | 110 |
| 507 | 114 | 165 |
| 1,007 | 112 | 229 |
| 2,007 | 123 | 523 |
| 4,007 | 130 | 645 |

허용은 필터 길이와 상관없이 110~130ns로 거의 일정하다(기존 허용 캐시). 거부는 길이에 따라 늘어 4,007개에서 645ns로 허용의 약 5배다. 거부 캐시의 목표는 거부 열을 허용 열 수준으로 낮추는 것이다. 정식 비교는 A의 측정 규칙(조건 교차 10회)으로 다시 잰다.

## 1. 지금 캐시가 하는 일 (6.18 코드 분석)

| 위치 (행) | 함수·구조체 | 하는 일 |
| --- | --- | --- |
| 168 | `struct action_cache` | 필터마다 붙는 비트맵 `allow_native`, `allow_compat`. 비트 1 = 이 시스템 콜은 항상 ALLOW |
| 770 | `seccomp_is_const_allow()` | 필터를 시스템 콜 번호와 arch만 넣고 흉내 실행. 인자를 읽으면 포기(false), `RET K`에 닿으면 `k == SECCOMP_RET_ALLOW`인지 돌려줌 |
| 847 | `seccomp_cache_prepare_bitmap()` | 앞 필터의 비트맵을 복사(없으면 전부 1)하고, 1인 번호마다 새 필터를 흉내 실행해 ALLOW가 아니면 0으로 |
| 889 | `seccomp_cache_prepare()` | native, compat 두 arch에 대해 위 함수를 부름 |
| 962 | `seccomp_attach_filter()` | 필터를 붙이기 직전에 `seccomp_cache_prepare()` 호출 |
| 367 | `seccomp_cache_check_allow()` | 시스템 콜마다 맨 위 필터의 비트맵만 확인 |
| 404 | `seccomp_run_filters()` | 416행에서 캐시가 맞으면 바로 ALLOW. 아니면 모든 필터를 실행 |
| 1259 | `__seccomp_filter()` | 결과 동작별 처리(ERRNO 반환값, TRAP 시그널, KILL, 사용자 알림 등) |
| 1008 | `seccomp_log()` | 동작별 감사 로그 여부 결정 |

정정: 계획 문서의 "177행 `seccomp_cache_check_allow`"는 `SECCOMP_ARCH_NATIVE`가 없는 아키텍처용 빈 함수다. 실제 함수는 **367행**이다.

### 코드를 읽고 답한 네 가지

**① 비트맵은 언제 만들어지고, 필터가 여러 개면 어떻게 합쳐지나**

필터를 붙일 때 한 번 만든다(962행). 새 필터는 앞 필터의 비트맵을 복사한 뒤 비트를 끄기만 하므로 결과는 AND다. 필터는 붙인 뒤 바뀌지 않아서(`struct seccomp_filter` 주석) 캐시를 무효화할 일이 없다.

거부는 AND로 합칠 수 없다. `seccomp_run_filters()`는 맨 위(가장 최근) 필터부터 실행하면서 `ACTION_ONLY(cur) < ACTION_ONLY(ret)`일 때만 결과를 바꾼다. 따라서 **가장 강한(값이 작은) 동작이 이기고, 같은 동작끼리는 더 최근 필터의 결과(DATA 포함)가 이긴다.** `ACTION_ONLY`는 `s32`로 비교하므로 `KILL_PROCESS`(0x80000000)가 가장 작다.

**② "인자를 안 본다"를 어떻게 판정하나**

`seccomp_is_const_allow()`는 `LD nr`, `LD arch`, `JEQ/JGE/JGT/JSET K`, `JA`, `AND K`, `RET K`만 흉내 낸다. `args[]`나 `instruction_pointer`를 읽거나 다른 명령이 나오면 포기한다. `RET K`에 닿으면 이미 결과 값 `k`를 알고 있는데 ALLOW인지만 확인하고 버린다. **이 `k`를 돌려주도록 일반화하면 거부 결과도 얻을 수 있다.**

**③ 캐시가 맞으면 어디서 무엇을 건너뛰나**

416행에서 모든 필터의 BPF 실행을 건너뛴다. 허용은 결과가 하나뿐이라 비트로 충분하지만, 거부는 `ERRNO`의 errno 값, `TRAP`의 DATA, 동작 종류가 다 다르다. **결과 값 32비트 전체를 저장해야 한다.**

**④ 거부 결과의 부수 효과**

`__seccomp_filter()`는 `ERRNO`, `TRAP`일 때 `seccomp_log(..., match ? match->log : false)`를 부른다. 즉 **결과를 낸 필터의 `log` 플래그(`SECCOMP_FILTER_FLAG_LOG`)** 가 감사 로그 여부를 정한다. 필터를 건너뛰어도 이 값을 알아야 한다. `KILL_*`, `LOG`는 항상 `requested = true`라서 필터 정보가 필요 없다. `USER_NOTIF`는 `match`의 알림 큐가 필요하고, `TRACE`는 ptrace 재검사를 거친다.

## 2. 설계

### 2.1 캐시할 결과

| 동작 | 캐시 | 이유 |
| --- | --- | --- |
| `ALLOW` | 기존 비트맵 | 그대로 둠 |
| `ERRNO`, `TRAP` | O | 결과 값 + `log` 비트만 있으면 똑같이 처리됨 |
| `KILL_THREAD`, `KILL_PROCESS`, `LOG` | O | 결과 값만 있으면 됨 |
| `USER_NOTIF`, `TRACE` | X | 결과를 낸 필터 자체가 필요하고, 어차피 감독 프로세스나 ptrace 왕복이 비용 대부분이라 이득이 작음 |

결과가 상수가 아니거나 캐시하지 않는 동작이면 "모름"으로 두고 지금처럼 모든 필터를 실행한다. 모름으로 두는 건 항상 안전하다.

### 2.2 자료구조

```c
struct action_cache {
	DECLARE_BITMAP(allow_native, SECCOMP_ARCH_NATIVE_NR);   /* 기존 */
	DECLARE_BITMAP(deny_native,  SECCOMP_ARCH_NATIVE_NR);   /* 1 = ret_native[nr]가 유효 */
	DECLARE_BITMAP(log_native,   SECCOMP_ARCH_NATIVE_NR);   /* 결과를 낸 필터의 log 플래그 */
	u32 ret_native[SECCOMP_ARCH_NATIVE_NR];                 /* 합쳐진 결과 (동작 + DATA) */
#ifdef SECCOMP_ARCH_COMPAT
	/* compat도 같은 네 개 */
#endif
};
```

arm64 기준 native 약 460개라 배열이 약 1.8KB, compat까지 필터 하나에 약 3.5KB가 는다. 필터는 프로세스당 몇 개라 감당할 만하지만, 줄이는 방법은 4장에 질문으로 남긴다.

### 2.3 만들기 (필터를 붙일 때)

`seccomp_is_const_allow()`를 `seccomp_emulate_const(fprog, sd, u32 *ret)`로 일반화한다. 상수면 true와 함께 `RET K`의 `k`를 돌려준다. uprobe 예외는 `ALLOW`를 돌려준다. 기존 함수는 이걸 감싸는 형태로 남긴다.

```
for nr in 0..NR-1:
    # 앞 필터까지 합친 결과
    if 앞 필터 없음:            prev = (상수, ALLOW, log=false)
    elif allow_prev[nr]:        prev = (상수, ALLOW, log=false)
    elif deny_prev[nr]:         prev = (상수, ret_prev[nr], log_prev[nr])
    else:                       모름 → continue

    if not emulate_const(새 필터, nr, &k): 모름 → continue

    # 새 필터가 먼저 실행되고, 앞 필터는 "더 작을 때만" 이긴다
    if ACTION_ONLY(prev.ret) < ACTION_ONLY(k): r, log = prev.ret, prev.log
    else:                                      r, log = k, 새 필터.log

    if r == SECCOMP_RET_ALLOW:          allow[nr] = 1
    elif 동작(r) in {ERRNO, TRAP, KILL_*, LOG}:
                                        deny[nr] = 1; ret[nr] = r; log[nr] = log
    # 그 밖(USER_NOTIF, TRACE, DATA가 붙은 ALLOW)은 모름
```

기존 코드와 같이 필터가 아직 보이지 않을 때 실행되므로 원자적 비트 연산이 필요 없다(`__set_bit`).

### 2.4 찾기 (시스템 콜마다)

```c
	if (seccomp_cache_check_allow(f, sd))
		return SECCOMP_RET_ALLOW;
	if (seccomp_cache_check_deny(f, sd, &ret, log))   /* 새로 추가 */
		return ret;
```

- 기존처럼 `array_index_nospec()`으로 배열 인덱스를 막는다(Spectre).
- `log` 전달: `seccomp_run_filters()`가 `match` 대신 `bool *log`도 채우게 하고, `__seccomp_filter()`의 `skip:`에서 `match ? match->log : log`를 쓴다. 필터 포인터를 번호마다 저장하는 방법(배열 3.7KB 추가)보다 작다.
- 이 경로에서 `match`는 NULL로 남지만, 캐시하는 동작 중 `match`를 쓰는 곳은 `skip:`의 log뿐이다(USER_NOTIF는 캐시하지 않음).

### 2.5 런타임 스위치와 비교 조건

`kernel.seccomp_cache_mode` (계획 문서의 조건과 맞춤)

| 값 | 조건 | 동작 |
| --- | --- | --- |
| 0 | F0 | 캐시 끔. 항상 모든 필터 실행 |
| 1 | F1 | 기존 허용 캐시만 (6.18 그대로) |
| 2 | F2 | 허용 + 거부 캐시 |
| 3 | F3 | F2 + 인자 인식 캐시 (도전) |

캐시는 필터를 붙일 때 항상 만들고, 스위치는 찾기에서만 본다. 그래야 실행 중에 바꿔도 안전하다.

### 2.6 확인 방법

- `CONFIG_SECCOMP_CACHE_DEBUG`의 `/proc/<pid>/seccomp_cache` 출력(2517행)에 거부 항목(`ERRNO 1`, `KILL_PROCESS` 등)을 추가해 캐시 내용을 직접 본다.
- 정확성 시험: 무작위 필터를 1~4개 쌓고 모든 시스템 콜 번호에 대해 mode 1과 mode 2의 결과(반환값, errno, 시그널, 감사 로그)가 같은지 대조한다. 인자를 보는 필터는 캐시되지 않아야 한다.
- 2.3의 합치기 규칙은 파이썬 모의 실험으로 먼저 확인했다. 무작위 필터 3,000세트(필터 1~4개, 일부는 인자 의존)에서 24만 경우 모두 "캐시 결과 = 모든 필터 실행 결과"였고, 그중 거부 캐시 적중이 약 9만 3천 건이었다.
- 기존 seccomp selftest가 그대로 통과해야 한다.

### 2.7 측정 (A와 협의)

- 마이크로벤치마크: 허용 목록 N개 + 기본 `ERRNO` 필터를 붙이고, 목록 밖 시스템 콜을 반복 호출해 F1과 F2의 지연을 비교한다. N(필터 길이)과 필터 개수를 바꿔 가며 잰다.
- 실제 사례: Docker 기본 프로필 형태의 필터에서 같은 비교.

## 3. 일정

| 기간 | 할 일 |
| --- | --- |
| ~11/26 | 이 초안 리뷰 |
| ~11/28 (M1) | 설계 확정, `seccomp_emulate_const()` 일반화와 자료구조 추가 (구현 시작), 캐시 모드 스위치 |
| ~12/3 (M2) | 찾기 경로와 log 전달 완성, 정확성 시험 통과 (F2) |

## 4. 회의에서 정할 것

1. `USER_NOTIF`, `TRACE`를 캐시에서 빼는 데 동의하는가.
2. 필터당 약 3.5KB 증가를 받아들일지, 거부 항목이 하나라도 있을 때만 배열을 따로 할당할지.
3. `log` 전달 방법(2.4)을 이대로 할지. `seccomp_run_filters()`의 인자가 바뀌므로 C(정책)의 패치와 겹치는지 확인이 필요하다.
4. 스위치 이름과 값(2.5)을 B·C의 `kernel.seccomp_auto_sync`와 같은 방식으로 맞출지.
