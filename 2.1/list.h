#ifndef LIST_H
#define LIST_H

#define _GNU_SOURCE
#include <pthread.h>

typedef enum {
    LOCK_MUTEX = 1,
    LOCK_SPIN  = 2,
    LOCK_RW    = 3
} LockKind;

typedef struct _Node {
    char value[100];
    int  strlen;
    struct _Node *next;

    LockKind kind;
    union {
        pthread_mutex_t    mtx;
        pthread_spinlock_t spn;
        pthread_rwlock_t   rw;
    } sync;
} Node;

typedef struct _Storage {
    Node *first;     /* sentinel */
    int   size;      /* number of real elements (not counting sentinel) */
    LockKind kind;
} Storage;

Storage *list_init(int n, LockKind kind);
void list_destroy(Storage *list);

#endif
