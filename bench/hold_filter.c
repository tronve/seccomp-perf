/* getpgid만 EPERM으로 거부하는 필터를 붙이고 멈춰 있는다 */
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

int main(void)
{
struct sock_filter f[] = {
BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_getpgid, 0, 1),
BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
};
struct sock_fprog prog = { .len = sizeof(f) / sizeof(f[0]), .filter = f };

prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog);
printf("pid %d: 필터 붙임, 대기 중\n", getpid());
fflush(stdout);
pause();
return 0;
}
