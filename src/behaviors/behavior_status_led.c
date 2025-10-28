#define DT_DRV_COMPAT zmk_behavior_status_led

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>

#include <zmk_status_led/widget.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_status_led_config {
    bool indicate_battery;
};

static int behavior_status_led_init(const struct device *dev) { return 0; }

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {

    #if IS_ENABLED(CONFIG_STATUS_LED)
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_status_led_config *cfg = dev->config; 
    #endif // IS_ENABLED(CONFIG_STATUS_LED)

    #if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    if (cfg->indicate_battery) {
        show_battery();
    }
    #endif

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_status_led_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define STATUSLEDIND_INST(n)                                                                                \
    static struct behavior_status_led_config behavior_status_led_config_##n = {                       \
        .indicate_battery = DT_INST_PROP(n, indicate_battery),                                        \
    };                                                                                                \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_status_led_init, NULL, NULL, &behavior_status_led_config_##n, \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                         \
                            &behavior_status_led_driver_api);

DT_INST_FOREACH_STATUS_OKAY(STATUSLED)