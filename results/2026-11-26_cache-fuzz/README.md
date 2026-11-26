# 거부 캐시 정확성 시험 (무작위 필터)

- 커널: 6.18.0-seccomp+ #6, patches/filter-cache 0001~0005 적용
- 프로그램: bench/cache_fuzz.c (gcc -O2), `sudo ./cache_fuzz 5000` (seed 1부터)
- 방법: 무작위 필터 1~4개를 붙인 자식 프로세스에서 무해한 시스템 콜 10개를 각각 부르고,
  cache_mode 1(필터 실행)과 2(거부 캐시)의 결과(성공, errno, TRAP 데이터, KILL)를 비교
- 결과: 50,000건 중 불일치 0건 (성공 8,372, errno 14,024, TRAP 7,659, KILL 19,945), 39초
