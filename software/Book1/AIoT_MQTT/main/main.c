/******************************************************************************
 * AIoT Workshop – Band 1
 * Project 18: MQTT Communication (Standalone Wi-Fi + MQTT)
 *
 * Copyright (c) 2026 Friedrich Riedhammer
 *
 * This source code is provided as part of the book "AIoT Workshop – Band 1".
 * Permission is granted to use, modify and compile this code for educational,
 * research and product development purposes.
 *
 * Redistribution of the source code as part of other publications or
 * commercial training material requires written permission of the author.
 *
 * The software is provided "as is", without warranty of any kind.
 ******************************************************************************/

/*
 * PROJECT GOAL
 * ------------
 * 1) Initialize NVS (required by Wi-Fi)
 * 2) Initialize TCP/IP network stack (esp_netif + event loop)
 * 3) Connect to Wi-Fi (Station mode) and wait until an IP is obtained (DHCP)
 * 4) Start MQTT client only AFTER network is ready
 * 5) Publish a status message periodically
 * 6) Subscribe to a command topic and print received messages
 *
 * WHY THIS STRUCTURE?
 * -------------------
 * If MQTT starts before the TCP/IP stack is ready, lwIP can assert with:
 *   tcpip_send_msg_wait_sem ... (Invalid mbox)
 * Therefore we enforce the correct order:
 *   Wi-Fi connected + GOT_IP  -> then MQTT start.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "mqtt_client.h"

/* --------------------------------------------------------------------------
 * USER CONFIGURATION
 * -------------------------------------------------------------------------- */

#define WIFI_SSID          "YOUR_SSID_HERE"
#define WIFI_PASS          "YOUR_PASSWORD_HERE"

/* Public broker for testing (no security). For products use TLS + auth. */
#define MQTT_BROKER_URI    "mqtt://test.mosquitto.org"

/* Topic naming: keep it stable across projects */
#define MQTT_TOPIC_STATUS  "aiot/node1/status"
#define MQTT_TOPIC_CMD     "aiot/node1/cmd"

#define WIFI_MAX_RETRY     10
#define PUBLISH_PERIOD_MS  5000

static const char *TAG = "PROJECT18";

/* --------------------------------------------------------------------------
 * WIFI STATE (EventGroup)
 * -------------------------------------------------------------------------- */

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0   /* Wi-Fi connected + got IP */
#define WIFI_FAIL_BIT      BIT1   /* Wi-Fi failed after retries */

static int s_retry_count = 0;

/* --------------------------------------------------------------------------
 * MQTT STATE
 * -------------------------------------------------------------------------- */

static esp_mqtt_client_handle_t s_mqtt_client = NULL;

/* --------------------------------------------------------------------------
 * WIFI EVENT HANDLER
 * -------------------------------------------------------------------------- */

/*
 * Handles:
 * - WIFI_EVENT_STA_START        -> start connect()
 * - WIFI_EVENT_STA_DISCONNECTED -> reconnect with retry count
 * - IP_EVENT_STA_GOT_IP         -> signal "network ready"
 */
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

        /* Network is ready -> allow MQTT start */
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

/* --------------------------------------------------------------------------
 * WIFI INITIALIZATION
 * -------------------------------------------------------------------------- */

static void wifi_init_and_connect(void)
{
    s_wifi_event_group = xEventGroupCreate();

    /* 1) TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 2) Default event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 3) Create default station interface */
    esp_netif_create_default_wifi_sta();

    /* 4) Init Wi-Fi driver */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 5) Register handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* 6) Configure credentials */
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    /* 7) Start station */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi init done");
}

/* --------------------------------------------------------------------------
 * MQTT EVENT HANDLER
 * -------------------------------------------------------------------------- */

/*
 * Called by ESP-IDF MQTT stack.
 * We subscribe after connecting, and log incoming messages.
 */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");

            /*
             * Subscribe to command topic.
             * Later projects can use this for:
             * - configuration updates
             * - mode changes
             * - OTA triggers
             */
            esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_CMD, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            break;

        case MQTT_EVENT_DATA:
            /* Data is not null-terminated: use length printing */
            ESP_LOGI(TAG, "MQTT RX topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "MQTT RX data : %.*s", event->data_len, event->data);
            break;

        default:
            break;
    }
}

/* --------------------------------------------------------------------------
 * MQTT START
 * -------------------------------------------------------------------------- */

static void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(s_mqtt_client,
                                   ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   NULL);

    esp_mqtt_client_start(s_mqtt_client);

    ESP_LOGI(TAG, "MQTT client started (broker=%s)", MQTT_BROKER_URI);
}

/* --------------------------------------------------------------------------
 * APPLICATION ENTRY
 * -------------------------------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "Project 18 starting (Wi-Fi + MQTT)");

    /* Wi-Fi requires NVS. Handle common NVS init issue when reflashing often. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init issue (%s). Erasing NVS...", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    /* 1) Init Wi-Fi and connect */
    wifi_init_and_connect();

    /* 2) Wait until we either got IP (success) or failed */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Network ready (Wi-Fi connected + IP). Starting MQTT...");
        mqtt_start();
    } else {
        ESP_LOGE(TAG, "Wi-Fi failed -> MQTT not started. Check SSID/PASS and signal.");
        /* Stop here; in products you might reboot or start AP fallback mode */
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* 3) Periodic publish loop */
    while (1) {
        const char *payload = "device alive";

        ESP_LOGI(TAG, "Publishing to %s: %s", MQTT_TOPIC_STATUS, payload);

        /*
         * QoS=1 for "at least once" delivery.
         * retain=0 so broker does not retain this as last will.
         */
        esp_mqtt_client_publish(s_mqtt_client,
                                MQTT_TOPIC_STATUS,
                                payload,
                                0,
                                1,
                                0);

        vTaskDelay(pdMS_TO_TICKS(PUBLISH_PERIOD_MS));
    }
}
