#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "libc_shim.h"
#include "util.h"

void *__memcpy_chk_fake(void *dst, const void *src, size_t n, size_t dstlen) {
  (void)dstlen;
  return memcpy(dst, src, n);
}

void *__memmove_chk_fake(void *dst, const void *src, size_t n, size_t dstlen) {
  (void)dstlen;
  return memmove(dst, src, n);
}

char *__strcat_chk_fake(char *dst, const char *src, size_t dstlen) {
  (void)dstlen;
  return strcat(dst, src);
}

char *__strchr_chk_fake(const char *s, int c, size_t slen) {
  (void)slen;
  return strchr(s, c);
}

char *__strcpy_chk_fake(char *dst, const char *src, size_t dstlen) {
  (void)dstlen;
  return strcpy(dst, src);
}

size_t __strlen_chk_fake(const char *s, size_t slen) {
  (void)slen;
  return strlen(s);
}

char *__strncat_chk_fake(char *dst, const char *src, size_t n, size_t dstlen) {
  (void)dstlen;
  return strncat(dst, src, n);
}

char *__strncpy_chk_fake(char *dst, const char *src, size_t n, size_t dstlen) {
  (void)dstlen;
  return strncpy(dst, src, n);
}

static size_t pf_bound(size_t n) { return n > 0x7fffffff ? (size_t)0x7fffffff : n; }

int __vsnprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, va_list va) {
  (void)flag; (void)slen;
  return vsnprintf(s, pf_bound(maxlen), fmt, va);
}

void __FD_SET_chk_fake(int fd, void *set, size_t set_size) {
  (void)set_size;
  uint64_t *words = set;
  if (fd >= 0 && fd < 1024)
    words[fd >> 6] |= 1ull << (fd & 63);
}

int __FD_ISSET_chk_fake(int fd, const void *set, size_t set_size) {
  (void)set_size;
  const uint64_t *words = set;
  if (fd < 0 || fd >= 1024)
    return 0;
  return (words[fd >> 6] >> (fd & 63)) & 1;
}

void *__memset_chk_fake(void *s, int c, size_t n, size_t slen) {
  (void)slen;
  return memset(s, c, n);
}

long __read_chk_fake(int fd, void *buf, size_t count, size_t buflen) {
  (void)buflen;
  return read(fd, buf, count);
}

long __recvfrom_chk_fake(int fd, void *buf, size_t len, size_t buflen,
                         int flags, void *from, int *fromlen) {
  (void)buflen;
  socklen_t sl = fromlen ? (socklen_t)*fromlen : 0;
  const long r = recvfrom(fd, buf, len, flags, from, fromlen ? &sl : NULL);
  if (fromlen)
    *fromlen = (int)sl;
  return r;
}

int __snprintf_chk_fake(char *s, size_t n, int flag, size_t slen, const char *fmt, ...) {
  (void)flag; (void)slen;
  va_list va;
  va_start(va, fmt);
  const int r = vsnprintf(s, pf_bound(n), fmt, va);
  va_end(va);
  return r;
}

int __sprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, ...) {
  (void)flag; (void)slen;
  va_list va;
  va_start(va, fmt);
  const int r = vsprintf(s, fmt, va);
  va_end(va);
  return r;
}

int __vsprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, va_list va) {
  (void)flag; (void)slen;
  return vsprintf(s, fmt, va);
}

char *__strncpy_chk2_fake(char *dst, const char *src, size_t n, size_t dstlen, size_t srclen) {
  (void)dstlen; (void)srclen;
  return strncpy(dst, src, n);
}

char *__strrchr_chk_fake(const char *s, int c, size_t slen) {
  (void)slen;
  return strrchr(s, c);
}

int __system_property_get_fake(const char *name, char *value) {
  (void)name;
  value[0] = '\0';
  return 0;
}

size_t __ctype_get_mb_cur_max_fake(void) {
  return 1;
}

void __google_potentially_blocking_region_begin_fake(void) {}
void __google_potentially_blocking_region_end_fake(void) {}

static unsigned char g_ctype_table[1 + 256];
const char *bionic_ctype = (const char *)g_ctype_table;

__attribute__((constructor)) static void init_bionic_ctype(void) {
  for (int c = 0; c < 256; c++) {
    unsigned char f = 0;
    if (isupper(c))            f |= 0x01;
    if (islower(c))            f |= 0x02;
    if (isdigit(c))            f |= 0x04;
    if (isspace(c))            f |= 0x08;
    if (ispunct(c))            f |= 0x10;
    if (iscntrl(c))            f |= 0x20;
    if (isxdigit(c))           f |= 0x40;
    if (c == ' ' || c == '\t') f |= 0x80;
    g_ctype_table[c + 1] = f;
  }
}

int gettid_fake(void) {
  return (int)gettid();
}

void android_set_abort_message_fake(const char *msg) {
  debugPrintf("abort message: %s\n", msg ? msg : "(null)");
}

