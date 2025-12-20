#define _GNU_SOURCE
#include "list.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>

/* ---------- counters ---------- */

static long inc_iters = 0;
static long dec_iters = 0;
static long eq_iters  = 0;
static long swap_cnt  = 0;

static pthread_mutex_t counters_lock = PTHREAD_MUTEX_INITIALIZER;

static void inc_counter(long *c)
{
    pthread_mutex_lock(&counters_lock);
    ++(*c);
    pthread_mutex_unlock(&counters_lock);
}

/* ---------- readers ---------- */

void *thread_inc(void *arg)
{
    Storage *s = arg;

    for (;;) {
        Node *prev = s->head;

        pthread_mutex_lock(&prev->lock);
        Node *cur = prev->next;
        if (!cur) {
            pthread_mutex_unlock(&prev->lock);
            inc_counter(&inc_iters);
            continue;
        }

        pthread_mutex_lock(&cur->lock);
        pthread_mutex_unlock(&prev->lock);

        while (cur->next) {
            Node *next = cur->next;
            pthread_mutex_lock(&next->lock);

            if (strlen(cur->value) < strlen(next->value)) {
                /* counted */
            }

            pthread_mutex_unlock(&cur->lock);
            cur = next;
        }

        pthread_mutex_unlock(&cur->lock);
        inc_counter(&inc_iters);
    }
}

void *thread_dec(void *arg)
{
    Storage *s = arg;

    for (;;) {
        Node *prev = s->head;

        pthread_mutex_lock(&prev->lock);
        Node *cur = prev->next;
        if (!cur) {
            pthread_mutex_unlock(&prev->lock);
            inc_counter(&dec_iters);
            continue;
        }

        pthread_mutex_lock(&cur->lock);
        pthread_mutex_unlock(&prev->lock);

        while (cur->next) {
            Node *next = cur->next;
            pthread_mutex_lock(&next->lock);

            if (strlen(cur->value) > strlen(next->value)) {
            }

            pthread_mutex_unlock(&cur->lock);
            cur = next;
        }

        pthread_mutex_unlock(&cur->lock);
        inc_counter(&dec_iters);
    }
}

void *thread_eq(void *arg)
{
    Storage *s = arg;

    for (;;) {
        Node *prev = s->head;

        pthread_mutex_lock(&prev->lock);
        Node *cur = prev->next;
        if (!cur) {
            pthread_mutex_unlock(&prev->lock);
            inc_counter(&eq_iters);
            continue;
        }

        pthread_mutex_lock(&cur->lock);
        pthread_mutex_unlock(&prev->lock);

        while (cur->next) {
            Node *next = cur->next;
            pthread_mutex_lock(&next->lock);

            if (strlen(cur->value) == strlen(next->value)) {
            }

            pthread_mutex_unlock(&cur->lock);
            cur = next;
        }

        pthread_mutex_unlock(&cur->lock);
        inc_counter(&eq_iters);
    }
}

/* ---------- swapper ---------- */

void *thread_swap(void *arg)
{
    Storage *s = arg;
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)pthread_self();

    for (;;) {
        if (s->count < 2)
            continue;

        int index = rand_r(&seed) % s->count;

        Node *prev = s->head;
        pthread_mutex_lock(&prev->lock);

        Node *a = prev->next;
        if (!a) {
            pthread_mutex_unlock(&prev->lock);
            continue;
        }
        pthread_mutex_lock(&a->lock);

        for (int i = 0; i < index && a->next; ++i) {
            Node *next = a->next;
            pthread_mutex_lock(&next->lock);

            pthread_mutex_unlock(&prev->lock);
            prev = a;
            a = next;
        }

        Node *b = a->next;
        if (!b) {
            pthread_mutex_unlock(&a->lock);
            pthread_mutex_unlock(&prev->lock);
            continue;
        }
        pthread_mutex_lock(&b->lock);

        /* safe swap */
        if (prev->next == a && a->next == b) {
            prev->next = b;
            a->next = b->next;
            b->next = a;
            inc_counter(&swap_cnt);
        }
        pthread_mutex_unlock(&b->lock);
        pthread_mutex_unlock(&a->lock);
        pthread_mutex_unlock(&prev->lock);
    }
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    int n = 1000;
    if (argc > 1)
        n = atoi(argv[1]);

    Storage s;
    storage_init(&s, n);

    pthread_t r1, r2, r3, sw1, sw2, sw3;

    pthread_create(&r1, NULL, thread_inc, &s);
    pthread_create(&r2, NULL, thread_dec, &s);
    pthread_create(&r3, NULL, thread_eq,  &s);

    pthread_create(&sw1, NULL, thread_swap, &s);
    pthread_create(&sw2, NULL, thread_swap, &s);
    pthread_create(&sw3, NULL, thread_swap, &s);

    for (;;) {
        sleep(2);
        pthread_mutex_lock(&counters_lock);
        printf("[mutex] iters: inc=%ld dec=%ld eq=%ld | swaps=%ld\n",
               inc_iters, dec_iters, eq_iters, swap_cnt);
        pthread_mutex_unlock(&counters_lock);
    }

    storage_destroy(&s);
    return 0;
}
