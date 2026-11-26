/* bpf_bench.c - 파일로 받은 seccomp 필터를 붙이고 허용/거부 시스템 콜 1회 지연을 잰다
 * 사용법: ./bpf_bench <필터.bpf | none> [반복 횟수]
 *   필터 파일 = libseccomp의 seccomp_export_bpf() 결과 (struct sock_filter 배열 그대로)
 *   허용 쪽: getppid, 거부 쪽: acct(NULL) (Docker 기본 프로필에서 EPERM)
 * 출력: allow_ns,deny_ns (한 줄) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#define MAX_INSNS 4096

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

int main(int argc, char **argv)
{
	static struct sock_filter insn[MAX_INSNS];
	long iters = argc > 2 ? atol(argv[2]) : 1000000;
	struct sock_fprog prog;
	size_t n = 0;

	if (argc < 2) {
		fprintf(stderr, "사용법: %s <필터.bpf | none> [반복 횟수]\n", argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "none")) {
		FILE *fp = fopen(argv[1], "rb");

		if (!fp) {
			perror(argv[1]);
			return 2;
		}
		n = fread(insn, sizeof(insn[0]), MAX_INSNS, fp);
		fclose(fp);
		prog.len = (unsigned short)n;
		prog.filter = insn;
		if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
		    syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog)) {
			perror("seccomp");
			return 2;
		}
	}
	measure(__NR_getppid, iters / 10);	/* 예열 */
	printf("%.1f,%.1f\n", measure(__NR_getppid, iters), measure(__NR_acct, iters));
	return 0;
}
