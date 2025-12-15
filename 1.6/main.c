#include "threadcore.h"
#include <stdio.h>
#include <sched.h>

static void *task(void *x)
{
    long id = (long)x;
    if (id % 5 == 0) sched_yield();
    printf("thread %ld running\n", id);
    return (void*)(id + 123);
}

int main(void)
{
    const int N = 50;
    tc_thread_t th[N];

    for (long i = 0; i < N; ++i)
        tc_spawn(&th[i], task, (void*)i);

    for (int i = 0; i < N; ++i) {
        if (i < N/2) {
            void *r;
            tc_join(&th[i], &r);
        } else {
            tc_detach(&th[i]);
        }
    }

    return 0;
}
