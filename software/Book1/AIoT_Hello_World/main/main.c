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
S
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "HELLO_UART";

void app_main(void)
{
    while (1)
    {
        printf("Hello World\r\n");
        ESP_LOGI(TAG, "Hello World (ESP_LOG)");

        vTaskDelay(pdMS_TO_TICKS(2000)); // 2 Sekunden
    }
}
