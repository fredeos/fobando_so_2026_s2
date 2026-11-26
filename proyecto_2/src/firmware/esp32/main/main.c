// =================================================================================
// Informacion relevante de la implementacion
// =================================================================================

/* Sobre el controlador de pantalla OLED (128x160): ST7735S
 *
 *
 *
*/

/* Sobre el controlador de pantalla LCD (20x4): HD44780U + PCF8574
 *
 *
 *
*/

// =================================================================================
// Drivers, librerias y modulos
// =================================================================================

// + Drivers
#include "gpio/gpio.h" // GPIO driver

// + Librerias de C
#include <stdbool.h>

// + Modulos


// + Librerias


// =================================================================================
// Configuracion de pines
// =================================================================================

// + LEDs
#define LED0 33 // protocol 0
#define LED1 26 // protocol 1
#define LED2 14 // debug 0
#define LED3 13 // debug 1

// + Botones
/* NOTA: en ESP32 los botones manejan logica invertida/negada */
#define BTN0 32 // alternar
#define BTN1 25 // anterior
#define BTN2 27 // siguiente
#define BTN3 12 // confirmar

// + I2C pantalla LCD
#define SDA 21 // I2C wire
#define SCL 22 // I2C wire
#define OE  4  // habilitar

// + SPI pantalla OLED
#define SCK  18 // VPSI
#define MOSI 23 // VSPI
#define MISO 19 // VSPI
#define SS   5  // VSPI

#define RST  16 // reset
#define DC   17 // control

// =================================================================================
// Constantes
// =================================================================================

// + Buffers
#define PROTOCOL_BUFFER PROTOCOL_FRAME_BYTES 
#define MESSAGE_LENGTH  PROTOCOL_PAYLOAD_BYTES

// =================================================================================
// Scope global
// =================================================================================

// + Inputs de botones
bool buttons[4] = { 0 };
gpio_level_t btn_in[4] = { 0 };

// =================================================================================
// Funciones auxiliares
// =================================================================================

// =================================================================================
// Codigo fuente
// =================================================================================

// Arduino-style setup function
void setup(void){
    /* Add your setup code here */
    // -------------------------------------------------
    // Configuracion de pines
    // -------------------------------------------------
    // + Pines para LEDs de protocol
    board_gpio_init(LED0, GPIO_OUTPUT); // protocol 0
    board_gpio_init(LED1, GPIO_OUTPUT); // protocol 1
    // + Pines para LEDS de debug
    board_gpio_init(LED2, GPIO_OUTPUT); // debug 0
    board_gpio_init(LED3, GPIO_OUTPUT); // debug 1
    // + Pines de botones
    board_gpio_init(BTN0, GPIO_INPUT);  // alternar
    board_gpio_init(BTN1, GPIO_INPUT);  // anterior
    board_gpio_init(BTN2, GPIO_INPUT);  // siguiente
    board_gpio_init(BTN3, GPIO_INPUT);  // confirmar
    // -------------------------------------------------
    // Configuracion de interfaces y controladores
    // -------------------------------------------------
    // + Perifericos I2C
    // + Perifericos SPI
    // -------------------------------------------------
    // Configuracion de estimulos iniciales
    // -------------------------------------------------
    // + Estimulos para LEDs
    board_gpio_write(LED0, GPIO_LOW);
    board_gpio_write(LED1, GPIO_LOW);
    board_gpio_write(LED2, GPIO_LOW);
    board_gpio_write(LED3, GPIO_LOW);
    // -------------------------------------------------
    // Configuracion de tareasGPIO_LOW
    // -------------------------------------------------
    // + Gestion de protocolo
    // + Gestion de I2C
    // + Gestion de SPI
}

// Arduino-style loop function
void loop(void){
    /* Add your loop code here */
    // -------------------------------------------------
    // Lectura de inputs
    // -------------------------------------------------
    // + Lectura de niveles logicos
    board_gpio_read(BTN0, &btn_in[0]);
    board_gpio_read(BTN1, &btn_in[1]);
    board_gpio_read(BTN2, &btn_in[2]);
    board_gpio_read(BTN3, &btn_in[3]);
    // + Interpretacion de inputs
    for (int i = 0; i < 4; i++) buttons[i] = (btn_in[i] == GPIO_HIGH);
    // -------------------------------------------------
    // Escritura de outputs
    // -------------------------------------------------
    // + Escritura en LEDs
    // << Anterior o siguiente >>
    if (buttons[1] || buttons[2])
        board_gpio_write(LED2, GPIO_HIGH);
    else
        board_gpio_write(LED2, GPIO_LOW);
    // << Alternar o confirmar >>
    if (buttons[0] || buttons[3])
        board_gpio_write(LED3, GPIO_HIGH);
    else
        board_gpio_write(LED3, GPIO_LOW);
}

// ESP32 main application entry point
void app_main(void){
    // << Call setup >>
    setup();

    // << Call loop >>
    while(1) loop();
}