#ifndef __LIBC_SHIM_H__
#define __LIBC_SHIM_H__

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/socket.h>

extern uint8_t fake_sF[3][0x100];

size_t fwrite_fake(const void *ptr, size_t size, size_t n, FILE *f);
size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f);
int fputc_fake(int c, FILE *f);
int fputs_fake(const char *s, FILE *f);
int fflush_fake(FILE *f);
int fclose_fake(FILE *f);
int ferror_fake(FILE *f);
int feof_fake(FILE *f);
int fileno_fake(FILE *f);
int fprintf_fake(FILE *f, const char *fmt, ...);
int vfprintf_fake(FILE *f, const char *fmt, va_list va);
int fscanf_fake(FILE *f, const char *fmt, ...);
int fseek_fake(FILE *f, long off, int whence);
int fgetc_fake(FILE *f);
char *fgets_fake(char *s, int n, FILE *f);
int ungetc_fake(int c, FILE *f);
FILE *freopen_fake(const char *path, const char *mode, FILE *f);
void rewind_fake(FILE *f);
int setvbuf_fake(FILE *f, char *buf, int mode, size_t size);

void *__memcpy_chk_fake(void *dst, const void *src, size_t n, size_t dstlen);
void *__memmove_chk_fake(void *dst, const void *src, size_t n, size_t dstlen);
char *__strcat_chk_fake(char *dst, const char *src, size_t dstlen);
char *__strchr_chk_fake(const char *s, int c, size_t slen);
char *__strcpy_chk_fake(char *dst, const char *src, size_t dstlen);
size_t __strlen_chk_fake(const char *s, size_t slen);
char *__strncat_chk_fake(char *dst, const char *src, size_t n, size_t dstlen);
char *__strncpy_chk_fake(char *dst, const char *src, size_t n, size_t dstlen);
int __vsnprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, va_list va);
void __FD_SET_chk_fake(int fd, void *set, size_t set_size);
int __FD_ISSET_chk_fake(int fd, const void *set, size_t set_size);
void *__memset_chk_fake(void *s, int c, size_t n, size_t slen);
long __read_chk_fake(int fd, void *buf, size_t count, size_t buflen);
long __recvfrom_chk_fake(int fd, void *buf, size_t len, size_t buflen, int flags, void *from, int *fromlen);
int __snprintf_chk_fake(char *s, size_t n, int flag, size_t slen, const char *fmt, ...);
int __sprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, ...);
int __vsprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, va_list va);
char *__strncpy_chk2_fake(char *dst, const char *src, size_t n, size_t dstlen, size_t srclen);
char *__strrchr_chk_fake(const char *s, int c, size_t slen);

int __system_property_get_fake(const char *name, char *value);
size_t __ctype_get_mb_cur_max_fake(void);
void __google_potentially_blocking_region_begin_fake(void);
void __google_potentially_blocking_region_end_fake(void);
extern const char *bionic_ctype;
int gettid_fake(void);
void android_set_abort_message_fake(const char *msg);
int __register_atfork_fake(void);
void __assert2_fake(const char *file, int line, const char *func, const char *expr);

int __fpclassifyd_fake(double x);
int __isnanf_fake(float x);

typedef struct { size_t v[10]; } bionic_mallinfo;
bionic_mallinfo mallinfo_fake(void);

char *basename_fake(const char *path);
int strerror_r_fake(int err, char *buf, size_t len);
int system_fake(const char *cmd);
FILE *popen_fake(const char *cmd, const char *mode);
int pclose_fake(FILE *f);

int getaddrinfo_fake(const char *node, const char *service, const void *hints, void **res);
void freeaddrinfo_fake(void *res);

void fake_tls_install(void);

FILE *fopen_fake(const char *path, const char *mode);
int open_fake(const char *path, int flags, ...);
void proc_files_init(const char *install_root);

#endif
