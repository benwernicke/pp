#ifndef HASH_SET_H
#define HASH_SET_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum {
    HASHSET_SUCCESS,
    HASHSET_BAD_ALLOC,

} hashset_error_t;

typedef uint64_t (*hashset_hash_function_t)(void*);
typedef bool (*hashset_cmp_function_t)(void* a, void* b);

typedef struct hashset_t hashset_t;

hashset_error_t hashset_set(hashset_t* hs, void* key);
hashset_error_t hashset_next(hashset_t* hs, void** curr);
hashset_error_t hashset_get(hashset_t* hs, void* key, void** ret);
hashset_error_t hashset_create(hashset_t** hs, hashset_hash_function_t hash, hashset_cmp_function_t cmp, uint64_t initial_size);
hashset_error_t hashset_delete(hashset_t* hs, void* key);
hashset_error_t hashset_free(hashset_t* hs);

#endif
