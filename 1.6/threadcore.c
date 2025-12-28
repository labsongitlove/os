#define _GNU_SOURCE
#include "threadcore.h"
//cheak plz
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

static int futex_wake(atomic_int *addr)
{
    return syscall(SYS_futex, addr, FUTEX_WAKE, 1, NULL, NULL, 0);
}

static int thread_entry(void *p)
{
    tc_thread_t *t = p;

    t->retval = t->fn(t->arg);

    atomic_store(&t->done, 1);
    futex_wake(&t->done);

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

int tc_join(tc_thread_t *t, void **out)
{
    if (!t || atomic_load(&t->detached)) {
        errno = EINVAL;
        return -1;
    }

    while (!atomic_load(&t->done))
        futex_wait(&t->done, 0);

    if (out)
        *out = t->retval;

    if (t->stack) {
        munmap(t->stack, TC_STACKSIZE);
        t->stack = NULL;
    }

    return 0;
}

int tc_detach(tc_thread_t *t)
{
    if (!t) {
        errno = EINVAL;
        return -1;
    }

    if (atomic_exchange(&t->detached, 1))
        return 0;

    /* если поток ещё работает — ждём */
    while (!atomic_load(&t->done))
        futex_wait(&t->done, 0);

    if (t->stack) {
        munmap(t->stack, TC_STACKSIZE);
        t->stack = NULL;
    }

    return 0;
}
