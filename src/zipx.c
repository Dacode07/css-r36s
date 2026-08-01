#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zipx.h"
#include "zlib_min.h"
#include "util.h"

#define EOCD_SIG 0x06054b50
#define EOCD64_SIG 0x06064b50
#define EOCD64_LOC_SIG 0x07064b50
#define CDIR_SIG 0x02014b50
#define LOCAL_SIG 0x04034b50

#define IN_CHUNK (64 * 1024)
#define OUT_CHUNK (64 * 1024)

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint64_t rd64(const uint8_t *p) {
  return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static long find_eocd(FILE *f, uint8_t *buf, size_t *out_len) {
  if (fseek(f, 0, SEEK_END) != 0)
    return -1;
  const long fsize = ftell(f);
  if (fsize < 22)
    return -1;
  size_t tail = 66 * 1024;
  if ((long)tail > fsize)
    tail = (size_t)fsize;
  const long start = fsize - (long)tail;
  if (fseek(f, start, SEEK_SET) != 0)
    return -1;
  const size_t got = fread(buf, 1, tail, f);
  for (long i = (long)got - 22; i >= 0; i--) {
    if (rd32(buf + i) == EOCD_SIG) {
      *out_len = got;
      return start + i;
    }
  }
  return -1;
}

static void read_zip64_extra(const uint8_t *extra, size_t len, ZipEntry *e,
                             int need_size, int need_comp, int need_off) {
  size_t p = 0;
  while (p + 4 <= len) {
    const uint16_t id = rd16(extra + p);
    const uint16_t sz = rd16(extra + p + 2);
    if (p + 4 + sz > len)
      return;
    if (id == 0x0001) {
      size_t q = p + 4;
      if (need_size && q + 8 <= p + 4 + sz) { e->size = rd64(extra + q); q += 8; }
      if (need_comp && q + 8 <= p + 4 + sz) { e->comp_size = rd64(extra + q); q += 8; }
      if (need_off && q + 8 <= p + 4 + sz) { e->local_hdr = rd64(extra + q); q += 8; }
      return;
    }
    p += 4 + sz;
  }
}

int zip_open(Zip *z, const char *path) {
  memset(z, 0, sizeof(*z));
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  z->fp = f;

  uint8_t *tail = malloc(66 * 1024);
  if (!tail) { fclose(f); z->fp = NULL; return -2; }
  size_t tail_len = 0;
  const long eocd_off = find_eocd(f, tail, &tail_len);
  if (eocd_off < 0) { free(tail); fclose(f); z->fp = NULL; return -3; }

  const uint8_t *eocd = tail + (eocd_off - (long)(ftell(f) - (long)tail_len));

  uint8_t rec[22];
  if (fseek(f, eocd_off, SEEK_SET) != 0 || fread(rec, 1, 22, f) != 22) {
    free(tail); fclose(f); z->fp = NULL; return -3;
  }
  (void)eocd;
  uint64_t count = rd16(rec + 10);
  uint64_t cd_off = rd32(rec + 16);

  if (count == 0xffff || cd_off == 0xffffffffu) {
    uint8_t loc[20];
    if (fseek(f, eocd_off - 20, SEEK_SET) == 0 && fread(loc, 1, 20, f) == 20 &&
        rd32(loc) == EOCD64_LOC_SIG) {
      const uint64_t e64 = rd64(loc + 8);
      uint8_t rec64[56];
      if (fseek(f, (long)e64, SEEK_SET) == 0 && fread(rec64, 1, 56, f) == 56 &&
          rd32(rec64) == EOCD64_SIG) {
        count = rd64(rec64 + 32);
        cd_off = rd64(rec64 + 48);
      }
    }
  }
  free(tail);

  if (!count || count > 200000) { fclose(f); z->fp = NULL; return -4; }
  z->entries = calloc((size_t)count, sizeof(ZipEntry));
  if (!z->entries) { fclose(f); z->fp = NULL; return -2; }

  if (fseek(f, (long)cd_off, SEEK_SET) != 0) { zip_close(z); return -5; }
  uint8_t hdr[46];
  for (uint64_t i = 0; i < count; i++) {
    if (fread(hdr, 1, 46, f) != 46 || rd32(hdr) != CDIR_SIG)
      break;
    ZipEntry *e = &z->entries[z->count];
    e->method = rd16(hdr + 10);
    e->comp_size = rd32(hdr + 20);
    e->size = rd32(hdr + 24);
    e->local_hdr = rd32(hdr + 42);
    const uint16_t nlen = rd16(hdr + 28);
    const uint16_t elen = rd16(hdr + 30);
    const uint16_t clen = rd16(hdr + 32);

    if (nlen >= sizeof(e->name)) {
      if (fseek(f, nlen + elen + clen, SEEK_CUR) != 0) break;
      continue;
    }
    if (fread(e->name, 1, nlen, f) != nlen) break;
    e->name[nlen] = '\0';

    if (elen) {
      uint8_t *extra = malloc(elen);
      if (!extra) break;
      if (fread(extra, 1, elen, f) != elen) { free(extra); break; }
      read_zip64_extra(extra, elen, e, e->size == 0xffffffffu,
                       e->comp_size == 0xffffffffu, e->local_hdr == 0xffffffffu);
      free(extra);
    }
    if (clen && fseek(f, clen, SEEK_CUR) != 0)
      break;
    z->count++;
  }

  if (!z->count) { zip_close(z); return -6; }
  return 0;
}

void zip_close(Zip *z) {
  if (z->fp)
    fclose((FILE *)z->fp);
  free(z->entries);
  z->fp = NULL;
  z->entries = NULL;
  z->count = 0;
}

uint64_t zip_total_size(Zip *z, zip_filter_fn filter, void *ud) {
  uint64_t total = 0;
  for (int i = 0; i < z->count; i++)
    if (filter(z->entries[i].name, ud))
      total += z->entries[i].size;
  return total;
}

static const char *base_name(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

static int seek_to_data(FILE *f, const ZipEntry *e) {
  uint8_t lh[30];
  if (fseek(f, (long)e->local_hdr, SEEK_SET) != 0)
    return -1;
  if (fread(lh, 1, 30, f) != 30 || rd32(lh) != LOCAL_SIG)
    return -1;
  const uint16_t nlen = rd16(lh + 26);
  const uint16_t elen = rd16(lh + 28);
  return fseek(f, nlen + elen, SEEK_CUR) == 0 ? 0 : -1;
}

static int extract_one(FILE *f, const ZipEntry *e, const char *out_path,
                       uint8_t *inbuf, uint8_t *outbuf,
                       uint64_t *done, uint64_t total,
                       zip_progress_fn progress, void *pud) {
  if (seek_to_data(f, e) != 0)
    return -1;
  FILE *o = fopen(out_path, "wb");
  if (!o)
    return -2;

  int rc = 0;
  uint64_t left = e->comp_size;

  if (e->method == 0) {
    while (left) {
      const size_t want = left < IN_CHUNK ? (size_t)left : IN_CHUNK;
      const size_t got = fread(inbuf, 1, want, f);
      if (!got) { rc = -3; break; }
      if (fwrite(inbuf, 1, got, o) != got) { rc = -4; break; }
      left -= got;
      *done += got;
      if (progress) progress(*done, total, base_name(e->name), pud);
    }
  } else if (e->method == 8) {
    z_stream_min zs;
    memset(&zs, 0, sizeof(zs));
    if (zm_inflate_init(&zs) != ZM_OK) { fclose(o); return -5; }
    int zret = ZM_OK;
    while (rc == 0) {
      if (zs.avail_in == 0 && left) {
        const size_t want = left < IN_CHUNK ? (size_t)left : IN_CHUNK;
        const size_t got = fread(inbuf, 1, want, f);
        if (!got) { rc = -3; break; }
        left -= got;
        zs.next_in = inbuf;
        zs.avail_in = (unsigned int)got;
      }
      zs.next_out = outbuf;
      zs.avail_out = OUT_CHUNK;
      zret = inflate(&zs, ZM_NO_FLUSH);
      if (zret != ZM_OK && zret != ZM_STREAM_END) { rc = -6; break; }
      const size_t produced = OUT_CHUNK - zs.avail_out;
      if (produced && fwrite(outbuf, 1, produced, o) != produced) { rc = -4; break; }
      *done += produced;
      if (progress) progress(*done, total, base_name(e->name), pud);
      if (zret == ZM_STREAM_END)
        break;
      if (!produced && !zs.avail_in && !left) { rc = -6; break; }
    }
    inflateEnd(&zs);
  } else {
    rc = -7;
  }

  if (fclose(o) != 0 && rc == 0)
    rc = -4;
  return rc;
}

int zip_extract(Zip *z, const char *out_dir, zip_filter_fn filter, void *fud,
                zip_progress_fn progress, void *pud) {
  FILE *f = (FILE *)z->fp;
  if (!f)
    return -1;

  const uint64_t total = zip_total_size(z, filter, fud);
  uint64_t done = 0;
  int written = 0;

  uint8_t *inbuf = malloc(IN_CHUNK);
  uint8_t *outbuf = malloc(OUT_CHUNK);
  if (!inbuf || !outbuf) { free(inbuf); free(outbuf); return -2; }

  for (int i = 0; i < z->count; i++) {
    ZipEntry *e = &z->entries[i];
    if (!filter(e->name, fud))
      continue;
    const char *bn = base_name(e->name);
    if (!bn[0])
      continue;
    char out_path[512];
    snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, bn);
    const int rc = extract_one(f, e, out_path, inbuf, outbuf, &done, total, progress, pud);
    if (rc != 0) {
      debugPrintf("setup: extracting %s failed (%d)\n", e->name, rc);
      remove(out_path);
      free(inbuf); free(outbuf);
      return rc;
    }
    written++;
  }

  free(inbuf);
  free(outbuf);
  return written;
}
