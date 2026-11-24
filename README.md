# seccomp 샌드박스 성능 개선 (팀 프로젝트)

Linux 6.18에서 seccomp의 두 경로를 개선한다.

- **자동 직통 모드 정책 (메인)**: 사용자 알림 경로에서 커널이 깨우기 방향별로 같은 CPU 깨우기를 스스로 켜고 끈다.
- **캐시 확장 (보조)**: 필터 경로에서 "항상 거부" 결과와, 결과가 인자 하나에만 달린 시스템 콜을 캐시한다.

전체 계획은 [최종 계획 문서](https://claude.ai/artifact/1qxXPwR6pVoBUkzonTiUef)에 있다. 개발 환경(새 VM, Ubuntu 20.04, Linux 6.18 빌드)은 [0단계 공통 세팅 가이드](https://claude.ai/artifact/1tvmeuWsga8mNGe5HGq4ad)를 따른다.

## 팀

| 역할 | 담당 | 주 작업 영역 |
| --- | --- | --- |
| A. 측정·검증 | 김승완 | `bench/`, `scripts/`, `results/`, 통합 빌드 |
| B. 알림 경로 | 임대철 | `patches/auto-sync/` (방향 ②, 시각 측정) |
| C. 정책 (메인) | 이준원 | `patches/auto-sync/` (공통 틀, 방향 ①) |
| D. 캐시 + PM | 권민찬 | `patches/filter-cache/`, `docs/`, 저장소 관리 |

## 저장소 구조

```
.
├── README.md
├── patches/              # 커널 패치 (git format-patch 결과). 커널 소스 자체는 올리지 않는다
│   ├── auto-sync/        # 자동 직통 모드 정책 (B, C)
│   └── filter-cache/     # 캐시 확장 (D)
├── bench/                # 벤치마크 소스 (A)
├── scripts/              # 패치 적용, 빌드, 측정 자동화
├── results/              # 측정 결과 (CSV)
└── docs/                 # 정책 명세서, 설계 노트, 회의록
```

## 작업 흐름 한눈에

```
main     ●──────────────────────────●───────────  (마일스톤마다 develop을 합침, 태그)
          \                        /
develop    ●────●──────────●──────●──────────────  (통합 브랜치, PR로만 합침)
                 \        /
feat/12-...       ●──●──●                          (이슈 하나 = 브랜치 하나)
```

1. **이슈를 만든다.** 할 일은 반드시 이슈로 시작한다.
2. `develop`에서 **기능 브랜치**를 판다. 이름에 이슈 번호를 넣는다.
3. 작업하고 **커밋**한다. 커널 코드는 패치 파일로 내보내서 커밋한다.
4. `develop`으로 **PR**을 연다. CI가 통과하면 작성자가 직접 합친다. 리뷰는 합친 뒤에 해도 된다.
5. **Squash and merge**로 합치고 브랜치를 지운다. 이슈는 PR에서 자동으로 닫힌다.
6. 마일스톤마다 PM이 `develop`을 `main`에 합치고 태그를 단다.

## 1. 이슈

- 작업 하나에 이슈 하나. 너무 크면 체크리스트로 쪼개거나 이슈를 나눈다 (목표: 1주 안에 끝나는 크기).
- 담당자(Assignee), 라벨, 마일스톤(M0~M4)을 꼭 단다.
- 버그나 이상한 측정값도 이슈로 남긴다.

**라벨**

| 종류 | 라벨 |
| --- | --- |
| 영역 | `area:policy`, `area:cache`, `area:bench`, `area:infra`, `area:docs` |
| 유형 | `type:feat`, `type:fix`, `type:exp`(실험·측정), `type:docs` |
| 상태 | `blocked`, `help-wanted` |

**이슈 템플릿**

```markdown
## 목적
(무엇을, 왜)

## 할 일
- [ ] 
- [ ] 

## 완료 조건
(예: selftest 통과, 실험 ①에서 B0 대비 수치 확인)

## 참고
(관련 이슈, 계획 문서 섹션, 커널 소스 위치)
```

## 2. 브랜치

| 브랜치 | 용도 | 규칙 |
| --- | --- | --- |
| `main` | 마일스톤 시점의 안정본, 제출·발표용 | 직접 push 금지. PM이 `develop`에서만 합침 |
| `develop` | 통합 브랜치 | 직접 push 금지. PR로만 합침 |
| `feat/<이슈번호>-<설명>` | 기능 개발 | `develop`에서 파서 `develop`으로 PR |
| `fix/<이슈번호>-<설명>` | 버그 수정 | 같음 |
| `exp/<이슈번호>-<설명>` | 실험·측정 | 같음. 결과는 `results/`에 |
| `docs/<이슈번호>-<설명>` | 문서 | 같음 |

- 설명은 영어 소문자와 하이픈으로 짧게: `feat/12-dir-policy-state`, `exp/20-baseline-b0-b1`
- 브랜치는 오래 두지 않는다. 일주일이 넘으면 `develop`을 받아 와서(`git pull origin develop`) 충돌을 미리 푼다.

## 3. 커밋

```
<유형>(<영역>): <요약> (#이슈번호)
```

- 유형: `feat`, `fix`, `exp`, `docs`, `refactor`, `chore`
- 영역: `policy`, `cache`, `bench`, `scripts`, `results`, `docs`
- 요약은 한국어도 된다. 한 커밋에는 한 가지 변경만.

```
feat(policy): 방향별 dir_policy 상태와 OFF/ON 전환 추가 (#12)
exp(results): 6.18 기준 B0·B1 왕복 지연 10회 측정 (#20)
fix(cache): 인자 두 개를 보는 필터가 캐시되던 문제 수정 (#31)
```

커널 트리 안의 커밋은 커널 관례를 따른다. 이 메시지가 그대로 패치 파일 제목과 본문이 된다. **제목 한 줄, 빈 줄, 설명(한 줄 75자 이내로 줄바꿈)** 을 쓴다. 설명이 없거나 줄이 길면 CI의 checkpatch가 실패한다.

```
seccomp: add per-direction auto sync wakeup policy

Decide per wakeup direction whether to wake the peer on the current
CPU, based on the observed round-trip times.
```

```bash
git commit          # -m 대신 편집기를 열어 제목과 설명을 쓴다
```

## 4. 커널 패치 다루기

커널 소스(수 GB)는 이 저장소에 올리지 않는다. 각자 로컬의 `~/proj/linux-6.18`에서 작업하고, **패치 파일**로 내보내 이 저장소에 커밋한다. 모든 패치의 기준은 **`v6.18`** 이다.

**처음 한 번 (커널 트리)**

```bash
cd ~/proj/linux-6.18
git branch base            # 아무것도 고치기 전, v6.18 그대로인 지점을 base로 표시
```

**작업 시작: develop에 있는 현재 패치를 깔고 시작**

```bash
# 이 저장소
cd ~/repo && git switch develop && git pull
git switch -c feat/12-dir-policy-state

# 커널 트리
cd ~/proj/linux-6.18
git switch -c feat/12-dir-policy-state base
git am ~/repo/patches/auto-sync/*.patch      # 해당 영역에 이미 합쳐진 패치 적용 (없으면 생략)
# ... 코드 수정, 커밋 ...
```

**패치 내보내기: 영역의 패치 전체를 다시 만든다**

```bash
cd ~/proj/linux-6.18
rm -f ~/repo/patches/auto-sync/*.patch
git format-patch --zero-commit --no-signature base..HEAD -o ~/repo/patches/auto-sync/

cd ~/repo
git add patches/auto-sync
git commit -m "feat(policy): 방향별 dir_policy 상태와 OFF/ON 전환 추가 (#12)"
git push -u origin feat/12-dir-policy-state
```

- `--zero-commit`, `--no-signature`를 쓰면 다시 내보내도 바뀌지 않은 패치 파일은 그대로라서, PR에는 새로 추가된 패치만 보인다.
- `auto-sync`는 B와 C가 같이 쓴다. B는 C의 공통 틀이 `develop`에 합쳐진 뒤 그 위에서 시작한다.
- 두 영역 모두 `seccomp.c`를 고치므로, 통합할 때는 `auto-sync` → `filter-cache` 순서로 적용한다. 충돌하면 뒤에 합쳐지는 쪽이 자기 패치를 고친다.

**다른 사람 패치 적용해 보기**

```bash
cd ~/proj/linux-6.18
git switch -c review/12 base
git am ~/repo/patches/auto-sync/*.patch
make -j$(nproc) && sudo make modules_install && sudo make install
```

## 5. Pull Request와 리뷰

- 대상은 항상 `develop`. 제목은 `[#이슈번호] 요약`.
- 본문에 `Closes #이슈번호`를 써서 이슈가 자동으로 닫히게 한다.
- **CI 통과**하면 작성자가 합친다. 승인은 기다리지 않는다.
- 리뷰어는 지정한다. 리뷰 짝: B ↔ C, A ↔ D. 리뷰어는 합쳐진 뒤라도 24시간 안에 보고, 고칠 점은 PR 댓글이나 새 이슈로 남긴다.
- 다른 사람 영역(`patches/`의 남의 폴더 등)을 건드리는 PR은 합치기 전에 그 사람에게 메신저로 알린다.
- 합치는 방식은 **Squash and merge**. 합친 뒤 브랜치를 지운다.
- 커널 패치 PR은 **빌드와 selftest 결과**를 반드시 적는다.

**PR 템플릿**

```markdown
## 무엇을 바꿨나
Closes #

## 왜

## 확인한 것
- [ ] 커널 빌드 (경고 없음)
- [ ] seccomp selftest: 기준 결과와 같음 / 다르면 이유
- [ ] 성능 영향 (해당하면): 실험 번호, 조건, 수치

## 리뷰어가 볼 부분
```

## 6. develop → main

- 마일스톤(M1~M3) 판정 회의에서 통과하면 PM이 `develop`을 `main`에 합치고 태그를 단다: `m1`, `m2`, `m3`
- `develop` → `main` PR은 **Create a merge commit**으로 합친다 (Squash 금지). Squash하면 `main`에만 있는 커밋이 생겨 다음 마일스톤 때 기록이 꼬인다.
- 합치기 전에 A가 통합 확인을 한다: 깨끗한 `v6.18`에 `develop`의 패치를 순서대로 적용 → 빌드 → selftest
- A는 매주 목요일에도 같은 통합 확인을 하고 결과를 이슈에 남긴다.

## 7. 측정 결과 올리기

- 위치: `results/<날짜>_<실험>/` (예: `results/2026-11-12_exp1-roundtrip/`)
- 형식: CSV, 열은 `condition,run,metric,value`
- 같은 폴더에 `README.md`로 조건을 적는다: 커널 판(`uname -r`), 적용한 패치(커밋), sysctl 값, 반복 횟수, VM 설정(4 vCPU, 4GB)
- 조건은 번갈아 가며 반복한다(조건당 10회). 원시 데이터를 올리고, 그래프는 스크립트로 다시 만들 수 있게 한다.

## 8. CI (자동 검사)

PR을 열거나 `develop`에 합칠 때 GitHub Actions가 세 가지를 검사한다. 하나라도 실패하면 합칠 수 없다.

| 검사 | 하는 일 | 실패하는 경우 |
| --- | --- | --- |
| 커널 패치 | 깨끗한 v6.18에 `patches/`를 `auto-sync` → `filter-cache` 순서로 적용, checkpatch, 바뀐 `.c` 파일 컴파일 | 패치 충돌, 스타일 경고, 컴파일 경고 |
| 벤치마크 | `bench/*.c` 컴파일 | 컴파일 경고 |
| 측정 결과 | `results/` 폴더마다 `README.md`, CSV 첫 줄 `condition,run,metric,value` | 형식 위반 |

- 커널 전체 빌드, 부팅, selftest는 CI에서 하지 않는다. PR 본문에 각자 VM에서 한 결과를 적는다.
- PR 전에 VM에서 같은 검사를 돌릴 수 있다: `scripts/ci-kernel.sh`, `scripts/check-results.sh`

## 하지 말 것

- `main`, `develop`에 직접 push
- 커널 소스 트리, 빌드 결과물(`*.o`, `*.ko`, `vmlinux`, `Image`), 큰 로그 파일 커밋
- 이슈 없이 작업 시작
- 다른 사람의 브랜치에 말없이 push

GitHub 설정(PM): `main`, `develop`에 브랜치 보호 규칙을 걸어 "PR 필수, CI 통과 필수"를 강제한다. 승인은 필수가 아니다.

## 자주 쓰는 명령

```bash
# 새 작업
git switch develop && git pull
git switch -c feat/<이슈번호>-<설명>

# 작업 중 develop 변경 받아오기
git pull origin develop

# 올리기
git push -u origin <브랜치>

# 합친 뒤 정리
git switch develop && git pull
git branch -d <브랜치>
```
