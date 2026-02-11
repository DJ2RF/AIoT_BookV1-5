/******************************************************************************
 * AIoT Workshop – Band 1
 * Project 21: Professional Finalization
 * - Unique device ID (MAC-based)
 * - Per-device topics
 * - Status model (retained)
 * - MQTT Last Will (offline detection)
 * - Command handling (ping, sleep, ota=<url>)
 *
 * Copyright (c) 2026 Friedrich Riedhammer
 *
 * This source code is provided as part of the book "AIoT Workshop – Band 1".
 * Permission is granted to use, modify and compile this code for educational,
 * research and product development purposes.
 *
 * Redistribution as part of other publications or commercial training material
 * requires written permission of the author.
 *
 * The software is provided "as is", without warranty of any kind.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_sleep.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"

#include "mqtt_client.h"

/* -------------------- USER CONFIG -------------------- */

#define WIFI_SSID             "YOUR_SSID_HERE"
#define WIFI_PASS             "YOUR_PASSWORD_HERE"

#define MQTT_BROKER_URI       "mqtt://test.mosquitto.org"

/*
 * Default sleep time. Can be changed at runtime via MQTT command:
 *   sleep=60
 */
#define DEFAULT_SLEEP_SEC     30

/*
 * Firmware version string (book-style).
 * Increase this when building OTA binaries.
 */
#define FW_VERSION            "1.0.0"

/* -------------------- INTERNAL -------------------- */

static const char *TAG = "PROJECT21";

/* Wi-Fi synchronization */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1
#define WIFI_MAX_RETRY        10
static int s_retry_count = 0;

/* MQTT synchronization */
static EventGroupHandle_t s_mqtt_event_group;
#define MQTT_CONNECTED_BIT    BIT0
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

/* Device identity and topics */
static char s_node_id[16];            // "AABBCCDDEEFF"
static char s_t_status[64];           // aiot/<id>/status
static char s_t_telemetry[64];        // aiot/<id>/telemetry
static char s_t_cmd[64];              // aiot/<id>/cmd
static char s_t_event[64];            // aiot/<id>/event

/* Runtime parameters */
static int s_sleep_sec = DEFAULT_SLEEP_SEC;

/* Command handling */
static volatile bool s_cmd_ota_requested = false;
static char s_ota_url[256] = {0};

/* -------------------- Helpers -------------------- */

static void generate_node_id(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);


    snprintf(s_node_id, sizeof(s_node_id),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Node ID: %s", s_node_id);
}

static void build_topics(void)
{
    snprintf(s_t_status, sizeof(s_t_status),    "aiot/%s/status",    s_node_id);
    snprintf(s_t_telemetry, sizeof(s_t_telemetry),"aiot/%s/telemetry", s_node_id);
    snprintf(s_t_cmd, sizeof(s_t_cmd),          "aiot/%s/cmd",       s_node_id);
    snprintf(s_t_event, sizeof(s_t_event),      "aiot/%s/event",     s_node_id);

    ESP_LOGI(TAG, "Topics:");
    ESP_LOGI(TAG, "  %s", s_t_status);
    ESP_LOGI(TAG, "  %s", s_t_telemetry);
    ESP_LOGI(TAG, "  %s", s_t_cmd);
    ESP_LOGI(TAG, "  %s", s_t_event);
}

static const char *wakeup_reason_str(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER: return "timer";
        case ESP_SLEEP_WAKEUP_EXT0:  return "ext0";
        case ESP_SLEEP_WAKEUP_EXT1:  return "ext1";
        case ESP_SLEEP_WAKEUP_GPIO:  return "gpio";
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "power_on";
        default: return "other";
    }
}

/* Publish retained status (dashboard-friendly) */
static void publish_status_retained(const char *state, const char *extra_kv)
{
    char msg[192];

    if (extra_kv && extra_kv[0]) {
        snprintf(msg, sizeof(msg),
                 "state=%s;id=%s;fw=%s;%s",
                 state, s_node_id, FW_VERSION, extra_kv);
    } else {
        snprintf(msg, sizeof(msg),
                 "state=%s;id=%s;fw=%s",
                 state, s_node_id, FW_VERSION);
    }

    ESP_LOGI(TAG, "STATUS: %s", msg);

    /* retain=1 so last known state is visible even after reconnect */
    esp_mqtt_client_publish(s_mqtt_client, s_t_status, msg, 0, 1, 1);
}

/* Publish non-retained event (short-lived) */
static void publish_event(const char *event_kv)
{
    if (!event_kv) return;
    ESP_LOGI(TAG, "EVENT: %s", event_kv);
    esp_mqtt_client_publish(s_mqtt_client, s_t_event, event_kv, 0, 1, 0);
}

/* -------------------- Wi-Fi -------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started, connecting...");
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Wi-Fi disconnected. Retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Wi-Fi failed");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

static void wifi_init_and_connect(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* -------------------- Command parsing -------------------- */

static void trim_spaces(char *s)
{
    if (!s) return;
    /* left trim */
    while (*s && isspace((unsigned char)*s)) s++;
}

