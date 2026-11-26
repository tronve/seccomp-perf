/* attach_bench.c - seccomp 필터를 붙이는 데 걸리는 시간 (seccomp() 호출 1회)
 *
 * 필터를 붙일 때 커널은 필터를 검사·복사하고, 허용 캐시와 거부 캐시를 만든다.
 * 이 프로그램은 그 전체 시간을 잰다. 거부 캐시가 더한 시간은 이 값보다 작다 (상한).
 *
 * 사용법: ./attach_bench [반복 횟수=200] [Docker 필터 .bpf]
 * 출력: filter,insns,attach_us (반복의 중앙값, 마이크로초)
 * 필터마다 새 자식 프로세스에서 붙인다 (한 프로세스에 필터를 계속 쌓을 수 없으므로). */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define MAX_INSNS 4096

static struct sock_filter insn[MAX_INSNS];

/* deny_bench.c와 같은 모양: arch 검사, 안 걸리는 비교 pad개, getpgid면 ERRNO, 나머지 허용 */
static unsigned short make_pad_filter(int pad)
{
	int i = 0;

	insn[i++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
	insn[i++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ARCH, 1, 0);
	insn[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
	insn[i++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
	for (int p = 0; p < pad; p++)
		insn[i++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 100000 + p, 0, 0);
	insn[i++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_getpgid, 0, 1);
	insn[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM);
	insn[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
	return (unsigned short)i;
}

static double attach_once(unsigned short len)
{
	struct sock_fprog prog = { .len = len, .filter = insn };
	int fd[2];
	double us = -1;
	pid_t pid;

	if (pipe(fd))
		return -1;
	pid = fork();
	if (pid == 0) {
		struct timespec a, b;

		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		clock_gettime(CLOCK_MONOTONIC, &a);
		if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog))
			_exit(1);
		clock_gettime(CLOCK_MONOTONIC, &b);
		us = (b.tv_sec - a.tv_sec) * 1e6 + (b.tv_nsec - a.tv_nsec) / 1e3;
		if (write(fd[1], &us, sizeof(us)) != sizeof(us))
			_exit(2);
		_exit(0);
	}
	close(fd[1]);
	if (read(fd[0], &us, sizeof(us)) != sizeof(us))
		us = -1;
	close(fd[0]);
	waitpid(pid, NULL, 0);
	return us;
}

static int cmp(const void *x, const void *y)
{
	double a = *(const double *)x, b = *(const double *)y;

	return (a > b) - (a < b);
}

static void report(const char *name, unsigned short len, int reps)
{
	double *t = calloc(reps, sizeof(*t));
	int i;

	for (i = 0; i < reps; i++) {
		t[i] = attach_once(len);
		if (t[i] < 0) {
			fprintf(stderr, "%s: 붙이기 실패\n", name);
			exit(1);
		}
	}
	qsort(t, reps, sizeof(*t), cmp);
	printf("%s,%u,%.1f\n", name, len, t[reps / 2]);
	fflush(stdout);
	free(t);
}

int main(int argc, char **argv)
{
	int reps = argc > 1 ? atoi(argv[1]) : 200;
	int pads[] = { 0, 100, 500, 1000, 2000, 4000 };
	unsigned int k;

	printf("filter,insns,attach_us\n");
	for (k = 0; k < sizeof(pads) / sizeof(pads[0]); k++) {
		char name[32];

		snprintf(name, sizeof(name), "pad%d", pads[k]);
		report(name, make_pad_filter(pads[k]), reps);
	}
	if (argc > 2) {
		FILE *fp = fopen(argv[2], "rb");
		size_t n;

		if (!fp) {
			perror(argv[2]);
			return 1;
		}
		n = fread(insn, sizeof(insn[0]), MAX_INSNS, fp);
		fclose(fp);
		report("docker", (unsigned short)n, reps);
	}
	return 0;
}
