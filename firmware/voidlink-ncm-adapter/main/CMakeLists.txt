idf_component_register(
    SRCS "usb_adapter_main.c"
    INCLUDE_DIRS ""
    REQUIRES chegewara__usb-netif espressif__esp_tinyusb
    PRIV_REQUIRES esp_event esp_http_server esp_hw_support esp_netif nvs_flash
)
