// <<< NUEVO ARCHIVO

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

  printf("=== priotest: iniciando en tiempo=%d ===\n", uptime());

  if (fork() == 0) {
    setpriority(0);              // hijo A: prioridad ALTA
    busy_work("A-alta(0)", iterations);
    exit(0);
  }

  if (fork() == 0) {
    setpriority(10);             // hijo B: prioridad NORMAL (default)
    busy_work("B-normal(10)", iterations);
    exit(0);
  }

  if (fork() == 0) {
    setpriority(19);             // hijo C: prioridad BAJA
    busy_work("C-baja(19)", iterations);
    exit(0);
  }

  // el padre espera a los 3 hijos
  wait(0);
  wait(0);
  wait(0);

  printf("=== priotest: todos terminaron ===\n");
  exit(0);
}