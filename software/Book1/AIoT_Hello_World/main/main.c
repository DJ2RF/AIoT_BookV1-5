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
