/*
 * sensors.c  —  Hardware abstraction layer for Linux-RT (PREEMPT_RT)
 *               on Raspberry Pi 4.
 *
 * GPIO  : /sys/class/gpio  sysfs interface
 * I2C   : /dev/i2c-1 + ioctl(I2C_SLAVE)  →  ADS1115 ADC for ACS712
 * 1-Wire: /sys/bus/w1/devices/28-*/w1_slave  (kernel w1_therm driver)
 *
 * Kernel prerequisites (add to /boot/config.txt):
 *   dtparam=i2c_arm=on
 *   dtoverlay=w1-gpio,gpiopin=<PIN_TEMP_1W>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "sensors.h"

/* ------------------------------------------------------------------ */
/*  I2C device                                                         */
/* ------------------------------------------------------------------ */
#define I2C_DEV      "/dev/i2c-1"
#define ADS1115_ADDR  0x48

static int i2c_fd = -1;

/* ------------------------------------------------------------------ */
/*  GPIO sysfs helpers                                                 */
/* ------------------------------------------------------------------ */
static void gpio_write_file(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return;
    write(fd, val, strlen(val));
    close(fd);
}

static void gpio_export(int pin) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", pin);
    gpio_write_file("/sys/class/gpio/export", buf);
    usleep(100000);  /* give kernel time to create sysfs entries */
}

static void gpio_set_direction(int pin, const char *dir) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    gpio_write_file(path, dir);
}

/* ------------------------------------------------------------------ */
/*  hw_init                                                            */
/* ------------------------------------------------------------------ */
int hw_init(void) {
    /* Export and configure digital sensor GPIO pins */
    int pins[] = { PIN_VIBRATION, PIN_SOUND, PIN_TEMP_1W };
    for (int i = 0; i < 3; i++) {
        gpio_export(pins[i]);
        gpio_set_direction(pins[i], "in");
    }

    /* Open I2C bus */
    i2c_fd = open(I2C_DEV, O_RDWR);
    if (i2c_fd < 0) {
        fprintf(stderr, "[HW] WARNING: cannot open %s: %s\n",
                I2C_DEV, strerror(errno));
        fprintf(stderr, "[HW] Current sensor will return 0.0 A\n");
    }

    printf("[HW] GPIO and I2C initialised (Vib:GPIO%d, Snd:GPIO%d, 1W:GPIO%d)\n",
           PIN_VIBRATION, PIN_SOUND, PIN_TEMP_1W);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  GPIO read / write                                                  */
/* ------------------------------------------------------------------ */
int hw_read_pin(int pin) {
    char path[64], val[4] = {0};
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    read(fd, val, sizeof(val) - 1);
    close(fd);
    return (val[0] == '1') ? 1 : 0;
}

void hw_configure_pin(int pin, int direction) {
    gpio_set_direction(pin, direction ? "out" : "in");
}

void hw_write_pin(int pin, int val) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    gpio_write_file(path, val ? "1" : "0");
}

/* ------------------------------------------------------------------ */
/*  I2C: ADS1115 → ACS712 current sensor                             */
/*                                                                     */
/*  Config register: PGA=±4.096 V, AIN0-GND, single-shot, 128 SPS    */
/*  Current formula (ACS712 20 A variant):                            */
/*    V_sense = raw * (4.096 / 32768)                                  */
/*    V_orig  = V_sense * 1.5   (voltage-divider compensation)         */
/*    I (A)   = (V_orig - 2.5) / 0.100                                */
/* ------------------------------------------------------------------ */
float hw_read_current_i2c(void) {
    if (i2c_fd < 0) return 0.0f;

    if (ioctl(i2c_fd, I2C_SLAVE, ADS1115_ADDR) < 0) return 0.0f;

    /* Write config register: 0x01, config MSB=0xC3, LSB=0x83 */
    uint8_t cfg[3] = { 0x01, 0xC3, 0x83 };
    if (write(i2c_fd, cfg, sizeof(cfg)) != sizeof(cfg)) return 0.0f;

    /* Wait for single-shot conversion (~10 ms at 128 SPS) */
    usleep(12000);

    /* Set pointer to conversion register */
    uint8_t ptr = 0x00;
    if (write(i2c_fd, &ptr, 1) != 1) return 0.0f;

    /* Read 2 bytes */
    uint8_t raw[2] = {0};
    if (read(i2c_fd, raw, 2) != 2) return 0.0f;

    int16_t raw_adc = (int16_t)((raw[0] << 8) | raw[1]);
    float   voltage = raw_adc * (4.096f / 32768.0f);
    float   v_orig  = voltage * 1.5f;
    float   current = (v_orig - 2.5f) / 0.100f;

    return (current < 0.0f) ? 0.0f : current;
}

/* ------------------------------------------------------------------ */
/*  1-Wire: DS18B20 via kernel w1_therm driver                        */
/*                                                                     */
/*  The kernel exposes each DS18B20 at:                               */
/*    /sys/bus/w1/devices/28-<serial>/w1_slave                        */
/*  The file contains two lines; the second ends with "t=XXXXX"       */
/*  where XXXXX is temperature in millidegrees Celsius.               */
/*                                                                     */
/*  Enable in /boot/config.txt:                                        */
/*    dtoverlay=w1-gpio,gpiopin=<PIN_TEMP_1W>                         */
/* ------------------------------------------------------------------ */
float hw_read_temp_1wire(int pin) {
    (void)pin;  /* pin is set via dtoverlay at boot, not runtime */

    /* Find first DS18B20 device directory (prefix "28-") */
    DIR *d = opendir("/sys/bus/w1/devices");
    if (!d) return 0.0f;

    char dev_path[128] = {0};
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, "28-", 3) == 0) {
            snprintf(dev_path, sizeof(dev_path),
                     "/sys/bus/w1/devices/%s/w1_slave", entry->d_name);
            break;
        }
    }
    closedir(d);

    if (dev_path[0] == '\0') return 0.0f;

    FILE *f = fopen(dev_path, "r");
    if (!f) return 0.0f;

    char line[64];
    float temp = 0.0f;
    while (fgets(line, sizeof(line), f)) {
        char *t = strstr(line, "t=");
        if (t) {
            temp = (float)atoi(t + 2) / 1000.0f;
            break;
        }
    }
    fclose(f);
    return temp;
}

/* ------------------------------------------------------------------ */
/*  Shared helper                                                      */
/* ------------------------------------------------------------------ */
const char *health_to_string(HealthStatus status) {
    switch (status) {
        case HEALTH_HEALTHY:  return "HEALTHY";
        case HEALTH_WARNING:  return "WARNING";
        case HEALTH_CRITICAL: return "CRITICAL";
        case HEALTH_FAULT:    return "FAULT";
        default:              return "UNKNOWN";
    }
}
