/* argdep_bench.c - 인자를 보는 시스템 콜에서 필터가 드는 시간을 잰다
 *
 * 인자 인식 캐시가 줄일 수 있는 시간의 상한을 보기 위한 것이다. 같은 프로세스에서
 * 필터 없이 한 번, Docker 기본 필터를 붙이고 한 번 재서 차이를 본다.
 *   getppid               항상 허용 (허용 캐시, 비교 기준)
 *   personality(~0)       인자를 봄, 시스템 콜 자체는 아주 쌈 (현재 값 조회)
 *   socket(AF_UNIX)+close socket은 인자를 봄, close는 항상 허용
 *   fork+waitpid          clone은 인자를 봄 (플래그), 시스템 콜 자체가 비쌈
 *
 * 사용법: ./argdep_bench <필터.bpf> [반복 횟수=1000000]   (fork는 반복/1000회)
 * 출력: 지표,값 줄 (scripts/run_bench.sh용). 단위는 이름 끝 (_ns, _us)
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#define MAX_INSNS 4096

static double now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e9 + ts.tv_nsec;
}

static double run_getppid(long iters)
{
	double t0 = now_ns();

	for (long i = 0; i < iters; i++)
		syscall(__NR_getppid);
	return (now_ns() - t0) / iters;
}

static double run_personality(long iters)
{
	double t0 = now_ns();

	for (long i = 0; i < iters; i++)
		syscall(__NR_personality, 0xffffffffUL);
	return (now_ns() - t0) / iters;
}

static double run_socket(long iters)
{
	double t0 = now_ns();

	for (long i = 0; i < iters; i++) {
		int fd = socket(AF_UNIX, SOCK_DGRAM, 0);

		if (fd < 0) {
			perror("socket");
			exit(2);
		}
		close(fd);
	}
	return (now_ns() - t0) / iters;
}

static double run_fork(long iters)
{
	double t0 = now_ns();

	for (long i = 0; i < iters; i++) {
		pid_t pid = fork();

		if (pid < 0) {
			perror("fork");
			exit(2);
		}
		if (pid == 0)
			_exit(0);
		waitpid(pid, NULL, 0);
	}
	return (now_ns() - t0) / iters / 1000;	/* µs */
}

static void measure_all(const char *tag, long iters)
{
	run_getppid(iters / 10);	/* 예열 */
	printf("%s_getppid_ns,%.1f\n", tag, run_getppid(iters));
	printf("%s_personality_ns,%.1f\n", tag, run_personality(iters));
	printf("%s_socket_close_ns,%.1f\n", tag, run_socket(iters / 10));
	printf("%s_fork_us,%.2f\n", tag, run_fork(iters / 1000));
	fflush(stdout);
}

int main(int argc, char **argv)
{
	static struct sock_filter insn[MAX_INSNS];
	long iters = argc > 2 ? atol(argv[2]) : 1000000;
	struct sock_fprog prog;
	FILE *fp;

	if (argc < 2) {
		fprintf(stderr, "사용법: %s <필터.bpf> [반복 횟수]\n", argv[0]);
		return 2;
	}
	fp = fopen(argv[1], "rb");
	if (!fp) {
		perror(argv[1]);
		return 2;
	}
	prog.len = (unsigned short)fread(insn, sizeof(insn[0]), MAX_INSNS, fp);
	prog.filter = insn;
	fclose(fp);

	measure_all("none", iters);
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
	    syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog)) {
		perror("seccomp");
		return 2;
	}
	measure_all("docker", iters);
	return 0;
}
