/* cache_fuzz.c - 거부 캐시 정확성 시험
 *
 * 무작위 seccomp 필터를 1~4개 쌓은 자식 프로세스에서 시스템 콜 하나를 부르고,
 * 그 결과(성공, errno, TRAP 시그널의 데이터, KILL)를 cache_mode 1과 2에서 비교한다.
 *   mode 1: 거부는 필터를 실제로 실행 (정답)
 *   mode 2: 거부는 거부 캐시에서 답 (검사 대상)
 *
 * 사용법: sudo ./cache_fuzz [시험 횟수] [시작 seed]
 *   /proc/sys/kernel/seccomp/cache_mode가 없으면 모드를 바꾸지 않고 같은 조건을 두 번 비교한다
 *   (시험 프로그램 자체를 확인하는 용도).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define MODE_FILE "/proc/sys/kernel/seccomp/cache_mode"
#define MAX_INSNS 512
#define MAX_FILTERS 4

/* 시험할 시스템 콜: 실제로 실행돼도 아무 해가 없는 것들 */
static const long targets[] = {
	__NR_getpid, __NR_getppid, __NR_getuid, __NR_geteuid, __NR_getgid,
	__NR_getegid, __NR_gettid, __NR_getpgid, __NR_getsid, __NR_sched_yield,
};
#define NTARGETS (int)(sizeof(targets) / sizeof(targets[0]))

/* 재현 가능한 난수 (seed가 같으면 같은 필터) */
static unsigned long long rng;
static unsigned int rnd(unsigned int n)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (unsigned int)(rng % n);
}

static unsigned int random_action(void)
{
	switch (rnd(10)) {
	case 0: case 1: case 2: return SECCOMP_RET_ALLOW;
	case 3: case 4: return SECCOMP_RET_ERRNO | (1 + rnd(40));
	case 5: return SECCOMP_RET_TRAP | rnd(50);
	case 6: return SECCOMP_RET_KILL_PROCESS;
	case 7: return SECCOMP_RET_KILL_THREAD;
	case 8: return SECCOMP_RET_LOG;
	default: return rnd(2) ? SECCOMP_RET_TRACE : SECCOMP_RET_USER_NOTIF;
	}
}

static long random_nr(void)
{
	/* 70%는 시험 대상 번호, 30%는 아무 번호 */
	return rnd(10) < 7 ? targets[rnd(NTARGETS)] : (long)rnd(450);
}

struct filter {
	struct sock_filter insn[MAX_INSNS];
	unsigned short len;
	unsigned int flags;
};

#define EMIT(f, x) ((f)->insn[(f)->len++] = (struct sock_filter)x)

static void gen_filter(struct filter *f)
{
	int rules = 1 + rnd(12), i;

	f->len = 0;
	f->flags = rnd(2) ? SECCOMP_FILTER_FLAG_LOG : 0;
	EMIT(f, BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)));
	EMIT(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ARCH, 1, 0));
	EMIT(f, BPF_STMT(BPF_RET | BPF_K, random_action()));
	EMIT(f, BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));
	/* 시험 장치가 쓰는 시스템 콜은 항상 허용 */
	EMIT(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit_group, 3, 0));
	EMIT(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit, 2, 0));
	EMIT(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_rt_sigreturn, 1, 0));
	EMIT(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_seccomp, 0, 1));
	EMIT(f, BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

	for (i = 0; i < rules; i++) {
		switch (rnd(4)) {
		case 0: /* 인자를 보는 규칙: 캐시되면 안 됨 */
			EMIT(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, random_nr(), 0, 4));
			EMIT(f, BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])));
			EMIT(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, rnd(2), 0, 1));
			EMIT(f, BPF_STMT(BPF_RET | BPF_K, random_action()));
			EMIT(f, BPF_STMT(BPF_RET | BPF_K, random_action()));
			break;
		case 1: /* 번호 범위 규칙 */
			EMIT(f, BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, random_nr(), 0, 1));
			EMIT(f, BPF_STMT(BPF_RET | BPF_K, random_action()));
			break;
		default: /* 번호 하나 규칙 */
			EMIT(f, BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, random_nr(), 0, 1));
			EMIT(f, BPF_STMT(BPF_RET | BPF_K, random_action()));
			break;
		}
	}
	EMIT(f, BPF_STMT(BPF_RET | BPF_K, random_action()));
}

