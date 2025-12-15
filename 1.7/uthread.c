#define _XOPEN_SOURCE 700
#include "uthread.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ---------- scheduler state ---------- */

static ucontext_t main_ctx;
static uthread_t *current = NULL;
static uthread_t *ready_queue = NULL;
static int initialized = 0;

/* ---------- queue helpers (FIFO) ---------- */

static void enqueue(uthread_t *t)
{
    t->next = NULL;
    if (!ready_queue) {
        ready_queue = t;
    } else {
        uthread_t *p = ready_queue;
        while (p->next)
            p = p->next;
        p->next = t;
    }
}

static uthread_t *dequeue(void)
{
    uthread_t *t = ready_queue;
    if (t)
        ready_queue = t->next;
    return t;
}

/* ---------- thread entry wrapper ---------- */

static void uthread_entry(void)
{
    current->retval = current->fn(current->arg);
    current->state = UTHREAD_FINISHED;
    uthread_yield();   /* never returns */
}

/* ---------- runtime init ---------- */

static void uthread_init(void)
{
    getcontext(&main_ctx);
    initialized = 1;
}

/* ---------- scheduler ---------- */

static void schedule(void)
{
    uthread_t *prev = current;
    uthread_t *next = dequeue();

    if (!next) {
        if (prev)
            swapcontext(&prev->ctx, &main_ctx);
        return;
    }

    if (prev && prev->state == UTHREAD_RUNNING) {
        prev->state = UTHREAD_READY;
        enqueue(prev);
    }

    next->state = UTHREAD_RUNNING;
    current = next;

    if (prev)
        swapcontext(&prev->ctx, &next->ctx);
    else
        swapcontext(&main_ctx, &next->ctx);
}

/* ---------- API ---------- */

int uthread_create(uthread_t *t, void *(*fn)(void *), void *arg)
{
    if (!t || !fn) {
        errno = EINVAL;
        return -1;
    }

    if (!initialized)
        uthread_init();

    memset(t, 0, sizeof(*t));

    t->fn = fn;
    t->arg = arg;
    t->state = UTHREAD_READY;

    t->stack = malloc(UTHREAD_STACK_SIZE);
    if (!t->stack)
        return -1;

    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp   = t->stack;
    t->ctx.uc_stack.ss_size = UTHREAD_STACK_SIZE;
    t->ctx.uc_link = &main_ctx;

    makecontext(&t->ctx, uthread_entry, 0);

    enqueue(t);
    return 0;
}

void uthread_yield(void)
{
    schedule();
}

int uthread_join(uthread_t *t, void **retval)
{
    if (!t) {
        errno = EINVAL;
        return -1;
    }

    while (t->state != UTHREAD_FINISHED)
        uthread_yield();

    if (retval)
        *retval = t->retval;

    free(t->stack);
    t->stack = NULL;

    return 0;
}
