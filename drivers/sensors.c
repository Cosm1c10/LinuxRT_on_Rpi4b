#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sensors.h"

// ============================================================
// ZEPHYR IMPLEMENTATION  (target: rpi_4b via west build)
// ============================================================
#ifdef __ZEPHYR__
    #include <zephyr/kernel.h>
    #include <zephyr/drivers/gpio.h>
    #include <zephyr/drivers/i2c.h>

    /*
     * Both devices are declared in the board DTS / overlay.
     * "gpio0" = BCM2711 GPIO controller.
     * "i2c1"  = BCM2711 BSC1 (enabled in boards/rpi_4b.overlay).
     */
    #define GPIO_NODE  DT_NODELABEL(gpio0)
    #define I2C_NODE   DT_NODELABEL(i2c1)

    static const struct device *gpio_dev = NULL;
    static const struct device *i2c_dev  = NULL;

    #define ADS1115_ADDR 0x48

    // -------------------------------------------------------
    // hw_init: obtain device handles and configure GPIO pins
    // -------------------------------------------------------
    int hw_init() {
        gpio_dev = DEVICE_DT_GET(GPIO_NODE);
        if (!device_is_ready(gpio_dev)) {
            printk("[HW] ERROR: GPIO device not ready\n");
            return -1;
        }

        i2c_dev = DEVICE_DT_GET(I2C_NODE);
        if (!device_is_ready(i2c_dev)) {
            printk("[HW] ERROR: I2C device not ready\n");
            return -1;
        }

        // Configure digital sensor input pins
        gpio_pin_configure(gpio_dev, PIN_VIBRATION, GPIO_INPUT);
        gpio_pin_configure(gpio_dev, PIN_SOUND,     GPIO_INPUT);
        gpio_pin_configure(gpio_dev, PIN_TEMP_1W,   GPIO_INPUT);

        printk("[HW] GPIO and I2C initialized (Vib:GPIO%d, Snd:GPIO%d, 1W:GPIO%d)\n",
               PIN_VIBRATION, PIN_SOUND, PIN_TEMP_1W);
        return 0;
    }

    // -------------------------------------------------------
    // GPIO helpers
    // -------------------------------------------------------
    int hw_read_pin(int pin) {
        if (!gpio_dev) return 0;
        int val = gpio_pin_get(gpio_dev, (gpio_pin_t)pin);
        return (val > 0) ? 1 : 0;
    }

    void hw_configure_pin(int pin, int direction) {
        if (!gpio_dev) return;
        gpio_pin_configure(gpio_dev, (gpio_pin_t)pin,
                           (direction == 1) ? GPIO_OUTPUT_INACTIVE : GPIO_INPUT);
    }

    void hw_write_pin(int pin, int val) {
        if (!gpio_dev) return;
        gpio_pin_set(gpio_dev, (gpio_pin_t)pin, val);
    }

    // -------------------------------------------------------
    // I2C: ADS1115 ADC -> ACS712 Current Sensor
    //
    // Register map used:
    //   0x01 Config  — PGA=±4.096 V, AIN0-GND, single-shot, 128 SPS
    //   0x00 Conv    — 16-bit signed result
    //
    // Current formula (ACS712 20 A variant):
    //   V_sense  = raw_adc * (4.096 / 32768)
    //   V_orig   = V_sense * 1.5          (voltage divider compensation)
    //   I (A)    = (V_orig - 2.5) / 0.100
    // -------------------------------------------------------
    float hw_read_current_i2c() {
        if (!i2c_dev) return 0.0f;

        // 1. Write Config Register
        uint8_t config[3] = { 0x01, 0xC3, 0x83 };
        if (i2c_write(i2c_dev, config, sizeof(config), ADS1115_ADDR) != 0) {
            return 0.0f;
        }

        // Wait for single-shot conversion (~10 ms at 128 SPS)
        k_msleep(10);

        // 2. Set pointer to Conversion Register then read 2 bytes
        uint8_t ptr    = 0x00;
        uint8_t raw[2] = {0};
        if (i2c_write_read(i2c_dev, ADS1115_ADDR, &ptr, 1, raw, 2) != 0) {
            return 0.0f;
        }

        int16_t raw_adc  = (int16_t)((raw[0] << 8) | raw[1]);
        float   voltage  = raw_adc * (4.096f / 32768.0f);
        float   v_orig   = voltage * 1.5f;
        float   current  = (v_orig - 2.5f) / 0.100f;

        return (current < 0.0f) ? 0.0f : current;
    }

    // -------------------------------------------------------
    // 1-Wire: DS18B20 Temperature Sensor (GPIO bit-bang)
    //
    // Full 1-Wire protocol requires precise µs timing.
    // k_busy_wait() provides the required busy-loop delays.
    //
    // NOTE: The conversion/data-read steps below are a placeholder.
    //       The reset pulse is implemented correctly; the ROM-search
    //       and scratchpad read need to be added for production use.
    // -------------------------------------------------------
    float hw_read_temp_1wire(int pin) {
        // Reset pulse: drive low for 480 µs, release, sample presence pulse
        hw_configure_pin(pin, 1);   // output
        hw_write_pin(pin, 0);
        k_busy_wait(480);           // hold reset low

        hw_configure_pin(pin, 0);   // release (input)
        k_busy_wait(70);            // wait for presence pulse
        // (presence detection skipped for placeholder)
        k_busy_wait(410);           // complete reset window

        // Placeholder: return a simulated temperature until full 1-Wire
        // bit-bang (ROM commands 0xCC / 0x44 / 0xBE) is implemented.
        return 25.0f + (float)(k_uptime_get() % 15) / 10.0f;
    }

// ============================================================
// LINUX MOCK IMPLEMENTATION  (for client-side / host compilation)
// ============================================================
#else
    int   hw_init()                              { return 0; }
    int   hw_read_pin(int pin)                   { (void)pin; return 0; }
    void  hw_configure_pin(int pin, int dir)     { (void)pin; (void)dir; }
    void  hw_write_pin(int pin, int val)         { (void)pin; (void)val; }
    float hw_read_current_i2c()                  { return 10.5f; }
    float hw_read_temp_1wire(int pin)            { (void)pin; return 35.2f; }
#endif

// ============================================================
// SHARED HELPER  (platform-independent)
// ============================================================
const char* health_to_string(HealthStatus status) {
    switch (status) {
        case HEALTH_HEALTHY:  return "HEALTHY";
        case HEALTH_WARNING:  return "WARNING";
        case HEALTH_CRITICAL: return "CRITICAL";
        case HEALTH_FAULT:    return "FAULT";
        default:              return "UNKNOWN";
    }
}