/* 자식의 결과를 종료 코드로 전달 */
#define OUT_SUCCESS 0
#define OUT_TRAP_BASE 150

static void on_sigsys(int sig, siginfo_t *si, void *ctx)
{
	(void)sig; (void)ctx;
	_exit(OUT_TRAP_BASE + si->si_errno % 100);  /* TRAP의 DATA */
}

static int run_child(const struct filter *fs, int nf, long nr)
{
	struct sigaction sa;
	pid_t pid;
	int st, i;

	pid = fork();
	if (pid == 0) {
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = on_sigsys;
		sa.sa_flags = SA_SIGINFO;
		sigaction(SIGSYS, &sa, NULL);
		prctl(PR_SET_DUMPABLE, 0);	/* KILL 때 코어 덤프 안 함 */
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		for (i = 0; i < nf; i++) {
			struct sock_fprog p = { .len = fs[i].len, .filter = (struct sock_filter *)fs[i].insn };

			if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, fs[i].flags, &p))
				_exit(254);
		}
		if (syscall(nr, 0, 0, 0, 0, 0, 0) == -1)
			_exit(errno < OUT_TRAP_BASE ? errno : 253);
		_exit(OUT_SUCCESS);
	}
	waitpid(pid, &st, 0);
	if (WIFSIGNALED(st))
		return 1000 + WTERMSIG(st);	/* KILL */
	return WEXITSTATUS(st);
}

static int set_mode(int m)
{
	FILE *fp = fopen(MODE_FILE, "w");

	if (!fp)
		return -1;
	fprintf(fp, "%d\n", m);
	return fclose(fp);
}

static const char *describe(int out, char *buf)
{
	if (out == OUT_SUCCESS)
		return "성공";
	if (out >= 1000)
		sprintf(buf, "KILL(signal %d)", out - 1000);
	else if (out >= OUT_TRAP_BASE && out < 250)
		sprintf(buf, "TRAP(data %d)", out - OUT_TRAP_BASE);
	else if (out >= 250)
		sprintf(buf, "시험 장치 오류(%d)", out);
	else
		sprintf(buf, "errno %d", out);
	return buf;
}

int main(int argc, char **argv)
{
	int trials = argc > 1 ? atoi(argv[1]) : 500;
	unsigned long long seed0 = argc > 2 ? strtoull(argv[2], NULL, 0) : 1;
	int have_mode = access(MODE_FILE, W_OK) == 0;
	long kinds[6] = { 0 }, total = 0, bad = 0;
	struct filter fs[MAX_FILTERS];
	int t, i;

	if (!have_mode)
		printf("주의: %s 없음 → 같은 조건 두 번 비교 (장치 점검용)\n", MODE_FILE);

	for (t = 0; t < trials; t++) {
		unsigned long long seed = seed0 + t;
		int nf;

		rng = seed * 0x9E3779B97F4A7C15ULL | 1;
		nf = 1 + rnd(MAX_FILTERS);
		for (i = 0; i < nf; i++)
			gen_filter(&fs[i]);

		for (i = 0; i < NTARGETS; i++) {
			int a, b;
			char ba[64], bb[64];

			if (have_mode)
				set_mode(1);
			a = run_child(fs, nf, targets[i]);
			if (have_mode)
				set_mode(2);
			b = run_child(fs, nf, targets[i]);
			total++;

			if (a >= 250 && a < 1000) {
				printf("시험 장치 오류: seed %llu, nr %ld, %s\n", seed, targets[i], describe(a, ba));
				return 2;
			}
			kinds[a == OUT_SUCCESS ? 0 : a >= 1000 ? 3 : a >= OUT_TRAP_BASE ? 2 : 1]++;
			if (a != b) {
				bad++;
				printf("불일치: seed %llu, 필터 %d개, nr %ld: mode1 %s, mode2 %s\n",
				       seed, nf, targets[i], describe(a, ba), describe(b, bb));
			}
		}
	}
	if (have_mode)
		set_mode(2);

	printf("비교 %ld건: 성공 %ld, errno %ld, TRAP %ld, KILL %ld\n",
	       total, kinds[0], kinds[1], kinds[2], kinds[3]);
	printf("불일치 %ld건 → %s\n", bad, bad ? "실패" : "통과");
	return bad ? 1 : 0;
}
