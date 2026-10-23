// > ESP32 gpio.h implementation file
#include "gpio/gpio.h"

#include "driver/gpio.h"

// ======================================================================
/*
 * Implementation of GPIO configuration for ESP32
 * 
*/
int board_gpio_init(uint32_t _pin, gpio_direction_t _mode){
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << _pin,
        .mode =
            (_mode == GPIO_OUTPUT)
                ? GPIO_MODE_OUTPUT
                : GPIO_MODE_INPUT,

        .pull_up_en =
            (_mode == GPIO_INPUT)
                ? GPIO_PULLUP_ENABLE
                : GPIO_PULLUP_DISABLE,

        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        .intr_type = GPIO_INTR_DISABLE
    };

    return gpio_config(&config);
}

// ======================================================================
/*
 * Implementation of GPIO write for ESP32
 * 
*/
int board_gpio_write(uint32_t _pin, gpio_level_t _level){
    return gpio_set_level(_pin, _level);
}

// ======================================================================
/*
 * Implementation of GPIO read for ESP32
 * 
*/
int board_gpio_read(uint32_t _pin, gpio_level_t* _level){
    if (_level == NULL)
        return -1;

    int value = gpio_get_level(_pin);

    *_level = value ? GPIO_HIGH : GPIO_LOW;

    return 0;
}