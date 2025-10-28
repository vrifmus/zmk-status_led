# ZMK Status LED's Module

ZMK Module for LED indication of keyboard states, like connection, switching profiles, battery level.
For full indication, i'll recommend activate LED's in [bootloader](https://github.com/aroum/nRF52_Bootloader_custom_LED).

Module uses LED's behavior code from [Kabarga](https://github.com/aroum/kabarga) keyboard.

## Setup

### Adjust your west.yaml to use the module

Add module to the `config/west.yml`:

```diff
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
+   - name: vrifmus
+     url-base: https://github.com/vrifmus

  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
+   - name: zmk-status_led
+     remote: vrifmus
+     revision: main

  self:
    path: config
```

### Adjust your shield.conf file

Activate module in `config/<name>.conf`:

```diff
...

CONFIG_BT_MAX_CONN=3
CONFIG_BT_MAX_PAIRED=3

+CONFIG_STATUS_LED=y
```

### Create definitions include file

Create `config/status_led.dtsi` file:

```diff
/ {
    chosen {
        zmk,backlight = &backlight;
    };

    backlight: pwmleds {
        compatible = "pwm-leds";
        pwm_led_0: led_0 {
            pwms = <&pwm0 0 PWM_MSEC(10) PWM_POLARITY_NORMAL>;
            label = "LED_NODE_0";
        };
        pwm_led_1: led_1 {
            pwms = <&pwm0 1 PWM_MSEC(10) PWM_POLARITY_NORMAL>;
            label = "LED_NODE_1";
        };
        pwm_led_2: led_2 {
            label = "LED_NODE_2";
            pwms = <&pwm0 2 PWM_MSEC(10) PWM_POLARITY_NORMAL>;
        };
    };
};


&pinctrl {
    pwm0_default: pwm0_default {
        group1 {
            psels = <NRF_PSEL(PWM_OUT0, 1, 7)>, // LED 0
                    <NRF_PSEL(PWM_OUT1, 1, 2)>, // LED 1
                    <NRF_PSEL(PWM_OUT2, 1, 1)>;  // LED 2
        };
    };
    pwm0_sleep: pwm0_sleep {
        group1 {
            psels = <NRF_PSEL(PWM_OUT0, 1, 7)>, // LED 0
                    <NRF_PSEL(PWM_OUT1, 1, 2)>, // LED 1
                    <NRF_PSEL(PWM_OUT2, 1, 1)>;  // LED 2
            low-power-enable;
        };
    };
};


&pwm0 {
    status = "okay";
    pinctrl-0 = <&pwm0_default>;
    pinctrl-1 = <&pwm0_sleep>;
    pinctrl-names = "default", "sleep";
};
```

Adjust pins and include file on your `config/<name>.overlay`:

```diff
#include "<name>.dtsi"
+#include "status_led.dtsi"
```

## Showing battery level on demand

For checking battery level on demand you need add behavior to your keymap and use keycode.

### Include behavior

Include behavior file in your `config/<name>.keymap`:

```diff
#include <behaviors.dtsi>
#include <dt-bindings/zmk/keys.h>
+#include <behaviors/status_led.dtsi>
```

### Bind keycode

Use `&ind_bat` in your `config/<name>.keymap`:

```diff
/ {
    keymap {
        ...
        some_layer {
            bindings = <
                ...
                &ind_bat  // indicate battery level
                ...
            >;
        };
    };
};
```