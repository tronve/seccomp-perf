/* 필터 두 개를 차례로 붙이고 멈춰 있는다: 합치기 규칙 확인용
 *
 * 필터 A (먼저, 로그 없음)            필터 B (나중, SECCOMP_FILTER_FLAG_LOG)
 *   getpgid -> ERRNO(EPERM)             getpgid -> ERRNO(EACCES)
 *   getsid  -> ERRNO(EPERM)             getuid  -> KILL_PROCESS
 *   getppid -> 인자를 읽음 (결과 모름)    그 외    -> ALLOW
 *   그 외    -> ALLOW
 *   (둘 다 arch가 다르면 KILL_PROCESS)
 */
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#define LD_ARCH BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch))
#define LD_NR   BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr))
#define LD_ARG0 BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0]))
#define RET(v)  BPF_STMT(BPF_RET | BPF_K, (v))
#define IS(n, t, f) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (n), (t), (f))

static int attach(struct sock_filter *f, unsigned short len, unsigned int flags)
{
	struct sock_fprog prog = { .len = len, .filter = f };

	return syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, flags, &prog);
}

int main(void)
{
	struct sock_filter a[] = {
		LD_ARCH, IS(AUDIT_ARCH_AARCH64, 1, 0), RET(SECCOMP_RET_KILL_PROCESS),
		LD_NR,
		IS(__NR_getpgid, 0, 1), RET(SECCOMP_RET_ERRNO | EPERM),
		IS(__NR_getsid, 0, 1), RET(SECCOMP_RET_ERRNO | EPERM),
		IS(__NR_getppid, 0, 2), LD_ARG0, RET(SECCOMP_RET_ALLOW),
		RET(SECCOMP_RET_ALLOW),
	};
	struct sock_filter b[] = {
		LD_ARCH, IS(AUDIT_ARCH_AARCH64, 1, 0), RET(SECCOMP_RET_KILL_PROCESS),
		LD_NR,
		IS(__NR_getpgid, 0, 1), RET(SECCOMP_RET_ERRNO | EACCES),
		IS(__NR_getuid, 0, 1), RET(SECCOMP_RET_KILL_PROCESS),
		RET(SECCOMP_RET_ALLOW),
	};

	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (attach(a, sizeof(a) / sizeof(a[0]), 0) ||
	    attach(b, sizeof(b) / sizeof(b[0]), SECCOMP_FILTER_FLAG_LOG)) {
		perror("seccomp");
		return 1;
	}
	printf("pid %d: 필터 2개 붙임, 대기 중\n", getpid());
	fflush(stdout);
	pause();
	return 0;
}
