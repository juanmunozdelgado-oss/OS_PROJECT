// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
//
// MOD-MEM: el asignador original de xv6 usaba UNA sola freelist global
// protegida por UN solo candado (kmem.lock), que serializaba todas las
// asignaciones/liberaciones de memoria entre todos los harts (CPUs). Esta
// era una limitacion explicita identificada en el analisis del esquema
// original: "Un solo candado global serializa todas las asignaciones
// entre CPUs (posible cuello de botella)".
//
// La modificacion implementada aqui cambia la ESTRATEGIA DE ASIGNACION:
// en vez de una freelist global, cada CPU tiene su propia freelist y su
// propio candado (kmem[NCPU]). kalloc() primero intenta tomar una pagina
// de la freelist de SU PROPIA CPU (sin contencion con las demas). Solo si
// esa freelist esta vacia, "roba" una pagina de la freelist de otra CPU
// (work stealing), tomando ese candado ajeno solo momentaneamente.
//
// Esto reduce drasticamente la contencion del candado en sistemas con
// varios nucleos ejecutando asignaciones de memoria en paralelo (por
// ejemplo, varios procesos haciendo fork()/sbrk() al mismo tiempo en
// distintas CPUs), sin cambiar el comportamiento observable de kalloc()/
// kfree() para el resto del kernel.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "kalloc.h" // MOD-MEM: struct meminfo compartida con el syscall meminfo()

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// MOD-MEM: una freelist independiente por cada CPU, cada una con su
// propio candado. Reemplaza la unica "struct kmem" global del asignador
// original.
struct kmem_cpu {
  struct spinlock lock;
  struct run *freelist;
  uint64 free_pages;  // paginas libres actualmente en ESTA freelist
  uint64 alloc_count;  // llamadas exitosas a kalloc() servidas por esta CPU
  uint64 free_count;   // llamadas a kfree() que devolvieron paginas aqui
};

static struct kmem_cpu kmem[NCPU];

// MOD-MEM: total de paginas fisicas gestionadas por todo el sistema.
// Es fijo una vez calculado en kinit() (no cambia en tiempo de ejecucion),
// por lo que no necesita candado propio.
static uint64 total_pages_all;

void
kinit()
{
  int i;

  for (i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
    kmem[i].freelist = 0;
    kmem[i].free_pages = 0;
    kmem[i].alloc_count = 0;
    kmem[i].free_count = 0;
  }

  total_pages_all = ((uint64)PHYSTOP - PGROUNDUP((uint64)end)) / PGSIZE;

  // kinit() se ejecuta una sola vez, en el hart 0, antes de que arranquen
  // los demas nucleos (ver main.c: "if (cpuid() == 0) { ... kinit(); ...}
  // else { while(started==0); ... }"). Por eso es seguro repartir las
  // paginas iniciales sin condiciones de carrera.
  freerange(end, (void *)PHYSTOP);
}

// MOD-MEM: reparte las paginas del rango inicial de forma equitativa
// (round-robin) entre las NCPU freelists, en vez de meterlas todas en una
// sola lista global. Asi, desde el arranque, cada CPU ya tiene memoria
// propia disponible sin depender de las demas.
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  int cpu = 0;

  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    struct run *r = (struct run *)p;

    memset(p, 1, PGSIZE); // relleno de basura, igual que en kfree()

    acquire(&kmem[cpu].lock);
    r->next = kmem[cpu].freelist;
    kmem[cpu].freelist = r;
    kmem[cpu].free_pages++;
    release(&kmem[cpu].lock);

    cpu = (cpu + 1) % NCPU; // MOD-MEM: siguiente CPU, en round-robin
  }
}

// MOD-MEM: obtiene el id de la CPU actual de forma segura (cpuid() exige
// interrupciones deshabilitadas para evitar que el proceso migre de CPU
// mientras se lee). No se necesita que el id siga siendo valido despues:
// si el proceso migra justo despues, en el peor caso se usa la freelist
// "equivocada", lo cual es inofensivo para la correctitud, solo afecta
// un poco la localidad.
static int
current_cpu(void)
{
  int id;
  push_off();
  id = cpuid();
  pop_off();
  return id;
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int id;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  // MOD-MEM: la pagina se devuelve a la freelist de la CPU que la libera
  // (no necesariamente la que la asigno originalmente). Esto mantiene la
  // localidad: si un proceso libera memoria, es probable que otro
  // proceso en la MISMA CPU la vuelva a pedir pronto.
  id = current_cpu();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  kmem[id].free_pages++;
  kmem[id].free_count++;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int id, i;

  id = current_cpu();

  // MOD-MEM: [1] intento rapido, sin contencion: tomar una pagina de la
  // freelist de la PROPIA CPU.
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if (r) {
    kmem[id].freelist = r->next;
    kmem[id].free_pages--;
    kmem[id].alloc_count++;
  }
  release(&kmem[id].lock);

  // MOD-MEM: [2] work stealing: si la freelist propia esta vacia, se
  // recorren las freelists de las demas CPUs (en orden circular a partir
  // de la propia) y se toma UNA pagina prestada de la primera que tenga
  // memoria disponible. Solo se mantiene un candado a la vez (nunca dos
  // simultaneos), por lo que no hay riesgo de deadlock por orden de
  // adquisicion de candados.
  if (r == 0) {
    for (i = 1; i < NCPU; i++) {
      int victim = (id + i) % NCPU;

      acquire(&kmem[victim].lock);
      r = kmem[victim].freelist;
      if (r) {
        kmem[victim].freelist = r->next;
        kmem[victim].free_pages--;
        kmem[victim].alloc_count++; // se contabiliza en la CPU "prestamista"
      }
      release(&kmem[victim].lock);

      if (r)
        break; // ya se consiguio una pagina, no hace falta seguir robando
    }
  }

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

// MOD-MEM: devuelve una foto (snapshot) agregada de las estadisticas de
// TODAS las CPUs. Recorre las NCPU freelists tomando cada candado solo
// brevemente (uno a la vez, nunca dos al tiempo) para sumar sus valores.
// Ademas llena per_cpu_free_pages para poder mostrar, en el programa de
// prueba, como se distribuye la memoria entre nucleos y evidenciar que
// la estrategia por-CPU realmente esta en uso.
void
kmeminfo(struct meminfo *mi)
{
  int i;
  uint64 free_sum = 0, alloc_sum = 0, free_count_sum = 0;

  for (i = 0; i < NCPU; i++) {
    acquire(&kmem[i].lock);
    mi->free_pages_per_cpu[i] = kmem[i].free_pages;
    free_sum += kmem[i].free_pages;
    alloc_sum += kmem[i].alloc_count;
    free_count_sum += kmem[i].free_count;
    release(&kmem[i].lock);
  }

  mi->total_pages = total_pages_all;
  mi->free_pages = free_sum;
  mi->used_pages = total_pages_all - free_sum;
  mi->alloc_count = alloc_sum;
  mi->free_count = free_count_sum;
}