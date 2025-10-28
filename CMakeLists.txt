target_sources_ifdef(CONFIG_STATUS_LED app PRIVATE src/status_led.c)

zephyr_include_directories(include)