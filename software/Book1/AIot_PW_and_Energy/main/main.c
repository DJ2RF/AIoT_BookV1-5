/******************************************************************************
 * AIoT Workshop – Band 1
 * Example Source Code
 * Project 16: Power Management and Deep Sleep
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
/******************************************************************************
 * AIoT Workshop – Band 1
 * Project 16: Power Management and Deep Sleep
 *
 * Copyright (c) 2026 Friedrich Riedhammer
 ******************************************************************************/

#include <stdio.h>
#include "esp_sleep.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SLEEP_TIME_SEC 10

static const char *TAG = "PROJECT16";

void app_main(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause == ESP_SLEEP_WAKEUP_TIMER)
        ESP_LOGI(TAG, "Wakeup from timer");
    else
        ESP_LOGI(TAG, "Normal startup");

    ESP_LOGI(TAG, "Active phase running");

    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", SLEEP_TIME_SEC);

    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_TIME_SEC * 1000000ULL);

    esp_deep_sleep_start();
}
