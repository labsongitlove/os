#define _GNU_SOURCE
#include "threadcore.h"
#include <errno.h>
#include <linux/futex.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* ---- futex helpers ---- */

static int futex_wait(atomic_int *addr, int expected)
{
    return syscall(SYS_futex, addr, FUTEX_WAIT, expected, NULL, NULL, 0);
}

static int futex_wake(atomic_int *addr, int n)
{
    return syscall(SYS_futex, addr, FUTEX_WAKE, n, NULL, NULL, 0);
}

/* ---- reaper: global queue of finished detached threads ---- */

static _Atomic(tc_thread_t*) g_reap_head = NULL;
static atomic_int g_reap_pending = 0;

/* started once */
static atomic_int g_reaper_started = 0;
static void *g_reaper_stack = NULL;

static void tc_try_cleanup(tc_thread_t *t)
{
    if (!t) return;

    /* run exactly once */
    if (atomic_exchange_explicit(&t->cleaned, 1, memory_order_acq_rel))
        return;

    if (t->stack) {
        munmap(t->stack, TC_STACKSIZE);
        t->stack = NULL;
    }
}

static void tc_enqueue_reap(tc_thread_t *t)
{
    /* push to lock-free stack */
    tc_thread_t *old;
    do {
        old = atomic_load_explicit(&g_reap_head, memory_order_acquire);
        t->reap_next = old;
    } while (!atomic_compare_exchange_weak_explicit(
        &g_reap_head, &old, t,
        memory_order_release, memory_order_relaxed
    ));

    atomic_fetch_add_explicit(&g_reap_pending, 1, memory_order_acq_rel);
    futex_wake(&g_reap_pending, 1);
}

static int reaper_entry(void *p)
{
    (void)p;

    for (;;) {
        /* wait until something is pending */
        while (atomic_load_explicit(&g_reap_pending, memory_order_acquire) == 0) {
            int r = futex_wait(&g_reap_pending, 0);
            if (r == -1 && errno != EINTR && errno != EAGAIN) {
                /* on unexpected error just continue polling */
            }
        }

        /* grab whole list */
        tc_thread_t *list = atomic_exchange_explicit(&g_reap_head, NULL, memory_order_acq_rel);
        if (!list)
            continue;

        /* we don't know exact count in list if multiple pushes raced,
           so we just decrement once per node processed */
        tc_thread_t *cur = list;
        while (cur) {
            tc_thread_t *next = cur->reap_next;
            cur->reap_next = NULL;

            tc_try_cleanup(cur);
            atomic_fetch_sub_explicit(&g_reap_pending, 1, memory_order_acq_rel);

            cur = next;
        }
    }

    return 0;
}

static void ensure_reaper_started(void)
{
    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(
            &g_reaper_started, &expected, 1,
            memory_order_acq_rel, memory_order_acquire))
        return;

    /* allocate stack for reaper thread */
    g_reaper_stack = mmap(NULL, TC_STACKSIZE,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS,
                          -1, 0);
    if (g_reaper_stack == MAP_FAILED) {
        g_reaper_stack = NULL;
        /* if we fail, revert "started" and fall back to leaks rather than crash */
        atomic_store_explicit(&g_reaper_started, 0, memory_order_release);
        return;
    }

    void *stack_top = (char*)g_reaper_stack + TC_STACKSIZE;
    stack_top = (void *)((uintptr_t)stack_top & ~0xF);

    int flags =
        CLONE_VM |
        CLONE_FS |
        CLONE_FILES |
        CLONE_SIGHAND |
        CLONE_THREAD;

    if (clone(reaper_entry, stack_top, flags, NULL) == -1) {
        munmap(g_reaper_stack, TC_STACKSIZE);
        g_reaper_stack = NULL;
        atomic_store_explicit(&g_reaper_started, 0, memory_order_release);
        return;
    }
}

/* ---- worker entry ---- */

static int thread_entry(void *p)
{
    tc_thread_t *t = p;

    t->retval = t->fn(t->arg);

    atomic_store_explicit(&t->done, 1, memory_order_release);
    futex_wake(&t->done, 1);

/* if detached -> reaper will free the stack (NOT this thread!) */
    if (atomic_load_explicit(&t->detached, memory_order_acquire)) {
        /* if reaper isn't available, cleanup can't be safe here */
        if (atomic_load_explicit(&g_reaper_started, memory_order_acquire))
            tc_enqueue_reap(t);
    }

    return 0;
}

/* ---- create ---- */

int tc_spawn(tc_thread_t *t, void *(*fn)(void *), void *arg)
{
    if (!t || !fn) {
        errno = EINVAL;
        return -1;
    }

    memset(t, 0, sizeof(*t));
    atomic_init(&t->done, 0);
    atomic_init(&t->detached, 0);
    atomic_init(&t->cleaned, 0);
    t->reap_next = NULL;

    t->fn  = fn;
    t->arg = arg;

    t->stack = mmap(NULL, TC_STACKSIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS,
                    -1, 0);
    if (t->stack == MAP_FAILED) {
        t->stack = NULL;
        return -1;
    }

    void *stack_top = (char *)t->stack + TC_STACKSIZE;
    stack_top = (void *)((uintptr_t)stack_top & ~0xF);

    int flags =
        CLONE_VM |
        CLONE_FS |
        CLONE_FILES |
        CLONE_SIGHAND |
        CLONE_THREAD;

    if (clone(thread_entry, stack_top, flags, t) == -1) {
        int e = errno;
        munmap(t->stack, TC_STACKSIZE);
        t->stack = NULL;
        errno = e;
        return -1;
    }

    return 0;
}

/* ---- join ---- */

int tc_join(tc_thread_t *t, void **out)
{
    if (!t || atomic_load_explicit(&t->detached, memory_order_acquire)) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        if (atomic_load_explicit(&t->done, memory_order_acquire))
            break;
        int r = futex_wait(&t->done, 0);
        if (r == -1 && errno != EINTR && errno != EAGAIN) {
            return -1;
        }
    }

    if (out)
        *out = t->retval;

    /* joiner frees stack safely */
    tc_try_cleanup(t);
    return 0;
}

/* ---- detach ---- */

int tc_detach(tc_thread_t *t)
{
    if (!t) {
        errno = EINVAL;
        return -1;
    }

    /* mark detached */
    if (atomic_exchange_explicit(&t->detached, 1, memory_order_acq_rel))
        return 0;

    /* start reaper so detached stacks are always freed later */
    ensure_reaper_started();

    /* if already done -> can free right now (caller is not on target stack) */
    if (atomic_load_explicit(&t->done, memory_order_acquire)) {
        tc_try_cleanup(t);
    } else {
        /* not done yet -> reaper will free after completion */
        /* (enqueue happens in thread_entry on completion) */
    }

    return 0;
}
