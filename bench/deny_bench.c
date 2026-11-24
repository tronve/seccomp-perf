/* deny_bench.c - 필터 길이에 따른 "허용" vs "거부" 시스템 콜 지연 비교
 * 사용법: ./deny_bench [반복 횟수]
 * 필터 구조: arch 확인 → 아무 것도 안 걸리는 비교 N개 → getpgid면 ERRNO → 나머지 ALLOW
 * 모든 판단이 시스템 콜 번호만 보므로, 허용은 기존 캐시에 걸리고 거부는 매번 필터를 끝까지 실행한다. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#if defined(__aarch64__)
#define ARCH AUDIT_ARCH_AARCH64
#elif defined(__x86_64__)
#define ARCH AUDIT_ARCH_X86_64
#endif

static double now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e9 + ts.tv_nsec;
}

static double measure(long nr, long iters)
{
	double t0 = now_ns();
	for (long i = 0; i < iters; i++)
		syscall(nr, 0);
	return (now_ns() - t0) / iters;
}

static int install(int pad)
{
	int len = 4 + pad + 3, i = 0;
	struct sock_filter *f = calloc(len, sizeof(*f));
	f[i++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
	f[i++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ARCH, 1, 0);
	f[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
	f[i++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
	for (int p = 0; p < pad; p++)   /* 있지도 않은 번호와 비교: 항상 다음 줄로 */
		f[i++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 100000 + p, 0, 0);
	f[i++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_getpgid, 0, 1);
	f[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM);
	f[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
	struct sock_fprog prog = { .len = (unsigned short)i, .filter = f };
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
		return -1;
	return syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog);
}

int main(int argc, char **argv)
{
	long iters = argc > 1 ? atol(argv[1]) : 1000000;
	int pads[] = { -1, 0, 100, 500, 1000, 2000, 4000 };

	printf("pad,insns,allow_ns,deny_ns\n");
	fflush(stdout);
	for (unsigned k = 0; k < sizeof(pads) / sizeof(pads[0]); k++) {
		pid_t pid = fork();
		if (pid == 0) {   /* 필터는 쌓이기만 하므로 조건마다 새 프로세스 */
			if (pads[k] >= 0 && install(pads[k])) {
				perror("seccomp");
				_exit(1);
			}
			measure(__NR_getppid, iters / 10);   /* 예열 */
			double a = measure(__NR_getppid, iters);
			double d = measure(__NR_getpgid, iters);
			printf("%d,%d,%.1f,%.1f\n", pads[k], pads[k] < 0 ? 0 : pads[k] + 7, a, d);
			fflush(stdout);
			_exit(0);
		}
		waitpid(pid, NULL, 0);
	}
	return 0;
}
