#ifndef __ZIPX_H__
#define __ZIPX_H__

#include <stdint.h>

typedef struct {
  char name[256];
  uint64_t comp_size;
  uint64_t size;
  uint64_t local_hdr;
  uint16_t method;
} ZipEntry;

typedef struct {
  void *fp;
  ZipEntry *entries;
  int count;
} Zip;

typedef int (*zip_filter_fn)(const char *name, void *ud);

typedef void (*zip_progress_fn)(uint64_t done, uint64_t total, const char *name, void *ud);

int zip_open(Zip *z, const char *path);
void zip_close(Zip *z);

int zip_extract(Zip *z, const char *out_dir, zip_filter_fn filter, void *filter_ud,
                zip_progress_fn progress, void *progress_ud);

uint64_t zip_total_size(Zip *z, zip_filter_fn filter, void *filter_ud);

#endif
