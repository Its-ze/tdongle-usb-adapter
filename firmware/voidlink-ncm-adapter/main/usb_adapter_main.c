/*
 * VoidLink T-Dongle USB Adapter
 *
 * USB NCM firmware for ESP32-S3 boards with native USB. The host sees a USB
 * Ethernet-style interface, while the dongle bridges traffic to Wi-Fi STA.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_private/wifi.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"
#include "tusb.h"

static const char *TAG = "voidlink";

static void usb_device_set_link_state(bool link_up)
{
    tud_network_link_state(0, link_up);
}

static esp_err_t usb_recv_callback(void *buffer, uint16_t len, void *ctx)
{
    bool *is_wifi_connected = (bool *)ctx;

    if (*is_wifi_connected) {
        return esp_wifi_internal_tx(WIFI_IF_STA, buffer, len);
    }

    return ESP_OK;
}

static void wifi_pkt_free(void *eb, void *ctx)
{
    (void)ctx;
    esp_wifi_internal_free_rx_buffer(eb);
}

static esp_err_t pkt_wifi_to_usb(void *buffer, uint16_t len, void *eb)
{
    if (tinyusb_net_send_sync(buffer, len, eb, portMAX_DELAY) != ESP_OK) {
        esp_wifi_internal_free_rx_buffer(eb);
    }

    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)event_data;
    bool *is_connected = (bool *)arg;

    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Wi-Fi STA connected");
        ESP_ERROR_CHECK(esp_wifi_internal_reg_rxcb(WIFI_IF_STA, pkt_wifi_to_usb));
        *is_connected = true;
        usb_device_set_link_state(true);
        return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi STA disconnected");
        *is_connected = false;
        ESP_ERROR_CHECK(esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL));
        usb_device_set_link_state(false);

#if CONFIG_VOIDLINK_AUTO_RECONNECT
        ESP_LOGI(TAG, "Trying Wi-Fi reconnect");
        ESP_ERROR_CHECK(esp_wifi_connect());
#endif
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Upstream Wi-Fi IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t start_wifi(uint8_t *mac_ret)
{
    assert(mac_ret);

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Cannot initialize esp-netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Cannot initialize event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "Failed to initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set Wi-Fi station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_get_mac(WIFI_IF_STA, mac_ret), TAG, "Failed to read Wi-Fi MAC");

    wifi_config_t wifi_config = { 0 };
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", CONFIG_VOIDLINK_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", CONFIG_VOIDLINK_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = strlen(CONFIG_VOIDLINK_WIFI_PASSWORD) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Failed to apply Wi-Fi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start Wi-Fi");

    if (strlen(CONFIG_VOIDLINK_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "No Wi-Fi SSID configured; USB NCM will enumerate but stay link-down");
        return ESP_OK;
    }

    return esp_wifi_connect();
}

void app_main(void)
{
    static bool s_is_wifi_connected = false;
    uint8_t wifi_mac[6] = { 0 };

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting VoidLink USB NCM adapter");
    ESP_GOTO_ON_ERROR(start_wifi(wifi_mac), err, TAG, "Failed to initialize Wi-Fi");
    ESP_LOGI(TAG, "Adapter MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4], wifi_mac[5]);

    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_GOTO_ON_ERROR(tinyusb_driver_install(&tusb_cfg), err, TAG, "Failed to install TinyUSB driver");

    tinyusb_net_config_t net_config = {
        .on_recv_callback = usb_recv_callback,
        .free_tx_buffer = wifi_pkt_free,
        .user_context = &s_is_wifi_connected,
    };
    memcpy(net_config.mac_addr, wifi_mac, sizeof(wifi_mac));

    ESP_GOTO_ON_ERROR(tinyusb_net_init(&net_config), err, TAG, "Failed to initialize USB NCM");
    usb_device_set_link_state(false);

    ESP_GOTO_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, &s_is_wifi_connected),
                      err, TAG, "Failed to register Wi-Fi handler");
    ESP_GOTO_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL),
                      err, TAG, "Failed to register IP handler");

    ESP_LOGI(TAG, "VoidLink is ready; connect USB host to the new NCM interface");
    return;

err:
    ESP_LOGE(TAG, "VoidLink startup failed");
}
