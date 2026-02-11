/******************************************************************************
 * AIoT Workshop – Band 1
 * Project 20: Final Node (ADC + I2C + MQTT + Deep Sleep + OTA-ready)
 *
 * Copyright (c) 2026 Friedrich Riedhammer
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

#include "mqtt_client.h"

/* ---- ADC (Project 15) ---- */
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

/* --------------------------------------------------------------------------
 * USER CONFIG
 * -------------------------------------------------------------------------- */

#define WIFI_SSID           "YOUR_SSID_HERE"
#define WIFI_PASS           "YOUR_PASSWORD_HERE"

#define MQTT_BROKER_URI     "mqtt://test.mosquitto.org"
#define NODE_ID             "node1"

#define TOPIC_TELEMETRY     "aiot/node1/telemetry"
#define TOPIC_STATUS        "aiot/node1/status"
#define TOPIC_CMD           "aiot/node1/cmd"

#define WIFI_MAX_RETRY      10
#define SLEEP_TIME_SEC      30

/* ADC fixed for this book: ADC1 on GPIO2 */
#define ADC_UNIT_USED       ADC_UNIT_1
#define ADC_CHANNEL_USED    ADC_CHANNEL_1
#define ADC_ATTEN           ADC_ATTEN_DB_11
#define ADC_BITWIDTH        ADC_BITWIDTH_DEFAULT
#define ADC_SAMPLES         64

static const char *TAG = "PROJECT20";

/* --------------------------------------------------------------------------
 * Wi-Fi sync
 * -------------------------------------------------------------------------- */

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
static int s_retry_count = 0;

/* --------------------------------------------------------------------------
 * MQTT sync
 * -------------------------------------------------------------------------- */

static EventGroupHandle_t s_mqtt_event_group;
#define MQTT_CONNECTED_BIT  BIT0
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

/* --------------------------------------------------------------------------
 * ADC calibration
 * -------------------------------------------------------------------------- */

static bool adc_create_calibration(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        *out_handle = handle;
        return true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        *out_handle = handle;
        return true;
    }
#endif

    return false;
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
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
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

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

/* --------------------------------------------------------------------------
 * MQTT event handler
 * -------------------------------------------------------------------------- */

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);

            /* Subscribe to command topic (for future extension / band 2) */
            esp_mqtt_client_subscribe(s_mqtt_client, TOPIC_CMD, 0);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "CMD topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "CMD data : %.*s", event->data_len, event->data);
            break;

        default:
            break;
    }
}

/* --------------------------------------------------------------------------
 * Init Wi-Fi
 * -------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------
 * Start MQTT
 * -------------------------------------------------------------------------- */

static void mqtt_start(void)
{
    s_mqtt_event_group = xEventGroupCreate();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(s_mqtt_client,
                                   ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   NULL);

    esp_mqtt_client_start(s_mqtt_client);
}

/* --------------------------------------------------------------------------
 * Read ADC (returns mV if calibration available, else raw as negative marker)
 * -------------------------------------------------------------------------- */

static int read_adc_mv(void)
{
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_USED,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_USED, &chan_config));

    adc_cali_handle_t cali_handle = NULL;
    bool cali_ok = adc_create_calibration(ADC_UNIT_USED, ADC_ATTEN, &cali_handle);

    int sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_USED, &raw));
        sum += raw;
    }
    int raw_avg = sum / ADC_SAMPLES;

    int mv = -1;
    if (cali_ok) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_avg, &mv));
        return mv;
    }

    /* return a negative value to indicate "raw only" */
    return -raw_avg;
}

/* --------------------------------------------------------------------------
 * Main application
 * -------------------------------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "Project 20 starting (final node)");

    /* show wakeup reason */
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wakeup cause: %d", cause);

    /* NVS (required by Wi-Fi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    /* 1) Wi-Fi connect */
    wifi_init_and_connect();

    EventBits_t wbits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY
    );

    if (!(wbits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi failed -> going to sleep");
        esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_TIME_SEC * 1000000ULL);
        esp_deep_sleep_start();
    }

    /* 2) MQTT connect */
    mqtt_start();

    EventBits_t mbits = xEventGroupWaitBits(
        s_mqtt_event_group,
        MQTT_CONNECTED_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(15000)
    );

    if (!(mbits & MQTT_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "MQTT connect timeout -> going to sleep");
        esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_TIME_SEC * 1000000ULL);
        esp_deep_sleep_start();
    }

    /* 3) Read sensors (ADC in this band, I2C sensor optional for later) */
    int adc_val = read_adc_mv();

    char payload[128];

    if (adc_val >= 0) {
        snprintf(payload, sizeof(payload),
                 "node=%s;adc_mv=%d;wakeup=%d",
                 NODE_ID, adc_val, (int)cause);
    } else {
        snprintf(payload, sizeof(payload),
                 "node=%s;adc_raw=%d;wakeup=%d",
                 NODE_ID, -adc_val, (int)cause);
    }

    ESP_LOGI(TAG, "Telemetry payload: %s", payload);

    /* 4) Publish telemetry */
    esp_mqtt_client_publish(s_mqtt_client,
                            TOPIC_TELEMETRY,
                            payload,
                            0,
                            1,
                            0);

    /* optional small delay to allow MQTT send before sleep */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 5) Deep sleep */
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", SLEEP_TIME_SEC);
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_TIME_SEC * 1000000ULL);
    esp_deep_sleep_start();
}
