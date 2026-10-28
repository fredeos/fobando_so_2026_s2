// > Implementation file for timing.h
#include "timing.h"

#include <time.h>
uint64_t get_time_ms(void){
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000ULL +
           (uint64_t)ts.tv_nsec / 1000000ULL;
}