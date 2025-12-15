#ifndef UTHREAD_H
#define UTHREAD_H

#include <ucontext.h>
#include <stddef.h>

#define UTHREAD_STACK_SIZE (64 * 1024)

typedef enum {
    UTHREAD_READY,
    UTHREAD_RUNNING,
    UTHREAD_FINISHED
} uthread_state_t;

typedef struct uthread {
    ucontext_t ctx;
    void *stack;

    void *(*fn)(void *);
    void *arg;
    void *retval;

    uthread_state_t state;
    struct uthread *next;
} uthread_t;

/* API */
int  uthread_create(uthread_t *t, void *(*fn)(void *), void *arg);
void uthread_yield(void);
int  uthread_join(uthread_t *t, void **retval);

#endif
