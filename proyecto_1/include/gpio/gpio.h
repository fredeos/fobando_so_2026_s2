#ifndef BOARD_GPIO_H
#define BOARD_GPIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NOTE: each microncontroller should implement the functions in this header file with their corresponding libraries.
   This is just a shared header file to standarize code on each board.
*/

// ------------------------------------------------------------------
/*
 * GPIO level microcontroller board
 * 
*/
typedef enum {
    GPIO_LOW  = 0,
    GPIO_HIGH = 1
} gpio_level_t;

/* 
 * GPIO direction mode
 * 
*/
typedef enum {
    GPIO_INPUT,
    GPIO_OUTPUT
} gpio_direction_t;
// ------------------------------------------------------------------
/*
 * Configure GPIO pin
 * Return 0 on success
*/
int board_gpio_init(uint32_t _pin, gpio_direction_t _mode);

/*
 * Write value to GPIO pin
 * Returns 0 on success
*/
int board_gpio_write(uint32_t _pin, gpio_level_t _level);

/*
 * Read value from GPIO pin
 * Returns 0 on success
*/
int board_gpio_read(uint32_t _pin, gpio_level_t* _level);

#ifdef __cplusplus
}
#endif

#endif //BOARD_GPIO_H