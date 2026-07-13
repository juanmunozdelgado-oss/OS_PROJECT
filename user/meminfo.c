// MOD-MEM: programa de usuario para la modificacion de memoria.
//
// Uso:
//   meminfo              -> imprime las estadisticas actuales una vez.
//   meminfo test [npag]  -> imprime las estadisticas, reserva npag paginas
//                           fisicas (por defecto 200) con sbrk eager,
//                           las toca (para forzar el commit fisico) y
//                           vuelve a imprimir; luego las libera y
//                           vuelve a imprimir una tercera vez.
//
// Sirve como evidencia "antes/despues" para la seccion de Resultados
// y comparacion del informe (memoria original vs. memoria con
// estadisticas).

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/kalloc.h"
#include "user/user.h"

#define PGSIZE 4096

void
print_meminfo(char *label)
{
  struct meminfo mi;

  if (meminfo(&mi) < 0) {
    printf("meminfo: syscall fallo\n");
    exit(1);
  }

  printf("== %s ==\n", label);
  printf("  paginas totales : %ld\n", mi.total_pages);
  printf("  paginas libres  : %ld\n", mi.free_pages);
  printf("  paginas en uso  : %ld\n", mi.used_pages);
  printf("  kalloc() total  : %ld\n", mi.alloc_count);
  printf("  kfree()  total  : %ld\n", mi.free_count);
}

int
main(int argc, char *argv[])
{
  if (argc <= 1) {
    print_meminfo("estadisticas actuales");
    exit(0);
  }

  if (strcmp(argv[1], "test") == 0) {
    int npag = 200; // por defecto ~800 KB
    char *base;
    int i;

    if (argc >= 3)
      npag = atoi(argv[2]);

    print_meminfo("antes de reservar memoria");

    base = sbrk(npag * PGSIZE);
    if (base == SBRK_ERROR) {
      printf("meminfo: sbrk fallo\n");
      exit(1);
    }
    // Tocar cada pagina para forzar la asignacion fisica real
    // (evita que el compilador u otra optimizacion se salte el acceso).
    for (i = 0; i < npag; i++)
      base[i * PGSIZE] = 1;

    print_meminfo("despues de reservar memoria");

    // Devolver la memoria: sbrk con tamano negativo llama a deallocuvm,
    // que a su vez llama kfree() por cada pagina liberada.
    sbrk(-(npag * PGSIZE));

    print_meminfo("despues de liberar memoria");

    exit(0);
  }

  printf("uso: %s [test [npaginas]]\n", argv[0]);
  exit(1);
}