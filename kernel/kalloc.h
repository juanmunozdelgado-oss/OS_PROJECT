// Estructura compartida entre el kernel y el espacio de usuario
// para exponer estadisticas del asignador de memoria fisica
// (kalloc.c). Forma parte de la modificacion de "Diseno de la
// modificacion de manejo de memoria": el asignador cambio de una
// unica freelist global a freelists independientes por CPU
// (ver kalloc.c), y esta struct expone tanto el total agregado
// como el desglose por CPU para poder evidenciarlo.

#ifndef KALLOC_H
#define KALLOC_H

#define MEMINFO_MAX_CPUS 8 // MOD-MEM: debe coincidir con NCPU (param.h)

struct meminfo {
  uint64 free_pages;  // paginas fisicas libres en este momento (suma de todas las CPUs)
  uint64 total_pages; // paginas fisicas totales gestionadas por kalloc
  uint64 used_pages;  // total_pages - free_pages
  uint64 alloc_count; // contador historico: llamadas exitosas a kalloc() (todas las CPUs)
  uint64 free_count;  // contador historico: llamadas a kfree() (todas las CPUs)

  // MOD-MEM: desglose por CPU, para evidenciar que la memoria esta
  // repartida entre freelists independientes en vez de una sola lista
  // global. free_pages_per_cpu[i] = paginas libres en la freelist de la CPU i.
  uint64 free_pages_per_cpu[MEMINFO_MAX_CPUS];
};

#endif