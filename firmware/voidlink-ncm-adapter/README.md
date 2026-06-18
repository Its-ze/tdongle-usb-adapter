# VoidLink NCM Adapter Firmware

ESP-IDF firmware for LilyGO T-Dongle S3 / ESP32-S3 boards with native USB.

This build is the new virtual-adapter path. It is not the existing serial/AP T-Dongle control-unit firmware.

## Configure

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

Set:

- `VoidLink Adapter -> Wi-Fi SSID`
- `VoidLink Adapter -> Wi-Fi password`

Then build:

```bash
idf.py build
```

The CI/package step expects these generated files:

- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
- `build/voidlink_ncm_adapter.bin`
