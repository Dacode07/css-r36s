#define _GNU_SOURCE

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "pthread_shim.h"
#include "util.h"

static int sys_futex(volatile uint32_t *uaddr, int op, uint32_t val,
                     const struct timespec *timeout) {
  return (int)syscall(SYS_futex, uaddr, op, val, timeout, NULL, 0);
}

static int sys_gettid(void) {
  return (int)syscall(SYS_gettid);
}

typedef struct {
  volatile uint32_t init_word;
  volatile uint32_t lock;
  volatile uint32_t owner;
  volatile uint32_t count;
  uint32_t pad[6];
} bmutex;

_Static_assert(sizeof(bmutex) == 40, "bmutex must match bionic pthread_mutex_t");

int b_mutexattr_init(int *attr) {
  if (attr) *attr = 0;
  return 0;
}

int b_mutexattr_settype(int *attr, int type) {
  if (attr) *attr = type;
  return 0;
}

int b_mutex_init(void *mp, const int *attr) {
  (void)attr;
  bmutex *m = mp;
  m->lock = 0;
  m->owner = 0;
  m->count = 0;
  return 0;
}

int b_mutex_destroy(void *mp) {
  bmutex *m = mp;
  m->lock = 0;
  m->owner = 0;
  m->count = 0;
  return 0;
}

int b_mutex_lock(void *mp) {
  bmutex *m = mp;
  const uint32_t tid = (uint32_t)sys_gettid();

  if (m->lock != 0 && m->owner == tid) {
    m->count++;
    return 0;
  }

  uint32_t c = __sync_val_compare_and_swap(&m->lock, 0, 1);
  if (c != 0) {

    do {
      if (c == 2 || __sync_val_compare_and_swap(&m->lock, 1, 2) != 0)
        sys_futex(&m->lock, FUTEX_WAIT_PRIVATE, 2, NULL);
    } while ((c = __sync_val_compare_and_swap(&m->lock, 0, 2)) != 0);
  }

  m->owner = tid;
  m->count = 1;
  return 0;
}

int b_mutex_trylock(void *mp) {
  bmutex *m = mp;
  const uint32_t tid = (uint32_t)sys_gettid();

  if (m->lock != 0 && m->owner == tid) {
    m->count++;
    return 0;
  }
  if (__sync_val_compare_and_swap(&m->lock, 0, 1) == 0) {
    m->owner = tid;
    m->count = 1;
    return 0;
  }
  return EBUSY;
}

int b_mutex_unlock(void *mp) {
  bmutex *m = mp;
  if (m->count > 1) {
    m->count--;
    return 0;
  }
  m->owner = 0;
  m->count = 0;
  if (__sync_lock_test_and_set(&m->lock, 0) == 2)
    sys_futex(&m->lock, FUTEX_WAKE_PRIVATE, 1, NULL);
  return 0;
}

typedef struct {
  volatile uint32_t seq;
  uint32_t pad[11];
} bcond;

_Static_assert(sizeof(bcond) == 48, "bcond must match bionic pthread_cond_t");

int b_cond_init(void *cp, const void *attr) {
  (void)attr;
  bcond *c = cp;
  c->seq = 0;
  return 0;
}

int b_cond_destroy(void *cp) {
  (void)cp;
  return 0;
}

int b_cond_signal(void *cp) {
  bcond *c = cp;
  __sync_fetch_and_add(&c->seq, 1);
  sys_futex(&c->seq, FUTEX_WAKE_PRIVATE, 1, NULL);
  return 0;
}

int b_cond_broadcast(void *cp) {
  bcond *c = cp;
  __sync_fetch_and_add(&c->seq, 1);
  sys_futex(&c->seq, FUTEX_WAKE_PRIVATE, INT_MAX, NULL);
  return 0;
}

int b_cond_wait(void *cp, void *mp) {
  bcond *c = cp;
  const uint32_t seq = c->seq;
  b_mutex_unlock(mp);
  sys_futex(&c->seq, FUTEX_WAIT_PRIVATE, seq, NULL);
  b_mutex_lock(mp);
  return 0;
}

static int64_t abstime_to_rel_ns(const struct timespec *t) {
  struct timespec rt, mt;
  clock_gettime(CLOCK_REALTIME, &rt);
  clock_gettime(CLOCK_MONOTONIC, &mt);
  const int64_t now_rt = (int64_t)rt.tv_sec * 1000000000ll + rt.tv_nsec;
  const int64_t now_mono = (int64_t)mt.tv_sec * 1000000000ll + mt.tv_nsec;
  const int64_t abs_ns = (int64_t)t->tv_sec * 1000000000ll + t->tv_nsec;
  const int64_t day = 24ll * 3600 * 1000000000ll;

  int64_t rel = abs_ns - now_rt;
  if (rel < -1000000ll || rel >= day) {
    const int64_t rel_mono = abs_ns - now_mono;
    if (rel_mono >= -1000000ll && rel_mono < day)
      rel = rel_mono;
  }
  return rel < 0 ? 0 : rel;
}

