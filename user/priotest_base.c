#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
busy_work(char *label, int iterations)
{
  volatile long x = 0;
  for (long i = 0; i < iterations; i++) {
    x += i;
  }
  printf("[%s] termino en tiempo=%d\n", label, uptime());
}

int
main(void)
{
  int iterations = 200000000;

  printf("=== priotest_base: iniciando en tiempo=%d ===\n", uptime());

  if (fork() == 0) { busy_work("A", iterations); exit(0); }
  if (fork() == 0) { busy_work("B", iterations); exit(0); }
  if (fork() == 0) { busy_work("C", iterations); exit(0); }

  wait(0); wait(0); wait(0);

  printf("=== priotest_base: todos terminaron ===\n");
  exit(0);
}
