#include "list.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------- helpers ---------- */

static Node *node_create(const char *s)
{
    Node *n = malloc(sizeof(Node));
    if (!n) {
        perror("malloc node");
        exit(1);
    }
    strcpy(n->value, s);
    n->next = NULL;

    if (pthread_spin_init(&n->lock, PTHREAD_PROCESS_PRIVATE) != 0) {
        perror("pthread_spin_init");
        exit(1);
    }
    return n;
}

/* ---------- storage ---------- */

void storage_init(Storage *s, int n)
{
    s->head = malloc(sizeof(Node));
    if (!s->head) {
        perror("malloc head");
        exit(1);
    }

    s->head->value[0] = '\0';   /* sentinel */
    s->head->next = NULL;

    if (pthread_spin_init(&s->head->lock, PTHREAD_PROCESS_PRIVATE) != 0) {
        perror("pthread_spin_init head");
        exit(1);
    }

    s->count = n;

    Node *prev = s->head;
    for (int i = 0; i < n; ++i) {
        char buf[100];
        snprintf(buf, sizeof(buf), "str_%d", i);
        Node *node = node_create(buf);
        prev->next = node;
        prev = node;
    }
}

void storage_destroy(Storage *s)
{
    Node *cur = s->head;
    while (cur) {
        Node *next = cur->next;
        pthread_spin_destroy(&cur->lock);
        free(cur);
        cur = next;
    }
}