int __register_atfork_fake(void) {
  return 0;
}

void __assert2_fake(const char *file, int line, const char *func, const char *expr) {
  debugPrintf("assertion failed:\n%s:%d (%s): %s\n", file, line, func, expr);
  abort();
}

int __fpclassifyd_fake(double x) {
  switch (fpclassify(x)) {
    case FP_INFINITE:  return 0x01;
    case FP_NAN:       return 0x02;
    case FP_SUBNORMAL: return 0x08;
    case FP_ZERO:      return 0x10;
    default:           return 0x04;
  }
}

int __isnanf_fake(float x) {
  return isnan(x);
}

bionic_mallinfo mallinfo_fake(void) {
  bionic_mallinfo m;
  memset(&m, 0, sizeof(m));
  return m;
}

char *basename_fake(const char *path) {
  static __thread char buf[256];
  if (!path || !path[0]) {
    strcpy(buf, ".");
    return buf;
  }
  const char *slash = strrchr(path, '/');
  snprintf(buf, sizeof(buf), "%s", slash ? slash + 1 : path);
  return buf;
}

int strerror_r_fake(int err, char *buf, size_t len) {

  const char *s = strerror(err);
  if (!buf || !len)
    return ERANGE;
  snprintf(buf, len, "%s", s ? s : "unknown error");
  return 0;
}

int system_fake(const char *cmd) {
  tracePrintf("system(%s) blocked\n", cmd ? cmd : "(null)");
  return -1;
}

FILE *popen_fake(const char *cmd, const char *mode) {
  (void)cmd; (void)mode;
  return NULL;
}

int pclose_fake(FILE *f) {
  (void)f;
  return -1;
}

struct bionic_addrinfo {
  int ai_flags;
  int ai_family;
  int ai_socktype;
  int ai_protocol;
  socklen_t ai_addrlen;
  char *ai_canonname;
  struct sockaddr *ai_addr;
  struct bionic_addrinfo *ai_next;
};

#define BIONIC_EAI_NONAME 8

int getaddrinfo_fake(const char *node, const char *service, const void *hints, void **res) {
  if (res)
    *res = NULL;

  struct addrinfo ghints, *gres = NULL;
  struct addrinfo *hp = NULL;
  if (hints) {
    const struct bionic_addrinfo *bh = hints;
    memset(&ghints, 0, sizeof(ghints));
    ghints.ai_flags = bh->ai_flags;
    ghints.ai_family = bh->ai_family;
    ghints.ai_socktype = bh->ai_socktype;
    ghints.ai_protocol = bh->ai_protocol;
    hp = &ghints;
  }

  if (getaddrinfo(node, service, hp, &gres) != 0 || !gres)
    return BIONIC_EAI_NONAME;

  struct bionic_addrinfo *head = NULL, *tail = NULL;
  for (const struct addrinfo *g = gres; g; g = g->ai_next) {
    struct bionic_addrinfo *b = calloc(1, sizeof(*b));
    if (!b)
      break;
    b->ai_flags = g->ai_flags;
    b->ai_family = g->ai_family;
    b->ai_socktype = g->ai_socktype;
    b->ai_protocol = g->ai_protocol;
    b->ai_addrlen = g->ai_addrlen;
    if (g->ai_addr && g->ai_addrlen) {
      b->ai_addr = malloc(g->ai_addrlen);
      if (b->ai_addr)
        memcpy(b->ai_addr, g->ai_addr, g->ai_addrlen);
    }
    if (g->ai_canonname)
      b->ai_canonname = strdup(g->ai_canonname);
    if (tail)
      tail->ai_next = b;
    else
      head = b;
    tail = b;
  }
  freeaddrinfo(gres);

  if (!head)
    return BIONIC_EAI_NONAME;
  if (res)
    *res = head;
  return 0;
}

void freeaddrinfo_fake(void *res) {
  struct bionic_addrinfo *b = res;
  while (b) {
    struct bionic_addrinfo *next = b->ai_next;
    free(b->ai_addr);
    free(b->ai_canonname);
    free(b);
    b = next;
  }
}

uint8_t fake_sF[3][0x100];

static int is_fake_file(const void *f) {
  const uint8_t *p = f;
  const uint8_t *base = (const uint8_t *)fake_sF;
  return p >= base && p < base + sizeof(fake_sF);
}

size_t fwrite_fake(const void *ptr, size_t size, size_t n, FILE *f) {
  if (is_fake_file(f)) {
    static char buf[0x400];
    const size_t total = size * n < sizeof(buf) - 1 ? size * n : sizeof(buf) - 1;
    memcpy(buf, ptr, total);
    buf[total] = '\0';
    engineStream(buf);
    return n;
  }
  return fwrite(ptr, size, n, f);
}

size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f) {
  if (is_fake_file(f))
    return 0;
  return fread(ptr, size, n, f);
}

int fputc_fake(int c, FILE *f) {
  if (is_fake_file(f))
    return c;
  return fputc(c, f);
}

