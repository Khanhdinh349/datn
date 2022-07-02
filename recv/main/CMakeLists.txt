set(component_srcs "main.c" "fb.c" "wifi.c" "lcd.c")

idf_component_register(SRCS "${component_srcs}"
                       INCLUDE_DIRS ".")


