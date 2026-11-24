#!/usr/bin/env bash
# 깨끗한 v6.18에 patches/의 패치를 순서대로 적용하고 checkpatch와 컴파일을 확인한다.
# GitHub Actions와 각자의 VM에서 똑같이 쓴다:  scripts/ci-kernel.sh
#   LINUX_TARBALL=경로  이미 받아 둔 커널 tar 파일을 쓴다 (없으면 kernel.org에서 받음)
set -euo pipefail

VER=6.18
ORDER="auto-sync filter-cache"          # 통합 순서 (README 4장)
REPO=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-$REPO/.ci}
GIT="git -c user.name=ci -c user.email=ci@localhost"

PATCHES=()
for d in $ORDER; do
	for p in "$REPO/patches/$d"/*.patch; do
		[ -e "$p" ] && PATCHES+=("$p")
	done
done
if [ ${#PATCHES[@]} -eq 0 ]; then
	echo "patches/에 패치가 없어 커널 검사를 건너뜀"
	exit 0
fi

mkdir -p "$WORK" && cd "$WORK"
TARBALL=${LINUX_TARBALL:-$WORK/linux-$VER.tar.xz}
[ -f "$TARBALL" ] || wget -nv -O "$TARBALL" \
	"https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$VER.tar.xz"
rm -rf "linux-$VER" && tar xf "$TARBALL" && cd "linux-$VER"
$GIT init -q && $GIT add -A && $GIT commit -qm "base: v$VER"

echo "== 1. 패치 적용 (${#PATCHES[@]}개)"
for p in "${PATCHES[@]}"; do
	echo "   ${p#"$REPO"/}"
	$GIT am -q "$p"
done

echo "== 2. checkpatch"
./scripts/checkpatch.pl --no-signoff --no-tree -q "${PATCHES[@]}"

echo "== 3. 바뀐 .c 파일 컴파일 (경고도 실패로 처리)"
make -s defconfig
./scripts/config --enable SECCOMP_FILTER --enable SECCOMP_CACHE_DEBUG
make -s olddefconfig
mapfile -t OBJS < <($GIT diff --name-only "HEAD~${#PATCHES[@]}" HEAD -- '*.c' | sed 's/\.c$/.o/')
if [ ${#OBJS[@]} -gt 0 ]; then
	make -j"$(nproc)" KCFLAGS=-Werror "${OBJS[@]}"
fi
echo "== 통과"
