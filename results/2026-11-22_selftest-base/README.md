# seccomp selftest 기준 결과 (6.18 그대로, 패치 없음)

- 프로그램: tools/testing/selftests/seccomp/seccomp_bpf
- 파일: selftest_base_<사용자>.txt (사람마다 하나)

| 담당 | 커널 | VM | 결과 |
| --- | --- | --- | --- |
| 이준원 | 6.18.0-seccomp | UTM arm64, 4 vCPU, 4GB | PASSED 111/111 (pass 99, skip 12: 전부 UPROBE, ARM64에는 해당 시스템 콜이 없어 정상) |
