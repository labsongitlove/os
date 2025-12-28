#ifndef THREADCORE_H
#define THREADCORE_H

#include <stdatomic.h>
#include <stddef.h>

#define TC_STACKSIZE (1024 * 1024)

typedef struct tc_thread {
    atomic_int done;        /* 0 — running, 1 — finished */
    atomic_int detached;    /* 0 — joinable, 1 — detached */
    atomic_int cleaned; 

    void *stack;
    void *(*fn)(void *);
    void *arg;
    void *retval;
} tc_thread_t;

int tc_spawn(tc_thread_t *t, void *(*fn)(void *), void *arg);
int tc_join(tc_thread_t *t, void **out);
int tc_detach(tc_thread_t *t);

#endif
