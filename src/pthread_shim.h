#ifndef __PTHREAD_SHIM_H__
#define __PTHREAD_SHIM_H__

#include <stdint.h>
#include <time.h>

int b_mutexattr_init(int *attr);
int b_mutexattr_settype(int *attr, int type);
int b_mutex_init(void *m, const int *attr);
int b_mutex_destroy(void *m);
int b_mutex_lock(void *m);
int b_mutex_trylock(void *m);
int b_mutex_unlock(void *m);

int b_cond_init(void *c, const void *attr);
int b_cond_destroy(void *c);
int b_cond_signal(void *c);
int b_cond_broadcast(void *c);
int b_cond_wait(void *c, void *m);
int b_cond_timedwait(void *c, void *m, const struct timespec *abstime);

int b_sem_init(void *s, int pshared, unsigned int value);
int b_sem_destroy(void *s);
int b_sem_post(void *s);
int b_sem_wait(void *s);
int b_sem_trywait(void *s);
int b_sem_timedwait(void *s, const struct timespec *abstime);
int b_sem_getvalue(void *s, int *val);

int b_pthread_create(unsigned long *thread, const void *attr,
                     void *(*entry)(void *), void *arg);
int b_pthread_attr_getstacksize(const void *attr, unsigned long *size);

#endif
