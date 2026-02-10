#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define GPIO_INPUT_PIN 17
#define POLL_DELAY_MS 50

static const char *TAG = "PROJECT12";

void app_main(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_INPUT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);

    int last_state = -1;

    while (1)
    {
        int state = gpio_get_level(GPIO_INPUT_PIN);

        if (state != last_state)
        {
            if (state)
                ESP_LOGI(TAG, "INPUT HIGH");
            else
                ESP_LOGI(TAG, "INPUT LOW");

            last_state = state;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_DELAY_MS));
    }
}
