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
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"

#include "mdns.h"

#define WIFI_SSID   "farswitch"
#define WIFI_PASS   "Kl79_?Sa13_04_1961Kl79_?Sa"

// Hostname muss RFC-konform sein: a-z 0-9 -
#define HOSTNAME    "esp32-aiot-buch"

#define TELNET_PORT 23
#define LINE_BUF_SIZE 256

static const char *TAG = "wifi_telnet";
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static esp_netif_t *s_netif = NULL;

/* ---------- Helpers ---------- */

static void sock_send(int sock, const char *s) {
    send(sock, s, strlen(s), 0);
}

static void sock_sendf(int sock, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sock_send(sock, buf);
}

static void get_netinfo(char *ip, size_t ip_len,
                        char *gw, size_t gw_len,
                        char *mask, size_t mask_len)
{
    esp_netif_ip_info_t info = {0};
    if (s_netif && esp_netif_get_ip_info(s_netif, &info) == ESP_OK) {
        snprintf(ip,   ip_len,   IPSTR, IP2STR(&info.ip));
        snprintf(gw,   gw_len,   IPSTR, IP2STR(&info.gw));
        snprintf(mask, mask_len, IPSTR, IP2STR(&info.netmask));
    } else {
        snprintf(ip, ip_len, "0.0.0.0");
        snprintf(gw, gw_len, "0.0.0.0");
        snprintf(mask, mask_len, "0.0.0.0");
    }
}

static void get_dns(char *dns1, size_t dns1_len, char *dns2, size_t dns2_len)
{
    const ip_addr_t *d1 = dns_getserver(0);
    const ip_addr_t *d2 = dns_getserver(1);

    if (d1) snprintf(dns1, dns1_len, "%s", ipaddr_ntoa(d1));
    else    snprintf(dns1, dns1_len, "0.0.0.0");

    if (d2) snprintf(dns2, dns2_len, "%s", ipaddr_ntoa(d2));
    else    snprintf(dns2, dns2_len, "0.0.0.0");
}

static int get_rssi(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}

/* ---------- WiFi Events ---------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ---------- Command Handling ---------- */

static void handle_cmd(int sock, char *cmd)
{
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;

    if (cmd[0] == '\0') return;

    if (!strcmp(cmd, "help")) {
        sock_send(sock,
            "\r\nCommands:\r\n"
            "  help    - show this help\r\n"
            "  ip      - show IP/GW/Mask/DNS\r\n"
            "  rssi    - show WiFi RSSI\r\n"
            "  info    - hostname + basic info\r\n"
            "  reboot  - restart ESP\r\n"
            "  quit    - close session\r\n"
        );

    } else if (!strcmp(cmd, "ip")) {
        char ip[16], gw[16], mask[16], dns1[32], dns2[32];
        get_netinfo(ip, sizeof(ip), gw, sizeof(gw), mask, sizeof(mask));
        get_dns(dns1, sizeof(dns1), dns2, sizeof(dns2));

        sock_sendf(sock, "\r\nIP:   %s\r\nGW:   %s\r\nMask: %s\r\nDNS1: %s\r\nDNS2: %s",
                   ip, gw, mask, dns1, dns2);

    } else if (!strcmp(cmd, "rssi")) {
        int rssi = get_rssi();
        if (rssi == 0) sock_send(sock, "\r\nNot associated.");
        else sock_sendf(sock, "\r\nRSSI: %d dBm", rssi);

    } else if (!strcmp(cmd, "info")) {
        sock_sendf(sock,
            "\r\nHostname: %s\r\nSSID: %s\r\nBuild: %s %s",
            HOSTNAME, WIFI_SSID, __DATE__, __TIME__);

    } else if (!strcmp(cmd, "reboot")) {
        sock_send(sock, "\r\nRebooting...\r\n");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();

    } else if (!strcmp(cmd, "quit")) {
        sock_send(sock, "\r\nBye.\r\n");

    } else {
        sock_send(sock, "\r\nUnknown command (type 'help')");
    }
}

/* ---------- Telnet Task ---------- */

static void telnet_task(void *arg)
{
    // Wait for WiFi + DHCP
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TELNET_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(listen_sock, 1);

    while (1) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        int sock = accept(listen_sock, (struct sockaddr *)&client, &len);
        if (sock < 0) continue;

        // Banner with netinfo (so you don't need serial monitor)
        char ip[16], gw[16], mask[16], dns1[32], dns2[32];
        get_netinfo(ip, sizeof(ip), gw, sizeof(gw), mask, sizeof(mask));
        get_dns(dns1, sizeof(dns1), dns2, sizeof(dns2));

        sock_sendf(sock,
            "\r\nESP32 WiFi Terminal\r\n"
            "Host: %s.local\r\n"
            "IP:   %s\r\n"
            "GW:   %s\r\n"
            "DNS1: %s\r\n"
            "Type 'help'\r\n> ",
            HOSTNAME, ip, gw, dns1);

        char line[LINE_BUF_SIZE] = {0};
        int idx = 0;

        while (1) {
            char c;
            int n = recv(sock, &c, 1, 0);
            if (n <= 0) break;

            // Enter
            if (c == '\r' || c == '\n') {
                line[idx] = 0;
                handle_cmd(sock, line);
                if (!strcmp(line, "quit")) break;
                idx = 0;
                memset(line, 0, sizeof(line));
                sock_send(sock, "\r\n> ");
                continue;
            }

            // Backspace
            if (c == 0x08 || c == 0x7F) {
                if (idx > 0) {
                    idx--;
                    line[idx] = 0;
                    sock_send(sock, "\b \b");
                }
                continue;
            }

            // Printable
            if (idx < LINE_BUF_SIZE - 1 && (unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E) {
                line[idx++] = c;
                send(sock, &c, 1, 0); // local echo
            }
        }

        close(sock);
    }
}

/* ---------- app_main ---------- */

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_event_group = xEventGroupCreate();

    // Create STA netif and set hostname BEFORE wifi start
    s_netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(s_netif, HOSTNAME));

    // WiFi init
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // mDNS for hostname.local
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 AIoT Terminal"));
    mdns_service_add(NULL, "_telnet", "_tcp", TELNET_PORT, NULL, 0);

    xTaskCreate(telnet_task, "telnet_task", 4096, NULL, 5, NULL);
}
