#ifndef GITSIGHT_CONTAINER_H
#define GITSIGHT_CONTAINER_H

#include <stddef.h>

typedef struct {
    void **items;
    size_t count;
    size_t capacity;
} Vector;

void     vector_init(Vector *v);
int      vector_push(Vector *v, void *item);
void    *vector_get(const Vector *v, size_t index);
size_t   vector_len(const Vector *v);
void     vector_free(Vector *v, void (*free_item)(void *));
void     vector_remove_at(Vector *v, size_t index, void (*free_item)(void *));

#endif
