/******************************************************************************
 * AIoT Workshop – Band 1
 * Example Source Code
 *
 * Copyright (c) 2026 Friedrich Riedhammer
 *
 * This source code is provided as part of the book
 * "AIoT Workshop – Band 1".
 *
 * Permission is granted to use, modify and compile this code for
 * educational, research and product development purposes.
 *
 * Redistribution of the source code as part of other publications
 * or commercial training material requires written permission
 * of the author.
 *
 * The software is provided "as is", without warranty of any kind.
 ******************************************************************************/

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
