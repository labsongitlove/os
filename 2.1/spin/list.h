#ifndef LIST_H
#define LIST_H

#include <pthread.h>

typedef struct Node {
    char value[100];
    struct Node *next;
    pthread_spinlock_t lock;   // <-- spinlock вместо mutex
} Node;

typedef struct {
    Node *head;   /* sentinel */
    int count;
} Storage;

/* init / destroy */
void storage_init(Storage *s, int n);
void storage_destroy(Storage *s);

#endif
