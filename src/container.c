#include "gitsight/container.h"
#include <stdlib.h>
#include <string.h>

#define VECTOR_INIT_CAPACITY 64

void vector_init(Vector *v) {
    v->items = malloc(sizeof(void *) * VECTOR_INIT_CAPACITY);
    v->count = 0;
    v->capacity = v->items ? VECTOR_INIT_CAPACITY : 0;
}

int vector_push(Vector *v, void *item) {
    if (v->count >= v->capacity) {
        size_t new_cap = v->capacity ? v->capacity * 2 : VECTOR_INIT_CAPACITY;
        void **new_items = realloc(v->items, sizeof(void *) * new_cap);
        if (!new_items) return -1;
        v->items = new_items;
        v->capacity = new_cap;
    }
    v->items[v->count++] = item;
    return 0;
}

void *vector_get(const Vector *v, size_t index) {
    if (index >= v->count) return NULL;
    return v->items[index];
}

size_t vector_len(const Vector *v) {
    return v->count;
}

void vector_remove_at(Vector *v, size_t index, void (*free_item)(void *)) {
    if (index >= v->count) return;
    if (free_item) free_item(v->items[index]);
    if (index + 1 < v->count)
        memmove(v->items + index, v->items + index + 1,
                sizeof(void *) * (v->count - index - 1));
    v->count--;
}

void vector_free(Vector *v, void (*free_item)(void *)) {
    if (free_item) {
        for (size_t i = 0; i < v->count; i++) {
            free_item(v->items[i]);
        }
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->capacity = 0;
}
