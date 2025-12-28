#define _GNU_SOURCE
#include "list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- node lock init/destroy ---------- */

static void node_lock_init(Node *n, LockKind kind)
{
    n->kind = kind;
    switch (kind) {
        case LOCK_MUTEX:
            pthread_mutex_init(&n->sync.mtx, NULL);
            break;
        case LOCK_SPIN:
            pthread_spin_init(&n->sync.spn, PTHREAD_PROCESS_PRIVATE);
            break;
        case LOCK_RW:
            pthread_rwlock_init(&n->sync.rw, NULL);
            break;
        default:
            fprintf(stderr, "Unknown lock kind\n");
            exit(1);
    }
}

static void node_lock_destroy(Node *n)
{
    switch (n->kind) {
        case LOCK_MUTEX:
            pthread_mutex_destroy(&n->sync.mtx);
            break;
        case LOCK_SPIN:
            pthread_spin_destroy(&n->sync.spn);
            break;
        case LOCK_RW:
            pthread_rwlock_destroy(&n->sync.rw);
            break;
        default:
            break;
    }
}

/* ---------- random string generator ---------- */
/* Делает заметно “живую” статистику cnt_* (а не доминацию == как у "str_%d"). */
static void gen_random_str(char out[100], int *out_len)
{
    /* длина 1..99 */
    int len = (rand() % 99) + 1;
    for (int i = 0; i < len; ++i) {
        out[i] = (char)('a' + (rand() % 26));
    }
    out[len] = '\0';
    *out_len = len;
}

static Node *node_create_random(LockKind kind)
{
    Node *n = (Node *)malloc(sizeof(Node));
    if (!n) {
        perror("malloc node");
        exit(1);
    }
    gen_random_str(n->value, &n->strlen);
    n->next = NULL;
    node_lock_init(n, kind);
    return n;
}

/* ---------- list init/destroy ---------- */

Storage *list_init(int n, LockKind kind)
{
    Storage *list = (Storage *)malloc(sizeof(Storage));
    if (!list) {
        perror("malloc storage");
        exit(1);
    }
    list->size = n;
    list->kind = kind;

    /* sentinel */
    list->first = (Node *)malloc(sizeof(Node));
    if (!list->first) {
        perror("malloc sentinel");
        exit(1);
    }
    list->first->value[0] = '\0';
    list->first->strlen = 0;
    list->first->next = NULL;
    node_lock_init(list->first, kind);

    Node *prev = list->first;
    for (int i = 0; i < n; ++i) {
        Node *x = node_create_random(kind);
        prev->next = x;
        prev = x;
    }

    return list;
}

void list_destroy(Storage *list)
{
    if (!list) return;

    Node *cur = list->first;
    while (cur) {
        Node *next = cur->next;
        node_lock_destroy(cur);
        free(cur);
        cur = next;
    }
    free(list);
}
