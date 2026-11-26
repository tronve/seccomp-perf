# 측정용 seccomp 필터

| 파일 | 내용 |
| --- | --- |
| `docker_default_arm64.bpf` | Docker 기본 프로필, arm64 (aarch64 + arm), 713개 명령 |
| `docker_default_amd64.bpf` | Docker 기본 프로필, amd64 (x86_64 + x86 + x32) |

- 원본: [moby/profiles default.json](https://raw.githubusercontent.com/moby/profiles/main/seccomp/default.json) (2026-11-26에 받음)
- 만든 방법: `scripts/gen_docker_filter.py`, libseccomp 2.5.5 기본 설정, 기본 권한의 일반 컨테이너 기준
- arm64 필터에서 aarch64 시스템 콜 470개 중 항상 허용 256, 항상 거부 210, 인자에 따라 다름 4
- `bench/bpf_bench.c`의 거부 쪽 `acct`는 이 필터에서 ERRNO(EPERM), 실행 명령 약 265개
