#ifndef FLOW_H
#define FLOW_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef void* (*thread_function_t)(void*);

typedef enum {
    FUTURE_AWAITED,
    FUTURE_DONE,
    FUTURE_NOT_AWAITED,
} future_state_t;

typedef struct future_t future_t;
struct future_t {
    future_state_t state;

    void* arg;
    thread_function_t function;

    future_t* next;
};

typedef struct fq_t_ fq_t_;
struct fq_t_ {
    pthread_mutex_t mutex;
    future_t* head;
    future_t* tail;
};

typedef struct msg_t_ msg_t_;
struct msg_t_ {
    size_t count;
    bool death;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

typedef struct slave_t_ slave_t_;
typedef struct flow_t flow_t;

struct slave_t_ {
    flow_t* system;
    uint_fast16_t i;
    pthread_t thread;
    msg_t_ msg;
};

struct flow_t {
    slave_t_* slaves;
    fq_t_* queues;
    uint8_t size;
};

void* flow_await(flow_t* system, future_t* f);
future_t* flow_async(flow_t* system, thread_function_t fn, void* arg);
void flow_destroy(flow_t* system);
flow_t* flow_create(uint_fast16_t size);
bool flow_async_noawait(flow_t* system, thread_function_t fn, void* arg);

#endif
