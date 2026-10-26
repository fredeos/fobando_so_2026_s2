// > Pi Pico gpio.h implementation file
#include "gpio/gpio.h"

#include "pico/stdlib.h"

// ======================================================================
/*
 * Implementation of GPIO configuration for Pi Pico
 * 
*/
int board_gpio_init(uint32_t _pin, gpio_direction_t _mode){
    gpio_init(_pin);

    /* Las entradas usan la resistencia externa del circuito (GP18: pulldown).
     * Evitar que una resistencia interna altere ese circuito. */
    if (_mode == GPIO_INPUT) gpio_disable_pulls(_pin);

    gpio_set_dir(
        _pin,
        (_mode == GPIO_OUTPUT) ? GPIO_OUT : GPIO_IN
    );

    return 0;
}

// ======================================================================
/*
 * Implementation of GPIO write for Pi Pico
 * 
*/
int board_gpio_write(uint32_t _pin, gpio_level_t _level){
    gpio_put(_pin, _level);
    return 0;
}

// ======================================================================
/*
 * Implementation of GPIO read for Pi Pico
 * 
*/
int board_gpio_read(uint32_t _pin, gpio_level_t* _level){
    if (_level == NULL)
        return -1;

    *_level = gpio_get(_pin) ? GPIO_HIGH : GPIO_LOW;

    return 0;
}