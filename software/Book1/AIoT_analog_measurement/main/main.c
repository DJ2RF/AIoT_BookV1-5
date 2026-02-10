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
/*
 * PROJECT 15
 * Analog measurement using ADC1 (GPIO2)
 *
 * Functions:
 *  - initialize ADC1
 *  - configure ADC channel
 *  - perform oversampling
 *  - average samples
 *  - convert raw values to millivolts
 *  - print measurement results periodically
 */
/*
 * Reference measurement setup:
 *
 * 3.3V --- 10k --- ADC(GPIO2) --- 10k --- GND
 *
 * Expected voltage at ADC: approx. 1.65 V
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

/* ADC configuration */
#define ADC_UNIT_USED          ADC_UNIT_1
#define ADC_CHANNEL_USED       ADC_CHANNEL_1      // GPIO2
#define ADC_ATTENUATION        ADC_ATTEN_DB_11
#define ADC_BITWIDTH_CONFIG    ADC_BITWIDTH_DEFAULT

/* measurement configuration */
#define ADC_SAMPLES            64
#define ADC_PERIOD_MS          500

static const char *TAG = "PROJECT15";

/* Calibration helper */
static bool adc_create_calibration(adc_unit_t unit,
                                   adc_atten_t atten,
                                   adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_CONFIG,
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
        .bitwidth = ADC_BITWIDTH_CONFIG,
    };

    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        *out_handle = handle;
        return true;
    }
#endif

    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "PROJECT 15 - ADC measurement starting");

    /* create ADC unit */
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_USED,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    /* configure ADC channel */
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_CONFIG
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle,
                                               ADC_CHANNEL_USED,
                                               &chan_config));

    /* enable calibration */
    adc_cali_handle_t cali_handle = NULL;
    bool calibration_enabled = adc_create_calibration(ADC_UNIT_USED,
                                                      ADC_ATTENUATION,
                                                      &cali_handle);

    ESP_LOGI(TAG, "ADC calibration: %s",
             calibration_enabled ? "enabled" : "not available");

    while (1)
    {
        int raw_sum = 0;

        /* oversampling loop */
        for (int i = 0; i < ADC_SAMPLES; i++) {
            int raw = 0;
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle,
                                             ADC_CHANNEL_USED,
                                             &raw));
            raw_sum += raw;
        }

        int raw_avg = raw_sum / ADC_SAMPLES;

        if (calibration_enabled) {
            int voltage_mv = 0;
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle,
                                                    raw_avg,
                                                    &voltage_mv));

            ESP_LOGI(TAG,
                     "ADC raw(avg)=%d  ->  %d mV",
                     raw_avg,
                     voltage_mv);
        }
        else {
            ESP_LOGI(TAG,
                     "ADC raw(avg)=%d (no calibration)",
                     raw_avg);
        }

        vTaskDelay(pdMS_TO_TICKS(ADC_PERIOD_MS));
    }
}
