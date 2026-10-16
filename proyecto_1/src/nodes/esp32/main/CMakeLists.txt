idf_component_register(
    SRCS
        "main.c"
        "../src/calibrator.c"
        "../../../common/ut.c"

    INCLUDE_DIRS
        "."
        "../src"
        "../../../../include"

    REQUIRES
        esp_driver_uart
        esp_timer
)