#include "gpio/gpio.h"

#define LED_PIN 25

int count = 0;

// Arduino-style setup function
void setup(void){
    /* Add your setup code here */
    board_gpio_init(LED_PIN, GPIO_OUTPUT);

    board_gpio_write(LED_PIN, GPIO_LOW);
}

// Arduino-style loop function
void loop(void){
    /* Add your loop code here */
    if (count == 0) {
        board_gpio_write(LED_PIN, GPIO_HIGH);
        count++;
    }
}

// ESP32 main application entry point
void app_main(void){
    // << Call setup >>
    setup();

    // << Call loop >>
    while(1) loop();
}