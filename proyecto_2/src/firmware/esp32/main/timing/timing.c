// Archivo de implementacion de <timing.h
#include "timing.h"

uint64_t get_time_ms(void){
    int64_t timestamp = esp_timer_get_time(); // microseconds
    return (uint64_t) timestamp / 1000ULL;    // miliseconds
}