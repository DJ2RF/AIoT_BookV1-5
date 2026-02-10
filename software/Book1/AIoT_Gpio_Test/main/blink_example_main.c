/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define GPIO_TEST_PIN 17     // <<< HIER deinen echten Test-GPIO eintragen
#define TOGGLE_DELAY_MS 3000

static const char *TAG = "PROJECT11";

void app_main(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_TEST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);

    ESP_LOGI(TAG, "GPIO Test gestartet");

    int level = 0;

    while (1)
    {
        gpio_set_level(GPIO_TEST_PIN, level);

        if (level)
            ESP_LOGI(TAG, "GPIO HIGH");
        else
            ESP_LOGI(TAG, "GPIO LOW");

        level = !level;

        vTaskDelay(pdMS_TO_TICKS(TOGGLE_DELAY_MS));
    }
}
