#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>

// necessary structs for schedulers

typedef struct Task
{
    uint32_t id;
    char type;
    uint32_t W; // UT quantity
    uint32_t T; // period of time
    uint32_t D; // relative end time
    uint32_t priority; // how important the task is
    uint32_t C; // tick equivalent
};

typedef struct Job
{
    Task *task;
    uint32_t task_id;
    uint32_t release;
    uint32_t deadline;
    uint32_t ticks_remaining;
    bool active;

};



#endif