
/******************************************************************************
 * AIoT Workshop – Band 1
 * Project 19: OTA Firmware Update via HTTP
 *
 * Copyright (c) 2026 Friedrich Riedhammer
 *
 * This source code is provided as part of the book "AIoT Workshop – Band 1".
 * Permission is granted to use, modify and compile this code for educational,
 * research and product development purposes.
 * Redistribution as part of other publications or commercial training material
 * requires written permission of the author.
 *
 * The software is provided "as is", without warranty of any kind.
 ******************************************************************************/

/*
 * PROJECT GOAL
 * ------------
 * 1) Connect to Wi-Fi (station mode)
 * 2) Download a firmware binary via HTTP (URL)
 * 3) Write it into the next OTA partition (ota_0 / ota_1)
 * 4) Set new partition as boot target
 * 5) Reboot into new firmware
 *
 * REQUIRED MENUCONFIG SETTINGS
 * ----------------------------
 * - Partition Table -> "Factory app, two OTA definitions"
 * - Serial flasher config -> Flash size must match real hardware
 *
 * WHY HTTP OTA FIRST?
 * -------------------
 * It is the simplest OTA form: fixed URL, deterministic behavior.
 * MQTT-triggered OTA is introduced later after MQTT is stable.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

/* --------------------------------------------------------------------------
 * USER CONFIGURATION
 * -------------------------------------------------------------------------- */

//#define WIFI_SSID           "YOUR_SSID_HERE"
//#define WIFI_PASS           "YOUR_PASSWORD_HERE"
#define WIFI_SSID      "farswitch" //YOUR_SSID_HERE"
#define WIFI_PASS      "Kl79_?Sa13_04_1961Kl79_?Sa" //YOUR_PASSWORD_HERE"

/*
 * OTA URL must point directly to the APP binary (*.bin).
 * Example:
 *   http://192.168.1.21:8000/AIoT_OTA.bin
 */
#define OTA_FIRMWARE_URL    "http://192.168.1.21:8000/AIoT_OTA.bin"

/* Wi-Fi behavior */
#define WIFI_MAX_RETRY      10

/* Download behavior */
#define HTTP_TIMEOUT_MS     10000
#define OTA_BUF_SIZE        1024

static const char *TAG = "PROJECT19";

/* --------------------------------------------------------------------------
 * WIFI STATE (EventGroup)
 * -------------------------------------------------------------------------- */

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static int s_retry_count = 0;

/* --------------------------------------------------------------------------
 * Helper: print partitions (diagnostic, very useful for OTA bring-up)
 * -------------------------------------------------------------------------- */

static void print_app_partitions(void)
{
    ESP_LOGI(TAG, "Listing APP partitions (for OTA diagnostics):");

    esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);

    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        ESP_LOGI(TAG, "APP: label=%s subtype=0x%02x addr=0x%lx size=%lu",
                 p->label,
                 p->subtype,
                 (unsigned long)p->address,
                 (unsigned long)p->size);
        it = esp_partition_next(it);
    }

    esp_partition_iterator_release(it);
}

/* --------------------------------------------------------------------------
 * Wi-Fi event handler
 * -------------------------------------------------------------------------- */

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started, connecting...");
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Disconnected. Retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Wi-Fi connect failed after %d retries", WIFI_MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

/* --------------------------------------------------------------------------
 * Wi-Fi initialization (station mode)
 * -------------------------------------------------------------------------- */

static void wifi_init_and_connect(void)
{
    s_wifi_event_group = xEventGroupCreate();

    /* TCP/IP stack + default event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* default Wi-Fi STA netif */
    esp_netif_create_default_wifi_sta();

    /* Wi-Fi driver */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* events */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* credentials */
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    /* start station */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi init done");
}

/* --------------------------------------------------------------------------
 * OTA over HTTP
 * -------------------------------------------------------------------------- */

/*
 * ota_http_update()
 * -----------------
 * Robust OTA flow:
 * - select next OTA partition
 * - open HTTP connection
 * - fetch headers (IMPORTANT: ensures status code is valid)
 * - stream download -> esp_ota_write()
 * - finalize -> set boot partition -> restart
 */
static esp_err_t ota_http_update(void)
{
    ESP_LOGI(TAG, "Starting OTA from URL: %s", OTA_FIRMWARE_URL);

    /* 1) Determine next OTA partition */
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found. Check partition table setting in menuconfig.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (0x%lx, size=%lu)",
             update_partition->label,
             (unsigned long)update_partition->address,
             (unsigned long)update_partition->size);

    /* 2) HTTP client config */
    esp_http_client_config_t config = {
        .url = OTA_FIRMWARE_URL,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    /* 3) Open connection */
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    /*
     * 4) Fetch headers BEFORE reading status code.
     * Without this step, status code can remain 0 even if the server answered.
     */
    int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "HTTP: fetch_headers failed (no valid response headers)");
        ESP_LOGE(TAG, "HTTP errno: %d", esp_http_client_get_errno(client));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int http_status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status=%d, content_length=%lld", http_status, content_length);

    if (http_status != 200) {
        ESP_LOGE(TAG, "HTTP status not OK: %d", http_status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    /* 5) Begin OTA */
    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    /* 6) Stream download into flash */
    uint8_t *buffer = (uint8_t *)malloc(OTA_BUF_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Out of memory");
        esp_ota_end(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total_written = 0;

    while (1) {
        int read_len = esp_http_client_read(client, (char *)buffer, OTA_BUF_SIZE);

        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            err = ESP_FAIL;
            break;
        }

        if (read_len == 0) {
            /* End of stream */
            err = ESP_OK;
            ESP_LOGI(TAG, "Download complete, total bytes written: %d", total_written);
            break;
        }

        err = esp_ota_write(ota_handle, buffer, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            break;
        }

        total_written += read_len;
    }

    free(buffer);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed during download/write");
        esp_ota_end(ota_handle);
        return err;
    }

    /* 7) End OTA */
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 8) Set boot target */
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 9) Reboot */
    ESP_LOGI(TAG, "OTA successful. Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK; /* not reached */
}

/* --------------------------------------------------------------------------
 * app_main()
 * -------------------------------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "Project 19 starting: OTA via HTTP");

    /* Print partition info early (helps diagnose OTA issues immediately) */
    print_app_partitions();

    /* NVS required by Wi-Fi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init issue (%s). Erasing NVS...", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    /* Connect Wi-Fi */
    wifi_init_and_connect();

    /* Wait for network ready (GOT_IP) */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi ready -> starting OTA");
        esp_err_t err = ota_http_update();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Wi-Fi failed -> OTA not possible");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
