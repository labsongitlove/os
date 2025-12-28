#define _GNU_SOURCE
#include "list.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>

/* ===================== globals like in "эталон" ===================== */

static atomic_int running = 0;

static atomic_ulong iters_incr = 0;
static atomic_ulong iters_decr = 0;
static atomic_ulong iters_comp = 0;

static atomic_ulong cnt_incr = 0;
static atomic_ulong cnt_decr = 0;
static atomic_ulong cnt_comp = 0;

static atomic_ulong swap_incr = 0;
static atomic_ulong swap_decr = 0;
static atomic_ulong swap_comp = 0;

/* ===================== node lock helpers ===================== */

static inline void node_lock_read(Node *n)
{
    switch (n->kind) {
        case LOCK_MUTEX: pthread_mutex_lock(&n->sync.mtx); break;
        case LOCK_SPIN:  pthread_spin_lock(&n->sync.spn);  break;
        case LOCK_RW:    pthread_rwlock_rdlock(&n->sync.rw); break;
        default: break;
    }
}

static inline void node_lock_write(Node *n)
{
    switch (n->kind) {
        case LOCK_MUTEX: pthread_mutex_lock(&n->sync.mtx); break;
        case LOCK_SPIN:  pthread_spin_lock(&n->sync.spn);  break;
        case LOCK_RW:    pthread_rwlock_wrlock(&n->sync.rw); break;
        default: break;
    }
}

static inline void node_unlock(Node *n)
{
    switch (n->kind) {
        case LOCK_MUTEX: pthread_mutex_unlock(&n->sync.mtx); break;
        case LOCK_SPIN:  pthread_spin_unlock(&n->sync.spn);  break;
        case LOCK_RW:    pthread_rwlock_unlock(&n->sync.rw); break;
        default: break;
    }
}

/* ===================== check threads (3 readers) ===================== */

static void *check_incr(void *arg)
{
    Storage *list = (Storage *)arg;
    if (list->size < 2) return NULL;

    while (atomic_load(&running)) {
        Node *head = list->first;
        if (!head) {
            atomic_fetch_add(&iters_incr, 1);
            continue;
        }

        node_lock_read(head);
        Node *curr = head->next;
        if (!curr) {
            atomic_fetch_add(&iters_incr, 1);
            node_unlock(head);
            continue;
        }

        node_lock_read(curr);
        node_unlock(head);

        while (curr && curr->next && atomic_load(&running)) {
            Node *next = curr->next;

            node_lock_read(next);

            if (curr->strlen < next->strlen)
                atomic_fetch_add(&cnt_incr, 1);

            node_unlock(curr);
            curr = next;
        }

        node_unlock(curr);
        atomic_fetch_add(&iters_incr, 1);
    }

    return NULL;
}

static void *check_decr(void *arg)
{
    Storage *list = (Storage *)arg;
    if (list->size < 2) return NULL;

    while (atomic_load(&running)) {
        Node *head = list->first;
        if (!head) {
            atomic_fetch_add(&iters_decr, 1);
            continue;
        }

        node_lock_read(head);
        Node *curr = head->next;
        if (!curr) {
            atomic_fetch_add(&iters_decr, 1);
            node_unlock(head);
            continue;
        }

        node_lock_read(curr);
        node_unlock(head);

        while (curr && curr->next && atomic_load(&running)) {
            Node *next = curr->next;

            node_lock_read(next);

            if (curr->strlen > next->strlen)
                atomic_fetch_add(&cnt_decr, 1);

            node_unlock(curr);
            curr = next;
        }

        node_unlock(curr);
        atomic_fetch_add(&iters_decr, 1);
    }

    return NULL;
}

static void *check_comp(void *arg)
{
    Storage *list = (Storage *)arg;
    if (list->size < 2) return NULL;

    while (atomic_load(&running)) {
        Node *head = list->first;
        if (!head) {
            atomic_fetch_add(&iters_comp, 1);
            continue;
        }

        node_lock_read(head);
        Node *curr = head->next;
        if (!curr) {
            atomic_fetch_add(&iters_comp, 1);
            node_unlock(head);
            continue;
        }

        node_lock_read(curr);
        node_unlock(head);

while (curr && curr->next && atomic_load(&running)) {
            Node *next = curr->next;

            node_lock_read(next);

            if (curr->strlen == next->strlen)
                atomic_fetch_add(&cnt_comp, 1);

            node_unlock(curr);
            curr = next;
        }

        node_unlock(curr);
        atomic_fetch_add(&iters_comp, 1);
    }

    return NULL;
}

/* ===================== swap threads (3 writers) ===================== */
/* Требование: при перестановке блокировать 3 записи: prev, left, right.
   И чтобы избежать deadlock: ближе к началу захватывать раньше.
   Здесь это обеспечено lock-coupling проходом слева направо. */

static void *do_swap_incr(void *arg)
{
    Storage *list = (Storage *)arg;
    if (list->size < 2) return NULL;

    unsigned int seed = (unsigned int)pthread_self() ^ (unsigned int)time(NULL);

    while (atomic_load(&running)) {
        int swap_ind = (int)(rand_r(&seed) % (unsigned int)(list->size - 1));

        Node *prev = list->first;
        if (!prev) break;

        node_lock_write(prev);
        Node *left = prev->next;
        if (!left) {
            node_unlock(prev);
            break;
        }

        node_lock_write(left);

        for (int i = 0; i < swap_ind; i++) {
            Node *right = left->next;
            if (!right) break;

            node_lock_write(right);
            node_unlock(prev);

            prev = left;
            left = right;
        }

        Node *right = left->next;
        if (!right) {
            node_unlock(left);
            node_unlock(prev);
            continue;
        }

        node_lock_write(right);

        /* "требуется ли": для возрастания — если нарушен порядок */
        if (left->strlen > right->strlen) {
            prev->next = right;
            left->next = right->next;
            right->next = left;
            atomic_fetch_add(&swap_incr, 1);
        }

        node_unlock(right);
        node_unlock(left);
        node_unlock(prev);
    }

    return NULL;
}

