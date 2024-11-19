#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/sensor/sensor.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/dedic_gpio.h>
#include <xtensa/core-macros.h>

namespace esphome {
namespace garantia {

#define A 10
#define B 7
#define C 8
#define D 9
#define L1 11
#define L2 12
#define L3 13

static volatile int d1 = 0;
static volatile int d2 = 0;
static volatile int d3 = 0;

dedic_gpio_bundle_handle_t num_bundle = NULL;
const int bundle_gpios[] = { A, B, C, D, L1, L2, L3 };

__attribute__((always_inline)) uint32_t getCcount() {
    uint32_t ccount;
    __asm__ __volatile__("rsr %0,ccount":"=a" (ccount));
    return ccount;
}
uint32_t MS_TO_CYCLES(const uint32_t ms) {
    const double cyclesPerNs = XTAL_CLK_FREQ/1000.0;
    const double elapsedCcounts = (double)ms * cyclesPerNs;
    return (int)(elapsedCcounts);
}
static void gpio_sampler(void* arg) {
    // Setup dedicated GPIO
    gpio_config_t io_conf = { .mode = GPIO_MODE_INPUT, };
    for (int i = 0; i < sizeof(bundle_gpios) / sizeof(bundle_gpios[0]); i++) {
        io_conf.pin_bit_mask = 1ULL << bundle_gpios[i];
        gpio_config(&io_conf);
    }
    dedic_gpio_bundle_config_t bundle_config = {
        .gpio_array = bundle_gpios,
        .array_size = sizeof(bundle_gpios) / sizeof(bundle_gpios[0]),
        .flags = { .in_en = 1 }
    };
    ESP_ERROR_CHECK(dedic_gpio_new_bundle(&bundle_config, &num_bundle));

    uint32_t val = 0;
    uint32_t prevVal = 0;
    int last_change_cycles = 0;
    int cycles_to_wait = MS_TO_CYCLES(1000); // 1s
    while (1) {
        val = dedic_gpio_bundle_read_in(num_bundle);
        if (val != prevVal) {
            if ((val >> 4) & 1) d1 = val & 0xf;
            if ((val >> 5) & 1) d2 = val & 0xf;
            if ((val >> 6) & 1) d3 = val & 0xf;
            last_change_cycles = getCcount();
        }
        if (getCcount() - last_change_cycles > cycles_to_wait) {
            last_change_cycles = getCcount();
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        prevVal = val;
    }
}

class WaterLevel : public sensor::Sensor, public PollingComponent {
public:
    void setup() override {
        ESP_LOGCONFIG("Garantia", "Setting up Garantia Sensor '%s'...", this->get_name().c_str());
        xTaskCreatePinnedToCore(&gpio_sampler, "gpio_sampler", 4096, NULL, 1, NULL, 1);
    }
    void update() override {
        int value = (d1 != 15? d1 * 100 : 0)
                  + (d2 != 15? d2 * 10 : 0)
                  + (d3 != 15? d3 : 0);

        ESP_LOGI("Garantia", "Got value: %d", value);

        this->publish_state(value);
    }
};

}  // namespace garantia
}  // namespace esphome
