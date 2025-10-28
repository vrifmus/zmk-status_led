#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Define fade durations for different modes
#define FADE_DURATION_PROFILE_MS 400
#define FADE_DURATION_BATTERY_MS 800
#define FADE_DURATION_USB_MS 400
#define FADE_DURATION_DISCONNECT_MS 300
#define BLINK_HOLD_DURATION_MS 100

#define LED_STATUS_ON 100
#define LED_STATUS_OFF 0

// Animation options
#define DISABLE_LED_SLEEP_PC

// Number of LEDs
#define CHILD_COUNT(...) +1
#define DT_NUM_CHILD(node_id) (DT_FOREACH_CHILD(node_id, CHILD_COUNT))
#define BACKLIGHT_NUM_LEDS DT_NUM_CHILD(DT_CHOSEN(zmk_backlight))

// LED configuration
#define LED_FADE_STEPS 100

struct Led {
    const struct device *dev;
    uint32_t id;
};

// Enumeration for LEDs
typedef enum {
    LED_1,
    LED_2,
    LED_3,
    LED_COUNT
} LedType;

// Array of LEDs
struct Led individual_leds[LED_COUNT] = {
    [LED_1] = { .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)), .id = 0 },
    [LED_2] = { .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)), .id = 1 },
    [LED_3] = { .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)), .id = 2 },
};

// Global state variables
bool is_connection_checking = false;
int usb_conn_state = ZMK_USB_CONN_NONE;
static int profile_count_blink = 1;
static uint8_t led_brightness[LED_COUNT] = {0};// Store the brightness of each LED

// Define stack size and priority for animation workqueue
#define ANIMATION_WORK_Q_STACK_SIZE 1024
#define ANIMATION_WORK_Q_PRIORITY 5

// Define stack area for animation workqueue
K_THREAD_STACK_DEFINE(animation_work_q_stack, ANIMATION_WORK_Q_STACK_SIZE);

// Define workqueue object
struct k_work_q animation_work_q;

// Helper function to set brightness of individual LED
static inline void set_individual_led_brightness(LedType led, uint8_t brightness) {
    led_set_brightness(individual_leds[led].dev, individual_leds[led].id, brightness);
    led_brightness[led] = brightness;  // Store the brightness value
}

void turn_off_all_leds() {
    for (int i = 0; i < LED_COUNT; i++) {
        const struct Led *led = &individual_leds[i];
        led_off(led->dev, led->id);
    }
    return;
}

static void fade_in_led(LedType led, uint32_t duration_ms) {
    uint32_t step_delay = duration_ms / LED_FADE_STEPS;
    for (int i = 0; i <= LED_FADE_STEPS; i++) {
        set_individual_led_brightness(led, LED_STATUS_ON * i / LED_FADE_STEPS);
        k_msleep(step_delay);
    }
    return;
}

static inline uint8_t get_led_brightness(LedType led) {
    return led_brightness[led];
}

void fade_out_all_leds(uint32_t duration_ms) {
    uint32_t step_delay = duration_ms / LED_FADE_STEPS;
    for (int j = LED_FADE_STEPS; j >= 0; j--) {
        for (int i = 0; i < LED_COUNT; i++) {
            uint8_t current_brightness = get_led_brightness(i);
            set_individual_led_brightness(i, current_brightness * j / LED_FADE_STEPS);
        }
        k_msleep(step_delay);
    }
}

void smooth_blink_leds(uint8_t led_mask, int count, uint32_t duration_ms) {
    for (int i = 0; i < count; i++) {
        // Fade in all specified LEDs simultaneously
        uint32_t step_delay = duration_ms / (2 * LED_FADE_STEPS);
        for (int j = 0; j <= LED_FADE_STEPS; j++) {
            for (int k = 0; k < LED_COUNT; k++) {
                if (led_mask & (1 << (LED_COUNT - 1 - k))) {
                    set_individual_led_brightness(k, LED_STATUS_ON * j / LED_FADE_STEPS);
                }
            }
            k_msleep(step_delay);
        }

        k_msleep(BLINK_HOLD_DURATION_MS); // Wait before fade-out

        // Fade out all specified LEDs simultaneously
        for (int j = LED_FADE_STEPS; j >= 0; j--) {
            for (int k = 0; k < LED_COUNT; k++) {
                if (led_mask & (1 << (LED_COUNT - 1 - k))) {
                    uint8_t current_brightness = get_led_brightness(k);
                    set_individual_led_brightness(k, current_brightness * j / LED_FADE_STEPS);
                }
            }
            k_msleep(step_delay);
        }

        k_msleep(BLINK_HOLD_DURATION_MS); // Wait before next blink
    }
}

struct k_work_delayable check_ble_conn_work;

