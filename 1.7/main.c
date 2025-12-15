#include <stdio.h>
#include "uthread.h"

void *worker(void *arg)
{
    int x = (int)(long)arg;
    for (int i = 0; i < 3; i++) {
        printf("thread %d: %d\n", x, i);
        uthread_yield();
    }
    return (void *)(long)(x * 10);
}

int main(void)
{
    uthread_t t1, t2;
    void *r1, *r2;

    uthread_create(&t1, worker, (void *)1);
    uthread_create(&t2, worker, (void *)2);

    uthread_join(&t1, &r1);
    uthread_join(&t2, &r2);

    printf("ret1 = %ld\n", (long)r1);
    printf("ret2 = %ld\n", (long)r2);
    return 0;
}