int fputs_fake(const char *s, FILE *f) {
  if (is_fake_file(f)) {
    engineStream(s);
    return 0;
  }
  return fputs(s, f);
}

int fflush_fake(FILE *f) {
  if (is_fake_file(f) || f == NULL)
    return 0;
  return fflush(f);
}

int fclose_fake(FILE *f) {
  if (is_fake_file(f))
    return 0;
  return fclose(f);
}

int ferror_fake(FILE *f) {
  if (is_fake_file(f))
    return 0;
  return ferror(f);
}

int feof_fake(FILE *f) {
  if (is_fake_file(f))
    return 1;
  return feof(f);
}

int fileno_fake(FILE *f) {
  if (is_fake_file(f))
    return ((const uint8_t *)f - &fake_sF[0][0]) / 0x100;
  return fileno(f);
}

int fprintf_fake(FILE *f, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  int ret;
  if (is_fake_file(f)) {
    static char buf[0x400];
    ret = vsnprintf(buf, sizeof(buf), fmt, va);
    engineStream(buf);
  } else {
    ret = vfprintf(f, fmt, va);
  }
  va_end(va);
  return ret;
}

int vfprintf_fake(FILE *f, const char *fmt, va_list va) {
  if (is_fake_file(f)) {
    static char buf[0x400];
    int ret = vsnprintf(buf, sizeof(buf), fmt, va);
    engineStream(buf);
    return ret;
  }
  return vfprintf(f, fmt, va);
}

int fscanf_fake(FILE *f, const char *fmt, ...) {
  if (is_fake_file(f))
    return -1;
  va_list va;
  va_start(va, fmt);
  int ret = vfscanf(f, fmt, va);
  va_end(va);
  return ret;
}

int fseek_fake(FILE *f, long off, int whence) {
  if (is_fake_file(f))
    return -1;
  return fseek(f, off, whence);
}

int fgetc_fake(FILE *f) {
  if (is_fake_file(f))
    return -1;
  return fgetc(f);
}

char *fgets_fake(char *s, int n, FILE *f) {
  if (is_fake_file(f))
    return NULL;
  return fgets(s, n, f);
}

int ungetc_fake(int c, FILE *f) {
  if (is_fake_file(f))
    return -1;
  return ungetc(c, f);
}

void rewind_fake(FILE *f) {
  if (is_fake_file(f))
    return;
  rewind(f);
}

FILE *freopen_fake(const char *path, const char *mode, FILE *f) {
  if (is_fake_file(f))
    return f;
  return freopen(path, mode, f);
}

int setvbuf_fake(FILE *f, char *buf, int mode, size_t size) {
  if (is_fake_file(f))
    return 0;
  return setvbuf(f, buf, mode, size);
}

static const char fake_meminfo[] =
  "MemTotal:        3276800 kB\n"
  "MemFree:         2097152 kB\n"
  "MemAvailable:    2621440 kB\n"
  "Buffers:               0 kB\n"
  "Cached:                0 kB\n"
  "SwapTotal:             0 kB\n"
  "SwapFree:              0 kB\n";

static int is_side_file(const char *path) {
  if (!path)
    return 0;
  const char *b = strrchr(path, '/');
  b = b ? b + 1 : path;
  return !strcmp(b, "engine.log") || !strcmp(b, "steam_appid.txt");
}

static int is_write_mode(const char *mode) {
  return mode && (*mode == 'w' || *mode == 'a');
}

FILE *fopen_fake(const char *path, const char *mode) {
  if (path && !strcmp(path, "/proc/meminfo"))
    return fmemopen((void *)fake_meminfo, sizeof(fake_meminfo) - 1, "r");
  if (is_side_file(path) && is_write_mode(mode))
    return fopen("/dev/null", mode);
  return fopen(path, mode);
}

static char proc_meminfo_path[512];

void proc_files_init(const char *install_root) {
  snprintf(proc_meminfo_path, sizeof(proc_meminfo_path), "%s/.meminfo", install_root);
  FILE *f = fopen(proc_meminfo_path, "wb");
  if (f) {
    fwrite(fake_meminfo, 1, sizeof(fake_meminfo) - 1, f);
    fclose(f);
  }
}

int open_fake(const char *path, int flags, ...) {
  int mode = 0666;
  if (flags & 0100) {
    va_list va;
    va_start(va, flags);
    mode = va_arg(va, int);
    va_end(va);
  }
  if (path && !strcmp(path, "/proc/meminfo") && proc_meminfo_path[0])
    path = proc_meminfo_path;
  if (is_side_file(path) && (flags & 0103))
    path = "/dev/null";
  return open(path, flags, mode);
}

__thread uint8_t bionic_tls_pad[4096]
    __attribute__((tls_model("initial-exec"), aligned(16), used));

void fake_tls_install(void) {

  bionic_tls_pad[0] = bionic_tls_pad[0];
}