void check_bluetooth_connection_handler(struct k_work *work) {
    if (!is_connection_checking) {
        return;
    } else {
        if (zmk_ble_active_profile_is_connected() || usb_conn_state != ZMK_USB_CONN_NONE) {
            is_connection_checking = false;
            return;
        } else {
            smooth_blink_leds(0b010, 1, FADE_DURATION_DISCONNECT_MS);
            k_work_reschedule(&check_ble_conn_work, K_SECONDS(4));
            return;
        }
    }
    return;
}
K_WORK_DELAYABLE_DEFINE(check_ble_conn_work, check_bluetooth_connection_handler);

void usb_animation_handler(struct k_work *work) {
#ifdef DISABLE_LED_SLEEP_PC
    if (usb_conn_state == USB_DC_SUSPEND) {
        fade_out_all_leds(FADE_DURATION_DISCONNECT_MS);
        return;
    }
#endif
    // USB animation: Sequential fade-in and fade-out all LEDs
    for (int i = 0; i < LED_COUNT; i++) {
        fade_in_led((LedType)i, FADE_DURATION_USB_MS);
        k_msleep(BLINK_HOLD_DURATION_MS); // Small delay between LEDs
    }
    k_msleep(BLINK_HOLD_DURATION_MS); // Wait before fade-out
    fade_out_all_leds(FADE_DURATION_USB_MS);
}
K_WORK_DELAYABLE_DEFINE(usb_animation_work, usb_animation_handler);

// Battery animation work handler
struct k_work_delayable battery_animation_work;

void battery_animation_handler(struct k_work *work) {
    uint8_t level = zmk_battery_state_of_charge();
    if (level <= 15) {
        smooth_blink_leds(0b100, 2, FADE_DURATION_BATTERY_MS);
    } else if (level <= 30) {
        smooth_blink_leds(0b100, 1, FADE_DURATION_BATTERY_MS);
    } else if (level <= 50) {
        smooth_blink_leds(0b110, 1, FADE_DURATION_BATTERY_MS);
    } else if (level <= 80) {
        smooth_blink_leds(0b111, 1, FADE_DURATION_BATTERY_MS);
    } else {
        smooth_blink_leds(0b111, 2, FADE_DURATION_BATTERY_MS);
    }
}
K_WORK_DELAYABLE_DEFINE(battery_animation_work, battery_animation_handler);

static int initialize_leds(const struct device *dev) {
    turn_off_all_leds();
    k_work_queue_init(&animation_work_q);

    k_work_queue_start(&animation_work_q, animation_work_q_stack,
                       K_THREAD_STACK_SIZEOF(animation_work_q_stack), ANIMATION_WORK_Q_PRIORITY,
                       NULL);

    k_work_schedule_for_queue(&animation_work_q, &battery_animation_work, K_SECONDS(1));
    return 0;
}

SYS_INIT(initialize_leds, APPLICATION, 32);

struct k_work_delayable ble_profile_work;

void ble_profile_handler(struct k_work *work) {
    smooth_blink_leds(0b100 >> (profile_count_blink), 1, FADE_DURATION_PROFILE_MS);
    if (!is_connection_checking) {
        is_connection_checking = true;
        k_work_reschedule(&check_ble_conn_work, K_SECONDS(4));
    }
}

int ble_profile_listener(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *profile_ev = as_zmk_ble_active_profile_changed(eh);
    if (profile_ev && profile_ev->index <= 2) {
        profile_count_blink = profile_ev->index;  // Set blink count based on profile index
        k_work_schedule_for_queue(&animation_work_q, &ble_profile_work, K_NO_WAIT);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
K_WORK_DELAYABLE_DEFINE(ble_profile_work, ble_profile_handler);

ZMK_LISTENER(ble_profile_status, ble_profile_listener)
ZMK_SUBSCRIPTION(ble_profile_status, zmk_ble_active_profile_changed);

struct k_work_delayable usb_conn_work;

void usb_connection_handler(struct k_work *work) {
    if (usb_conn_state == ZMK_USB_CONN_POWERED) {
        k_work_schedule_for_queue(&animation_work_q, &usb_animation_work, K_NO_WAIT);
    } else {
        is_connection_checking = true;
        k_work_reschedule(&check_ble_conn_work, K_SECONDS(4));
    }
}

int usb_connection_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *usb_ev = as_zmk_usb_conn_state_changed(eh);
    if (usb_ev) {
        usb_conn_state = usb_ev->conn_state;
        k_work_schedule_for_queue(&animation_work_q, &usb_conn_work, K_NO_WAIT);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
K_WORK_DELAYABLE_DEFINE(usb_conn_work, usb_connection_handler);

ZMK_LISTENER(usb_conn_state_listener, usb_connection_listener)
ZMK_SUBSCRIPTION(usb_conn_state_listener, zmk_usb_conn_state_changed);

void show_battery() {
    k_work_schedule_for_queue(&animation_work_q, &battery_animation_work, K_NO_WAIT);
}

void hide_battery() {
    // Optionally implement to turn off LEDs or any other behavior if needed
}