static void *do_swap_decr(void *arg)
{
    Storage *list = (Storage *)arg;
    if (list->size < 2) return NULL;

    unsigned int seed = (unsigned int)pthread_self() ^ (unsigned int)time(NULL);

    while (atomic_load(&running)) {
        int swap_ind = (int)(rand_r(&seed) % (unsigned int)(list->size - 1));

        Node *prev = list->first;
        if (!prev) break;

        node_lock_write(prev);
        Node *left = prev->next;
        if (!left) {
            node_unlock(prev);
            break;
        }

        node_lock_write(left);

        for (int i = 0; i < swap_ind; i++) {
            Node *right = left->next;
            if (!right) break;

            node_lock_write(right);
            node_unlock(prev);

            prev = left;
            left = right;
        }

        Node *right = left->next;
        if (!right) {
            node_unlock(left);
            node_unlock(prev);
            continue;
        }

        node_lock_write(right);

        /* "требуется ли": для убывания — если нарушен порядок */
        if (left->strlen < right->strlen) {
            prev->next = right;
            left->next = right->next;
            right->next = left;
            atomic_fetch_add(&swap_decr, 1);
        }

        node_unlock(right);
        node_unlock(left);
        node_unlock(prev);
    }

    return NULL;
}

static void *do_swap_comp(void *arg)
{
    Storage *list = (Storage *)arg;
    if (list->size < 2) return NULL;

    unsigned int seed = (unsigned int)pthread_self() ^ (unsigned int)time(NULL);

    while (atomic_load(&running)) {
        int swap_ind = (int)(rand_r(&seed) % (unsigned int)(list->size - 1));

        Node *prev = list->first;
        if (!prev) break;

        node_lock_write(prev);
        Node *left = prev->next;
        if (!left) {
            node_unlock(prev);
            break;
        }

        node_lock_write(left);

        for (int i = 0; i < swap_ind; i++) {
            Node *right = left->next;
            if (!right) break;

node_lock_write(right);
            node_unlock(prev);

            prev = left;
            left = right;
        }

        Node *right = left->next;
        if (!right) {
            node_unlock(left);
            node_unlock(prev);
            continue;
        }

        node_lock_write(right);

        /* "требуется ли": для "одинаковой длины" — если длины разные */
        if (left->strlen != right->strlen) {
            prev->next = right;
            left->next = right->next;
            right->next = left;
            atomic_fetch_add(&swap_comp, 1);
        }

        node_unlock(right);
        node_unlock(left);
        node_unlock(prev);
    }

    return NULL;
}

/* ===================== run helpers ===================== */

static void reset_counters(void)
{
    atomic_store(&iters_incr, 0);
    atomic_store(&iters_decr, 0);
    atomic_store(&iters_comp, 0);

    atomic_store(&cnt_incr, 0);
    atomic_store(&cnt_decr, 0);
    atomic_store(&cnt_comp, 0);

    atomic_store(&swap_incr, 0);
    atomic_store(&swap_decr, 0);
    atomic_store(&swap_comp, 0);
}

static const char *kind_name(LockKind k)
{
    switch (k) {
        case LOCK_MUTEX: return "mutex";
        case LOCK_SPIN:  return "spinlock";
        case LOCK_RW:    return "rwlock";
        default:         return "unknown";
    }
}

static void run_one(int n, LockKind kind, int seconds)
{
    reset_counters();

    Storage *list = list_init(n, kind);

    pthread_t tincr, tdecr, tcomp;
    pthread_t swincr, swdecr, swcomp;

    atomic_store(&running, 1);

    pthread_create(&tincr,  NULL, check_incr,   list);
    pthread_create(&tdecr,  NULL, check_decr,   list);
    pthread_create(&tcomp,  NULL, check_comp,   list);

    pthread_create(&swincr, NULL, do_swap_incr, list);
    pthread_create(&swdecr, NULL, do_swap_decr, list);
    pthread_create(&swcomp, NULL, do_swap_comp, list);

    sleep(seconds);
    atomic_store(&running, 0);

    pthread_join(tincr,  NULL);
    pthread_join(tdecr,  NULL);
    pthread_join(tcomp,  NULL);
    pthread_join(swincr, NULL);
    pthread_join(swdecr, NULL);
    pthread_join(swcomp, NULL);

    printf("n = %d [%s] "
           "iters: incr=%lu decr=%lu comp=%lu | "
           "cnt: incr=%lu decr=%lu comp=%lu | "
           "swaps: incr=%lu decr=%lu comp=%lu\n",
           n, kind_name(kind),
           atomic_load(&iters_incr), atomic_load(&iters_decr), atomic_load(&iters_comp),
           atomic_load(&cnt_incr),   atomic_load(&cnt_decr),   atomic_load(&cnt_comp),
           atomic_load(&swap_incr),  atomic_load(&swap_decr),  atomic_load(&swap_comp));

    list_destroy(list);
}

/* ===================== main ===================== */

int main(void)
{
    srand((unsigned int)time(NULL));

    const int seconds = 10;
    const int Ns[4] = {100, 1000, 10000, 100000};

    for (int i = 0; i < 4; ++i) {
        int n = Ns[i];

        run_one(n, LOCK_MUTEX, seconds);
        run_one(n, LOCK_SPIN,  seconds);
        run_one(n, LOCK_RW,    seconds);

        puts("------------------------------------------------------------");
        fflush(stdout);
    }

    return 0;
}
