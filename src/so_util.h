#ifndef __SO_UTIL_H__
#define __SO_UTIL_H__

#include <stdint.h>
#include <stddef.h>
#include <elf.h>

#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

#define SO_MAX_SEGMENTS 8

typedef struct {
  char *symbol;
  uintptr_t func;
} DynLibFunction;

typedef struct so_module {
  struct so_module *next;
  char name[64];

  void *load_base, *load_virtbase;
  size_t load_size;

  Elf64_Phdr phdr[SO_MAX_SEGMENTS * 2];
  int phnum;

  void *so_base;
  size_t so_size;

  Elf64_Ehdr *elf_hdr;
  Elf64_Phdr *prog_hdr;
  Elf64_Shdr *sec_hdr;
  Elf64_Sym *syms;
  int num_syms;
  char *shstrtab;
  char *dynstrtab;

  void (**init_array)(void);
  int num_init;
} so_module;

void hook_arm64(uintptr_t addr, uintptr_t dst);

void so_flush_caches(so_module *mod);
void so_free_temp(so_module *mod);
int so_load(so_module *mod, const char *filename);
int so_relocate(so_module *mod);
int so_resolve(so_module *mod, DynLibFunction *funcs, int num_funcs, int taint_missing_imports);
void so_execute_init_array(so_module *mod);

uintptr_t so_find_addr(so_module *mod, const char *symbol);
uintptr_t so_find_addr_rx(so_module *mod, const char *symbol);

uintptr_t so_try_find_addr_rx(so_module *mod, const char *symbol);

uintptr_t so_lookup_export(so_module *mod, const char *name);
DynLibFunction *so_find_import(DynLibFunction *funcs, int num_funcs, const char *name);
void so_finalize(so_module *mod);
int so_unload(so_module *mod);

so_module *so_first(void);
so_module *so_find_module(const char *basename);
int so_is_module(const void *handle);

uintptr_t so_lookup_export_all(const char *name);

int so_dl_iterate_phdr(int (*callback)(void *info, size_t size, void *data), void *data);

#endif
