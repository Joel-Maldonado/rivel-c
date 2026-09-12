#include "base/strmap.h"

#include "base/util.h"

void strmap_init(StrMap *m) {
    m->entries = NULL;
    m->count = 0;
    m->cap = 0;
}

void strmap_free(StrMap *m) {
    free(m->entries);
    strmap_init(m);
}

static StrMapEntry *strmap_slot(StrMapEntry *entries, size_t cap, Str key) {
    size_t i = (size_t)(str_hash(key) & (cap - 1));
    while (entries[i].used && !str_eq(entries[i].key, key)) {
        i = (i + 1) & (cap - 1);
    }
    return &entries[i];
}

static void strmap_grow(StrMap *m) {
    size_t new_cap = m->cap ? m->cap * 2 : 16;
    StrMapEntry *entries = xcalloc(new_cap, sizeof *entries);

    for (size_t i = 0; i < m->cap; i++) {
        if (m->entries[i].used) {
            *strmap_slot(entries, new_cap, m->entries[i].key) = m->entries[i];
        }
    }
    free(m->entries);
    m->entries = entries;
    m->cap = new_cap;
}

void *strmap_get(const StrMap *m, Str key) {
    StrMapEntry *e;
    if (m->cap == 0) {
        return NULL;
    }
    e = strmap_slot(m->entries, m->cap, key);
    return e->used ? e->value : NULL;
}

bool strmap_has(const StrMap *m, Str key) {
    return m->cap != 0 && strmap_slot(m->entries, m->cap, key)->used;
}

void *strmap_put(StrMap *m, Str key, void *value) {
    StrMapEntry *e;
    void *old;

    if ((m->count + 1) * 4 >= m->cap * 3) {
        strmap_grow(m);
    }
    e = strmap_slot(m->entries, m->cap, key);
    if (e->used) {
        old = e->value;
        e->value = value;
        return old;
    }
    e->used = true;
    e->key = key;
    e->value = value;
    m->count++;
    return NULL;
}
