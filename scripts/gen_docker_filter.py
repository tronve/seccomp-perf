#!/usr/bin/env python3
"""Docker 기본 seccomp 프로필(default.json)을 libseccomp로 cBPF 필터 파일로 만든다.

사용법: python3 scripts/gen_docker_filter.py default.json arm64 > gen.c
        gcc -O2 -o gen gen.c -lseccomp && ./gen bench/filters/docker_default_arm64.bpf
필요: libseccomp-dev. 기본 권한(capability)의 일반 컨테이너 기준으로 규칙을 고른다.
default.json: https://raw.githubusercontent.com/moby/profiles/main/seccomp/default.json
"""
import json
import sys

ARCH = {
    "arm64": (["SCMP_ARCH_AARCH64", "SCMP_ARCH_ARM"], {"arm64", "aarch64"}),
    "amd64": (["SCMP_ARCH_X86_64", "SCMP_ARCH_X86", "SCMP_ARCH_X32"], {"amd64", "x86_64"}),
}
CAPS = {"CAP_CHOWN", "CAP_DAC_OVERRIDE", "CAP_FSETID", "CAP_FOWNER", "CAP_MKNOD",
        "CAP_NET_RAW", "CAP_SETGID", "CAP_SETUID", "CAP_SETFCAP", "CAP_SETPCAP",
        "CAP_NET_BIND_SERVICE", "CAP_SYS_CHROOT", "CAP_KILL", "CAP_AUDIT_WRITE"}


def applies(rule, names):
    inc, exc = rule.get("includes") or {}, rule.get("excludes") or {}
    if inc.get("arches") and not names & set(inc["arches"]):
        return False
    if inc.get("caps") and not set(inc["caps"]) <= CAPS:
        return False
    if exc.get("arches") and names & set(exc["arches"]):
        return False
    if exc.get("caps") and set(exc["caps"]) & CAPS:
        return False
    return True


def main():
    prof = json.load(open(sys.argv[1]))
    arches, names = ARCH[sys.argv[2]]
    print("#include <seccomp.h>\n#include <fcntl.h>\n#include <unistd.h>")
    print("int main(int c, char **v) {")
    print("\tscmp_filter_ctx x = seccomp_init(SCMP_ACT_ERRNO(%d));" % prof.get("defaultErrnoRet", 1))
    print("\tseccomp_arch_remove(x, SCMP_ARCH_NATIVE);")
    for a in arches:
        print("\tseccomp_arch_add(x, %s);" % a)
    for rule in prof["syscalls"]:
        if not applies(rule, names) or rule["action"] not in ("SCMP_ACT_ALLOW", "SCMP_ACT_ERRNO"):
            continue
        act = ("SCMP_ACT_ALLOW" if rule["action"] == "SCMP_ACT_ALLOW"
               else "SCMP_ACT_ERRNO(%d)" % rule.get("errnoRet", 1))
        args = rule.get("args") or []
        cmps = "".join(", SCMP_CMP64(%d, %s, %dULL, %dULL)" % (a["index"], a["op"], a["value"], a.get("valueTwo", 0))
                       for a in args)
        for n in rule["names"]:
            print('\t{ int nr = seccomp_syscall_resolve_name("%s"); if (nr != __NR_SCMP_ERROR) '
                  'seccomp_rule_add(x, %s, nr, %d%s); }' % (n, act, len(args), cmps))
    print("\tint fd = open(v[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);")
    print("\tseccomp_export_bpf(x, fd);\n\treturn close(fd);\n}")


main()
