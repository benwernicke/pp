#include "flow.h"

static uint64_t rand_()
{
    static uint8_t __thread d = 0;
    return ++d;
}

// msg_t_
//--------------------------------------------------------------------------------------------------------------------------------

#define MSG_INITIALIZER \
    (msg_t_) { .count = 0, .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .death = 0 }

static bool msg_is_dead_wait_(msg_t_* msg)
{
    bool is_dead;
    pthread_mutex_lock(&msg->mutex);
    if (msg->count == 0 && !msg->death) {
        pthread_cond_wait(&msg->cond, &msg->mutex);
    }
    msg->count--;
    is_dead = msg->count == 0 && msg->death;
    pthread_mutex_unlock(&msg->mutex);
    return is_dead;
}

static void msg_send_(msg_t_* msg)
{
    pthread_mutex_lock(&msg->mutex);
    msg->count++;
    pthread_mutex_unlock(&msg->mutex);
    pthread_cond_signal(&msg->cond);
}

static void msg_send_death_(msg_t_* msg)
{
    pthread_mutex_lock(&msg->mutex);
    msg->count++;
    msg->death = 1;
    pthread_mutex_unlock(&msg->mutex);
    pthread_cond_signal(&msg->cond);
}
// future_t
//--------------------------------------------------------------------------------------------------------------------------------

#define FUTURE_NOAWAIT 0
#define FUTURE_AWAIT 1

static void future_exec_(future_t* f)
{
    f->arg = f->function(f->arg);
    switch (f->state) {
    case FUTURE_DONE:
        __builtin_unreachable(); // done tasks can't be handled;
        return;
    case FUTURE_AWAITED:
        f->state = FUTURE_DONE;
        return;
    case FUTURE_NOT_AWAITED:
        free(f);
        return;
    }
}

static void future_init_(future_t* f, future_state_t state, thread_function_t fn, void* arg)
{
    *f = (future_t) {
        .arg = arg,
        .function = fn,
        .next = NULL,
        .state = state,
    };
}

static future_t* future_create_(future_state_t state, thread_function_t fn, void* arg)
{
    future_t* f = malloc(sizeof(*f));
    if (f == NULL) {
        return NULL;
    }
    future_init_(f, state, fn, arg);
    return f;
}

// fq_t_
//--------------------------------------------------------------------------------------------------------------------------------

static future_t* fq_pop_(fq_t_* q)
{
    future_t* f = NULL;
    if (q->head != NULL) {
        f = q->head;
        q->head = q->head->next;
        f->next = NULL;
    }
    return f;
}

static void fq_push_(fq_t_* q, future_t* f)
{
    f->next = NULL;
    if (q->head != NULL) {
        q->tail = q->tail->next = f;
    } else {
        q->head = q->tail = f;
    }
}

static bool fq_try_pop_(fq_t_* q, future_t** fp)
{
    if (pthread_mutex_trylock(&q->mutex) == 0) {
        *fp = fq_pop_(q);
        pthread_mutex_unlock(&q->mutex);
        return *fp != NULL;
    }
    return 0;
}

static bool fq_try_push_(fq_t_* q, future_t* f)
{
    if (pthread_mutex_trylock(&q->mutex) == 0) {
        fq_push_(q, f);
        pthread_mutex_unlock(&q->mutex);
        return 1;
    }
    return 0;
}

static future_t* fq_force_pop_(fq_t_* q)
{
    future_t* f;
    pthread_mutex_lock(&q->mutex);
    f = fq_pop_(q);
    pthread_mutex_unlock(&q->mutex);
    return f;
}
static void fq_force_push_(fq_t_* q, future_t* f)
{
    pthread_mutex_lock(&q->mutex);
    fq_push_(q, f);
    pthread_mutex_unlock(&q->mutex);
}

// slave_t_ flow_t
//--------------------------------------------------------------------------------------------------------------------------------

static future_t* flow_pop_(flow_t* system, unsigned int i)
{
    future_t* f;
    unsigned int j;
    for (j = i; j < system->size * 4 + i; j++) {
        if (fq_try_pop_(&system->queues[j % system->size], &f)) {
            return f;
        }
    }
    return fq_force_pop_(&system->queues[i]);
}

static void* slave_work_(slave_t_* me)
{
    flow_t* system = me->system;
    future_t* f;
    bool alive = 1;

    while (alive) {
        alive = !msg_is_dead_wait_(&me->msg);
        f = flow_pop_(system, me->i);
        if (f != NULL) {
            future_exec_(f);
        }
    }
    return NULL;
}

flow_t* flow_create(uint_fast16_t size)
{
    flow_t* system = malloc(sizeof(*system));
    if (system == NULL) {
        return NULL;
    }
    slave_t_* slaves = malloc(size * sizeof(*slaves));
    fq_t_* queues = malloc(size * sizeof(*queues));

    if (slaves == NULL || queues == NULL) {
        free(system);
        free(slaves);
        free(queues);
        return NULL;
    }

    *system = (flow_t) {
        .slaves = slaves,
        .queues = queues,
        .size = size
    };
    uint_fast16_t i;
    for (i = 0; i < size; i++) {
        queues[i] = (fq_t_) {
            .mutex = PTHREAD_MUTEX_INITIALIZER,
            .head = NULL,
            .tail = NULL,
        };
    }

    bool err = 0;
    for (i = 0; i < size; i++) {
        slaves[i] = (slave_t_) {
            .thread = 0,
            .msg = MSG_INITIALIZER,
            .i = i,
            .system = system,
        };
        if (pthread_create(&slaves[i].thread, NULL, (thread_function_t)slave_work_, &slaves[i]) != 0) {
            err = 1;
        }
    }
    if (err) {
        free(system->slaves);
        free(system->queues);
        free(system);
        return NULL;
    }

    return system;
}

static void kill_slaves_(slave_t_* slaves, uint_fast16_t size)
{
    slave_t_* slave;
    for (slave = slaves; slave != slaves + size; slave++) {
        msg_send_death_(&slave->msg);
    }
    for (slave = slaves; slave != slaves + size; slave++) {
        pthread_join(slave->thread, NULL);
    }
}

void flow_destroy(flow_t* system)
{
    if (system != NULL) {
        kill_slaves_(system->slaves, system->size);
        free(system->queues);
        free(system->slaves);
        free(system);
    }
}

void flow_push_(flow_t* system, future_t* f)
{
    uint_fast16_t i = rand_() % system->size;
    uint_fast16_t j;
    for (j = i; j < system->size * 4 + i; j++) {
        if (fq_try_push_(&system->queues[j % system->size], f)) {
            msg_send_(&system->slaves[j % system->size].msg);
            return;
        }
    }
    fq_force_push_(&system->queues[i % system->size], f);
    msg_send_(&system->slaves[i % system->size].msg);
}

future_t* flow_async(flow_t* system, thread_function_t fn, void* arg)
{
    future_t* f = future_create_(FUTURE_AWAITED, fn, arg);
    if (f == NULL) {
        return NULL;
    }
    flow_push_(system, f);
    return f;
}

bool flow_async_noawait(flow_t* system, thread_function_t fn, void* arg)
{
    future_t* f = future_create_(FUTURE_NOT_AWAITED, fn, arg);
    if (f == NULL) {
        return 0;
    }
    flow_push_(system, f);
    return 1;
}

void* flow_await(flow_t* system, future_t* f)
{
    future_t* m;
    while (f->state != FUTURE_DONE) {
        m = flow_pop_(system, rand_() % system->size);
        if (m != NULL) {
            future_exec_(m);
        }
    }
    void* r = f->arg;
    free(f);
    return r;
}
