#!/usr/bin/env bash
# 라벨과 마일스톤을 한 번에 만든다 (PM이 한 번만 실행). 필요: gh auth login
set -euo pipefail
REPO=${REPO:-tronve/seccomp-perf}

label() { gh label create "$1" --repo "$REPO" --color "$2" --description "$3" --force; }
label "area:policy" 1d76db "자동 직통 모드 정책 (B, C)"
label "area:cache"  0e8a16 "필터 캐시 (D)"
label "area:bench"  5319e7 "벤치마크, 측정 (A)"
label "area:infra"  6a737d "저장소, CI, 스크립트"
label "area:docs"   c5def5 "문서, 회의록, 발표"
label "type:feat"   a2eeef "기능 개발"
label "type:fix"    d73a4a "버그 수정"
label "type:exp"    fbca04 "실험, 측정"
label "type:docs"   0075ca "문서 작업"
label "blocked"     b60205 "다른 작업을 기다리는 중"
label "help-wanted" 008672 "도움 필요"

# GitHub 기본 라벨은 우리 규칙과 겹치므로 지운다 (없으면 무시)
for l in bug documentation duplicate enhancement "good first issue" \
	 "help wanted" invalid question wontfix; do
	gh label delete "$l" --repo "$REPO" --yes 2>/dev/null || true
done

# 마감은 한국 시간 그날 23:59 (= UTC 14:59)
milestone() {
	local args=(-f title="$1" -f description="$2")
	[ -n "${3:-}" ] && args+=(-f due_on="${3}T14:59:59Z")
	gh api "repos/$REPO/milestones" "${args[@]}" --silent && echo "마일스톤 $1"
}
milestone "M0 착수"            "4명 모두 6.18 부팅과 selftest 기준, 팀 저장소, 측정 프로토콜" 2026-11-23
milestone "M1 기준 확보"       "B0·B1·F1 기준 수치, 정책 명세서 v1과 거부 캐시 설계 리뷰"     2026-11-28
milestone "M2 핵심 정책 동작"  "두 방향 적격 조건과 OFF/ON, 번갈아 패턴에서 C가 B0보다 빠름, F2 정확성 통과" 2026-12-03
milestone "M3 확장·결과 확정"  "적응형 기준·이득 확인·백오프, 인자 인식 캐시, 교차 측정, 코드 동결 (발표일 확정 후 마감 추가)"
