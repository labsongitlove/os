#include "threadcore.h"
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

void *task(void *x)
{
    long id = (long)x;

    if (id % 3 == 0)
        sleep(1);
    else
        sched_yield();

    printf("thread %ld done\n", id);
    return (void *)(id + 123);
}
int main(void)
{
    const int N = 200;
    tc_thread_t th[N];

    for (long i = 0; i < N; ++i) {
      tc_spawn(&th[i], task, (void *)i);
        if (i < 100)
          tc_detach(&th[i]);     // detach сразу
        else{
          void *rv = NULL;
          tc_join(&th[i], &rv);  
        }
    }

    sleep(3);
    return 0;
}
