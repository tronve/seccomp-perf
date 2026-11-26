/* log_check.c - 거부 캐시에서도 감사 로그가 똑같이 남는지 확인
 * 사용법: ./log_check log     SECCOMP_FILTER_FLAG_LOG로 필터를 붙임 (로그가 남아야 함)
 *         ./log_check nolog   플래그 없이 붙임 (ERRNO는 로그가 남지 않아야 함)
 * 필터: getpgid이면 ERRNO(EPERM), 나머지 허용. getpgid를 한 번 부르고 끝낸다. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#if defined(__aarch64__)
#define ARCH AUDIT_ARCH_AARCH64
#elif defined(__x86_64__)
#define ARCH AUDIT_ARCH_X86_64
#endif

int main(int argc, char **argv)
{
	unsigned int flags = (argc > 1 && !strcmp(argv[1], "log")) ? SECCOMP_FILTER_FLAG_LOG : 0;
	struct sock_filter f[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ARCH, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_getpgid, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = { .len = sizeof(f) / sizeof(f[0]), .filter = f };
	long r;

	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, flags, &prog)) {
		perror("seccomp");
		return 1;
	}
	r = syscall(__NR_getpgid, 0);
	printf("%s: getpgid -> %ld (%s)\n", flags ? "log" : "nolog", r, r < 0 ? strerror(errno) : "ok");
	return 0;
}
