#include "list.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
//cheak plz
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
    pthread_mutex_init(&n->lock, NULL);
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
    pthread_mutex_init(&s->head->lock, NULL);

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
        pthread_mutex_destroy(&cur->lock);
        free(cur);
        cur = next;
    }
}