int b_cond_timedwait(void *cp, void *mp, const struct timespec *abstime) {
  bcond *c = cp;
  if (!abstime)
    return b_cond_wait(cp, mp);

  const int64_t rel = abstime_to_rel_ns(abstime);
  struct timespec ts = { .tv_sec = rel / 1000000000ll, .tv_nsec = rel % 1000000000ll };

  const uint32_t seq = c->seq;
  b_mutex_unlock(mp);
  const int r = sys_futex(&c->seq, FUTEX_WAIT_PRIVATE, seq, &ts);
  const int timed_out = (r < 0 && errno == ETIMEDOUT);
  b_mutex_lock(mp);
  return timed_out ? ETIMEDOUT : 0;
}

typedef struct {
  volatile uint32_t count;
  uint32_t pad[3];
} bsem;

_Static_assert(sizeof(bsem) == 16, "bsem must match bionic sem_t");

int b_sem_init(void *sp, int pshared, unsigned int value) {
  (void)pshared;
  bsem *s = sp;
  s->count = value;
  return 0;
}

int b_sem_destroy(void *sp) {
  (void)sp;
  return 0;
}

int b_sem_post(void *sp) {
  bsem *s = sp;
  __sync_fetch_and_add(&s->count, 1);
  sys_futex(&s->count, FUTEX_WAKE_PRIVATE, 1, NULL);
  return 0;
}

int b_sem_trywait(void *sp) {
  bsem *s = sp;
  for (;;) {
    const uint32_t c = s->count;
    if (c == 0) {
      errno = EAGAIN;
      return -1;
    }
    if (__sync_bool_compare_and_swap(&s->count, c, c - 1))
      return 0;
  }
}

int b_sem_wait(void *sp) {
  bsem *s = sp;
  for (;;) {
    if (b_sem_trywait(sp) == 0)
      return 0;
    sys_futex(&s->count, FUTEX_WAIT_PRIVATE, 0, NULL);
  }
}

int b_sem_timedwait(void *sp, const struct timespec *abstime) {
  bsem *s = sp;
  if (!abstime)
    return b_sem_wait(sp);
  for (;;) {
    if (b_sem_trywait(sp) == 0)
      return 0;
    const int64_t rel = abstime_to_rel_ns(abstime);
    if (rel <= 0) {
      errno = ETIMEDOUT;
      return -1;
    }
    struct timespec ts = { .tv_sec = rel / 1000000000ll, .tv_nsec = rel % 1000000000ll };
    const int r = sys_futex(&s->count, FUTEX_WAIT_PRIVATE, 0, &ts);
    if (r < 0 && errno == ETIMEDOUT) {

      if (b_sem_trywait(sp) == 0)
        return 0;
      errno = ETIMEDOUT;
      return -1;
    }
  }
}

int b_sem_getvalue(void *sp, int *val) {
  bsem *s = sp;
  if (val)
    *val = (int)s->count;
  return 0;
}

typedef struct { void *(*fn)(void *); void *arg; } ThreadTramp;

static void *thread_entry_tramp(void *p) {
  ThreadTramp t = *(ThreadTramp *)p;
  free(p);
  char loc[96];
  resolve_code_addr((uintptr_t)t.fn, loc, sizeof(loc));
  tracePrintf("engine thread: tid=%ld entry=%s\n",
              (long)syscall(SYS_gettid), loc);
  return t.fn(t.arg);
}

int b_pthread_create(unsigned long *thread, const void *attr,
                     void *(*entry)(void *), void *arg) {
  (void)attr;
  pthread_t t = 0;
  ThreadTramp *tr = malloc(sizeof(*tr));
  int ret;
  if (tr) {
    tr->fn = entry; tr->arg = arg;
    ret = pthread_create(&t, NULL, thread_entry_tramp, tr);
    if (ret != 0) free(tr);
  } else {
    ret = pthread_create(&t, NULL, entry, arg);
  }
  if (ret == 0 && thread)
    *thread = (unsigned long)t;
  return ret;
}

int b_pthread_attr_getstacksize(const void *attr, unsigned long *size) {
  (void)attr;
  if (size)
    *size = 1024 * 1024;
  return 0;
}
