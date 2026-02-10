/******************************************************************************
 * AIoT Workshop – Band 1
 * Project 17: Wi-Fi Connect + HTTP GET (minimal network bring-up)
 *
 * Copyright (c) 2026 Friedrich Riedhammer
 *
 * This source code is provided as part of the book "AIoT Workshop – Band 1".
 * Permission is granted to use, modify and compile this code for educational,
 * research and product development purposes.
 * Redistribution of the source code as part of other publications or
 * commercial training material requires written permission of the author.
 *
 * The software is provided "as is", without warranty of any kind.
 ******************************************************************************/

/*
 * PROJECT GOAL
 * -----------
 * 1) Connect to Wi-Fi (Station Mode)
 * 2) Wait until DHCP assigned an IP address
 * 3) Perform an HTTP GET request
 * 4) Print the HTTP response body to the monitor
 *
 * WHY THIS PROJECT?
 * -----------------
 * - Demonstrates the ESP-IDF event system: WiFi events and IP events
 * - Demonstrates basic network readiness (DHCP, routing, DNS if needed)
 * - Creates a repeatable baseline before MQTT, HTTPS and OTA are introduced
 *
 * NOTES
 * -----
 * - This example uses HTTP (not HTTPS) to keep the first network test simple.
 * - If you run into "port cannot be opened" issues after sleep experiments:
 *   USB-CDC can re-enumerate. For Project 17 we do NOT use deep sleep.
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_http_client.h"

/* -------------------- USER CONFIG -------------------- */
/* Set your Wi-Fi credentials here. */
#define WIFI_SSID      "YOUR_SSID_HERE"
#define WIFI_PASS      "YOUR_PASSWORD_HERE"

/*
 * How many reconnect attempts before we declare a failure.
 * In a product you would implement a backoff strategy (e.g. exponential),
 * but for a first project a fixed retry count is easier to understand.
 */
#define WIFI_MAX_RETRY 10

/*
 * HTTP endpoint for the first test.
 * - Use a simple, always reachable URL.
 * - For corporate networks/proxies this might fail; then pick a local URL.
 */
#define HTTP_TEST_URL  "http://example.com/"

/* -------------------- INTERNAL STATE -------------------- */
static const char *TAG = "PROJECT17";

/*
 * We use an Event Group to synchronize "Wi-Fi connected" and "Wi-Fi failed".
 * This allows app_main() to wait until the system is ready (or failed)
 * without busy-wait loops.
 */
static EventGroupHandle_t wifi_event_group;

/* Event group bits */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* Retry counter for reconnects */
static int s_retry_count = 0;

/* -------------------------------------------------------- */
/* 17.4.1  Wi-Fi + IP event handler                          */
/* -------------------------------------------------------- */

/*
 * ESP-IDF uses an event loop:
 * - Wi-Fi driver emits WIFI_EVENT_xxx
 * - TCP/IP stack emits IP_EVENT_xxx
 *
 * We register ONE handler that reacts to:
 * - WIFI_EVENT_STA_START          -> start connecting
 * - WIFI_EVENT_STA_DISCONNECTED   -> retry / fail
 * - IP_EVENT_STA_GOT_IP           -> success (DHCP done)
 */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started, connecting to AP...");
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /*
         * This event means: no link to AP (wrong password, weak signal, AP down, etc.)
         * For a stable system:
         * - log the reason
         * - implement backoff
         * - possibly reboot after long time
         */
        if (s_retry_count < WIFI_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Disconnected. Retrying %d/%d ...", s_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Wi-Fi connect failed after %d retries", WIFI_MAX_RETRY);
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /*
         * This is the key "network ready" event:
         * - We have link-level connectivity AND DHCP assigned an IP address.
         * - Now HTTP/MQTT/OTA can start.
         */
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP via DHCP: " IPSTR, IP2STR(&event->ip_info.ip));

        s_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

/* -------------------------------------------------------- */
/* 17.4.2  Wi-Fi initialization (Station mode)               */
/* -------------------------------------------------------- */

/*
 * wifi_init_sta()
 * ---------------
 * Sets up:
 * - TCP/IP stack (esp_netif)
 * - default event loop
 * - Wi-Fi driver in STA mode
 * - event handlers
 * - starts Wi-Fi
 *
 * Important:
 * - NVS is required by Wi-Fi (it stores calibration and other data).
 */
static void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    /* Initialize TCP/IP network interface (required for DHCP/HTTP/etc.) */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Create default event loop used by Wi-Fi and other components */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create default Wi-Fi station network interface (DHCP client, etc.) */
    esp_netif_create_default_wifi_sta();

    /* Initialize Wi-Fi driver */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handlers for Wi-Fi and IP events */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* Configure the SSID / password */
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    /*
     * Optional robustness settings (kept minimal here):
     * - threshold.authmode can enforce WPA2, etc.
     * - pmf_cfg for Protected Management Frames
     */

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* Start Wi-Fi: this triggers WIFI_EVENT_STA_START */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta() finished");
}

/* -------------------------------------------------------- */
/* 17.4.3  HTTP client                                       */
/* -------------------------------------------------------- */

/*
 * HTTP event handler
 * ------------------
 * Called by esp_http_client while receiving the response.
 * We print the response body as chunks. This is not a JSON parser,
 * only a visibility test that network + HTTP works.
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            /*
             * evt->data is NOT guaranteed to be null-terminated.
             * We print it as a bounded string using %.*s
             */
            if (evt->data && evt->data_len > 0) {
                printf("%.*s", evt->data_len, (const char *)evt->data);
            }
            break;

        default:
            /* Other events exist (connected, headers, finish, disconnect) */
            break;
    }
    return ESP_OK;
}

/*
 * http_get_example()
 * ------------------
 * Performs one HTTP GET request and prints:
 * - response body (via event handler)
 * - HTTP status code
 * - content length (if server provides it)
 */
static void http_get_example(void)
{
    ESP_LOGI(TAG, "HTTP GET test -> %s", HTTP_TEST_URL);

    esp_http_client_config_t config = {
        .url = HTTP_TEST_URL,
        .event_handler = http_event_handler,
        /*
         * Timeout is helpful in unreliable networks.
         * Increase if you use slow/remote endpoints.
         */
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    /* Perform request (blocking). For products, you often run this in a task. */
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        long long len = esp_http_client_get_content_length(client);

        ESP_LOGI(TAG, "HTTP GET done. Status=%d, content_length=%lld", status, len);
    } else {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    /* Print a newline after raw body output */
    printf("\n");
}

/* -------------------------------------------------------- */
/* 17.4.4  app_main()                                        */
/* -------------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "Project 17 starting...");

    /*
     * Wi-Fi requires NVS.
     * If NVS init fails due to no free pages, we erase and re-init.
     * This is common when flashing different projects often.
     */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init issue (%s). Erasing NVS...", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    /* Start Wi-Fi station */
    wifi_init_sta();

    /*
     * Wait until either:
     * - connected and got IP (WIFI_CONNECTED_BIT), or
     * - failed after retries (WIFI_FAIL_BIT)
     */
    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected. Network is ready.");
        http_get_example();
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Wi-Fi connection failed. Check SSID/password and signal.");
    } else {
        /* Should never happen */
        ESP_LOGE(TAG, "Unexpected event group state");
    }

    /*
     * Keep the app alive.
     * In later projects, the device will continue with MQTT/OTA or periodic tasks.
     */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