static void handle_cmd_payload(const char *payload, int len)
{
    /* Copy to a null-terminated buffer */
    char buf[256];
    int n = (len < (int)sizeof(buf) - 1) ? len : (int)sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';

    ESP_LOGI(TAG, "CMD payload: %s", buf);

    /* ping */
    if (strcmp(buf, "ping") == 0) {
        publish_event("event=pong");
        return;
    }

    /* sleep=<sec> */
    if (strncmp(buf, "sleep=", 6) == 0) {
        int sec = atoi(buf + 6);
        if (sec >= 1 && sec <= 86400) {
            s_sleep_sec = sec;
            char msg[64];
            snprintf(msg, sizeof(msg), "event=sleep_set;sec=%d", s_sleep_sec);
            publish_event(msg);
        } else {
            publish_event("event=err;reason=bad_sleep_range");
        }
        return;
    }

    /*
     * ota=<url>
     * This project only sets a flag and stores the URL.
     * The actual OTA procedure is performed in the main flow (safe context).
     */
    if (strncmp(buf, "ota=", 4) == 0) {
        const char *url = buf + 4;
        if (strlen(url) >= 8 && strlen(url) < sizeof(s_ota_url)) {
            strncpy(s_ota_url, url, sizeof(s_ota_url));
            s_cmd_ota_requested = true;
            publish_event("event=ota_requested");
        } else {
            publish_event("event=err;reason=bad_ota_url");
        }
        return;
    }

    publish_event("event=err;reason=unknown_cmd");
}

/* -------------------- MQTT -------------------- */

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t id, void *data)
{
    (void)args; (void)base;

    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);

            /* subscribe to per-device command topic */
            esp_mqtt_client_subscribe(s_mqtt_client, s_t_cmd, 0);

            /* publish retained online state */
            publish_status_retained("online", "stage=connected");
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            break;

        case MQTT_EVENT_DATA:
            /* Only process commands for our command topic */
            if (e->topic_len == (int)strlen(s_t_cmd) &&
                memcmp(e->topic, s_t_cmd, e->topic_len) == 0) {
                handle_cmd_payload(e->data, e->data_len);
            }
            break;

        default:
            break;
    }
}

static void mqtt_start(void)
{
    s_mqtt_event_group = xEventGroupCreate();

    /*
     * Last Will & Testament (LWT):
     * If device disconnects unexpectedly, broker publishes "offline" retained.
     * This is the standard mechanism for fleet monitoring.
     */
    char will_msg[128];
    snprintf(will_msg, sizeof(will_msg), "state=offline;id=%s;fw=%s", s_node_id, FW_VERSION);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,

        /* LWT settings */
        .session.last_will.topic = s_t_status,
        .session.last_will.msg = will_msg,
        .session.last_will.msg_len = 0, /* 0 => treat msg as null-terminated */
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
    };

    s_mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

/* -------------------- Optional OTA hook -------------------- */
/*
 * For Project 21 we only show where OTA fits in.
 * The actual OTA implementation is Project 19.
 *
 * If you want to trigger OTA here:
 * - include Project 19's ota_http_update(url) function
 * - call it when s_cmd_ota_requested becomes true
 *
 * This keeps Project 21 readable while showing the integration point.
 */
static void ota_placeholder_run_if_requested(void)
{
    if (!s_cmd_ota_requested) return;

    publish_status_retained("ota", "stage=requested");
    ESP_LOGW(TAG, "OTA requested via CMD. URL=%s", s_ota_url);

    /*
     * Integration point:
     *  - call ota_http_update_with_url(s_ota_url);
     *  - on success device restarts into new firmware
     */
    publish_event("event=ota_not_executed_in_project21");

    /* Clear flag so we don't spam */
    s_cmd_ota_requested = false;
}

/* -------------------- Main flow -------------------- */

void app_main(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    ESP_LOGI(TAG, "Project 21 starting");
    ESP_LOGI(TAG, "Wakeup reason: %s", wakeup_reason_str(cause));

    /* Device identity + topics */
    generate_node_id();
    build_topics();

    /* NVS (required for Wi-Fi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    /* Connect Wi-Fi */
    publish_event("event=boot");
    wifi_init_and_connect();

    EventBits_t wb = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY
    );

    if (!(wb & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi failed -> sleep");
        /* No MQTT possible, go to sleep */
        esp_sleep_enable_timer_wakeup((uint64_t)s_sleep_sec * 1000000ULL);
        esp_deep_sleep_start();
    }

    /* Start MQTT */
    mqtt_start();

    EventBits_t mb = xEventGroupWaitBits(
        s_mqtt_event_group,
        MQTT_CONNECTED_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(15000)
    );

    if (!(mb & MQTT_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "MQTT timeout -> sleep");
        esp_sleep_enable_timer_wakeup((uint64_t)s_sleep_sec * 1000000ULL);
        esp_deep_sleep_start();
    }

    /* Publish a short "wakeup" event and status */
    char extra[64];
    snprintf(extra, sizeof(extra), "reason=%s", wakeup_reason_str(cause));
    publish_status_retained("online", extra);

    /* --- Telemetry placeholder (Project 20 provides real sensor data) --- */
    char telem[128];
    snprintf(telem, sizeof(telem),
             "id=%s;fw=%s;reason=%s",
             s_node_id, FW_VERSION, wakeup_reason_str(cause));

    esp_mqtt_client_publish(s_mqtt_client, s_t_telemetry, telem, 0, 1, 0);

    /* Allow a short window for commands (optional) */
    vTaskDelay(pdMS_TO_TICKS(800));

    /* Check OTA request integration point */
    ota_placeholder_run_if_requested();

    /* Going to sleep -> retained status update */
    char sleep_info[64];
    snprintf(sleep_info, sizeof(sleep_info), "next=%d", s_sleep_sec);
    publish_status_retained("sleep", sleep_info);

    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Deep sleep for %d seconds", s_sleep_sec);
    esp_sleep_enable_timer_wakeup((uint64_t)s_sleep_sec * 1000000ULL);
    esp_deep_sleep_start();
}
