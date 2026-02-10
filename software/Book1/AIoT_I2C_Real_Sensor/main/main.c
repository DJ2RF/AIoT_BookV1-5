#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"

#define I2C_MASTER_NUM         I2C_NUM_0
#define I2C_MASTER_SDA_IO      8      // <<< anpassen
#define I2C_MASTER_SCL_IO      9      // <<< anpassen
#define I2C_MASTER_FREQ_HZ     100000

#define MPU6050_ADDR           0x68   // Standardadresse (AD0=0)
#define MPU6050_WHO_AM_I       0x75
#define MPU6050_PWR_MGMT_1     0x6B
#define MPU6050_ACCEL_XOUT_H   0x3B

static const char *TAG = "PROJECT14";

static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t mpu6050_write_byte(uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t mpu6050_read(uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    // Register address schreiben
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);

    // Danach lesen
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_READ, true);

    if (len > 1) {
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);

    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static int16_t be16(const uint8_t *p)
{
    return (int16_t)((p[0] << 8) | p[1]);
}

void app_main(void)
{
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C init ok (SDA=%d, SCL=%d, %d Hz)", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);

    // WHO_AM_I lesen
    uint8_t who = 0;
    ESP_ERROR_CHECK(mpu6050_read(MPU6050_WHO_AM_I, &who, 1));
    ESP_LOGI(TAG, "MPU6050 WHO_AM_I = 0x%02X (erwartet meist 0x68)", who);

    // Sensor aus Sleep holen: PWR_MGMT_1 = 0
    ESP_ERROR_CHECK(mpu6050_write_byte(MPU6050_PWR_MGMT_1, 0x00));
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "MPU6050 aktiv. Lese Accel/Gyro...");

    // Standard-Sensitivitäten (nach Reset):
    // Accel FS = ±2g  => 16384 LSB/g
    // Gyro  FS = ±250°/s => 131 LSB/(°/s)
    const float accel_scale = 16384.0f;
    const float gyro_scale  = 131.0f;

    while (1)
    {
        uint8_t raw[14] = {0};
        esp_err_t ret = mpu6050_read(MPU6050_ACCEL_XOUT_H, raw, sizeof(raw));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C read error: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        int16_t ax = be16(&raw[0]);
        int16_t ay = be16(&raw[2]);
        int16_t az = be16(&raw[4]);
        int16_t temp_raw = be16(&raw[6]);
        int16_t gx = be16(&raw[8]);
        int16_t gy = be16(&raw[10]);
        int16_t gz = be16(&raw[12]);

        // Umrechnungen
        float ax_g = ax / accel_scale;
        float ay_g = ay / accel_scale;
        float az_g = az / accel_scale;

        float gx_dps = gx / gyro_scale;
        float gy_dps = gy / gyro_scale;
        float gz_dps = gz / gyro_scale;

        // Temperatur (MPU-6050 typisch): Temp(°C) = (temp_raw / 340) + 36.53
        float temp_c = (temp_raw / 340.0f) + 36.53f;

        ESP_LOGI(TAG,
                 "A[g]=(%+.3f, %+.3f, %+.3f)  G[dps]=(%+.2f, %+.2f, %+.2f)  T=%.2fC",
                 ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps, temp_c);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
