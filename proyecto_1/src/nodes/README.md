# Guia para uso y configuracion

## ESP32 + Raspberry Pi Pico

---

## Dependencias

- Linux Ubuntu

- [ESP-IDF para Linux](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/linux-setup.html): para compilar código de C para el ESP32. Seguir la guía de instalación adjunta (al final del documento viene el comando para instalar las dependencias necesarias)

- [Pico SDK](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html): para compilar código de C para la Raspberry Pi Pico. Seguir la guía de instalación adjunta (o ver los comandos al final de este documento)

- `cmake`: instalar con el comando `sudo apt install cmake`

- `ninja`: instalar con el comando `sudo apt install`

- `git`: instalar con el comando `sudo apt install git`

## Compilar

Este proceso de compilación se hace automatizado usando `make` por lo que el respectivo **Makefile** en la raiz de este proyecto (`fobando_so_2026_s2/proyecto_1`) contiene todas las reglas necesarias.

- ***ESP32***:

- ***Pi Pico***:

## Configuracion de proyectos

- ***ESP32***:
Los proyectos de ESP32 usan `cmake` para configurar las dependencias y codigo fuente, por tanto se deben incluir en los respectivos CMakeLists.txt todas las configuraciones necesarias.

    - 

- ***Pi Pico***:

---

## Comandos importantes

***ESP32***:
- Para instalar dependencias de ESP IDF ejecutar:
```bash
sudo apt install cmake ninja git
```

- `source "/home/<user>/.espressif/tools/activate_idf_v6.1.sh"`: para poder entrar al entorno virtual de ESP-IDF y poder compilar  el proyecto

- `idf.py set-target esp32`: para configurar el tipo dispositivo al que vamos a compilar

- `idf.py build`: para hacer build del proyecto

- `idf.py -p /dev/ttyUSB0 flash` ó `idf.py -p /dev/ttyACM0 flash`: para flashear el ejecutable al ESP32 conectado a un puerto USB o CDC, respectivamente (mantener presionado el boton de **boot**).

***Pi Pico***:

- Para instalar `pico-sdk` se puede ejecutar esta secuencia de comandos:
```bash
cd ~
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH="$HOME/pico-sdk"
```

- `cmake -S . -B build -G Ninja`: compilar el proyecto y dependencias

- `cmake --build build`: hacer build del proyecto

***General***:
- `ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null`: encontrar dispositivos conectados a un puerto serial; detecta tanto USB como CDC

- `ls -l /dev/ttyUSB0` o `ls -l /dev/ttyACM0`: para ver los permisos de los puertos. Por defecto solo el grupo `dialout` tiene permisos

- `sudo usermod -aG dialout "$USER"`: para agregar al usuario al grupo dialout y que tenga acceso a los puertos seriales. Requiere reiniciar sesion para ver los cambios.

- `groups`: ver grupos a los que pertenece el usuario.

## Aspectos adicionales