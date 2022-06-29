#include "hashset.h"

struct hashset_t {
    hashset_hash_function_t hash;
    hashset_cmp_function_t cmp;
    uint64_t size;
    uint64_t used;
    void** table;
};

hashset_error_t hashset_create(hashset_t** hs, hashset_hash_function_t hash, hashset_cmp_function_t cmp, uint64_t initial_size)
{
    *hs = malloc(sizeof(**hs));
    if (*hs == NULL) {
        return HASHSET_BAD_ALLOC;
    }
    (*hs)->table = calloc(initial_size, sizeof(*(*hs)->table));
    (*hs)->size = initial_size;
    (*hs)->hash = hash;
    (*hs)->cmp = cmp;
    (*hs)->used = 0;

    if ((*hs)->table == NULL) {
        free((*hs)->table);
        free(*hs);
        return HASHSET_BAD_ALLOC;
    }
    return HASHSET_SUCCESS;
}

hashset_error_t hashset_get(hashset_t* hs, void* key, void** ret)
{
    if (key == NULL) {
        return HASHSET_SUCCESS;
    }
    uint64_t index = hs->hash(key) % hs->size;
    while (hs->table[index] != NULL && !hs->cmp(hs->table[index], key)) {
        index = (index + 1) % hs->size;
    }
    *ret = hs->table[index];
    return HASHSET_SUCCESS;
}

hashset_error_t hashset_next(hashset_t* hs, void** curr)
{
    static void** iter = NULL;
    if (hs == NULL && curr == NULL) {
        iter = NULL;
        return HASHSET_SUCCESS;
    }
    if (iter == NULL) {
        iter = hs->table;
    } else {
        iter++;
    }
    while (*iter == NULL && iter != hs->table + hs->size) {
        iter++;
    }
    if (iter == hs->size + hs->table) {
        *curr = NULL;
        iter = NULL;
    } else {
        *curr = *iter;
    }
    return HASHSET_SUCCESS;
}

static hashset_error_t hashset_realloc_(hashset_t* hs)
{
    hashset_t* new_hs = NULL;
    hashset_error_t err = hashset_create(&new_hs, hs->hash, hs->cmp, hs->size << 2);
    if (err != HASHSET_SUCCESS) {
        return err;
    }
    void* curr;
    hashset_next(NULL, NULL);
    do {
        hashset_next(hs, &curr);
        hashset_set(new_hs, curr);
    } while (curr != NULL);
    free(hs->table);
    hs->table = new_hs->table;
    hs->size = new_hs->size;
    free(new_hs);
    return HASHSET_SUCCESS;
}

hashset_error_t hashset_set(hashset_t* hs, void* key)
{
    if (key == NULL) {
        return HASHSET_SUCCESS;
    }
    {
        void* x;
        hashset_get(hs, key, &x);
        if (x != NULL) {
            return HASHSET_SUCCESS;
        }
    }
    hs->used++;
    if (hs->used * 4 >= hs->size) {
        hashset_error_t err = hashset_realloc_(hs);
        if (err != HASHSET_SUCCESS) {
            return err;
        }
    }
    uint64_t index = hs->hash(key) % hs->size;
    while (hs->table[index] != NULL) {
        index = (index + 1) % hs->size;
    }
    hs->table[index] = key;
    return HASHSET_SUCCESS;
}

hashset_error_t hashset_delete(hashset_t* hs, void* key)
{
    if (key == NULL) {
        return HASHSET_SUCCESS;
    }
    {
        void* ret = NULL;
        hashset_get(hs, key, &ret);
        if (ret == NULL) {
            return HASHSET_SUCCESS;
        }
    }
    uint64_t index = hs->hash(key) % hs->size;
    while (!hs->cmp(hs->table[index], key)) {
        index = (index + 1) % hs->size;
    }
    hs->table[index] = NULL;
    hs->used--;
    return HASHSET_SUCCESS;
}

hashset_error_t hashset_free(hashset_t* hs)
{
    if (hs == NULL) {
        return HASHSET_SUCCESS;
    }
    free(hs->table);
    free(hs);
    return HASHSET_SUCCESS;
}
