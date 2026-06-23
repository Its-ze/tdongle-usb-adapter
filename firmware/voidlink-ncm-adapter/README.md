# VoidLink NCM Adapter Firmware

ESP-IDF firmware for LilyGO T-Dongle S3 / ESP32-S3 boards with native USB.

This build is the virtual-adapter pairing path. It is not the existing serial/AP T-Dongle control-unit firmware.

After flashing, the computer should see a USB Ethernet/NCM interface. The dongle runs DHCP on that link and serves the pairing UI at:

```text
http://192.168.4.1/
```

The JSON status endpoint is:

```text
http://192.168.4.1/api/status
```

## Configure

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

Then build:

```bash
idf.py build
```

The CI/package step expects these generated files:

- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
- `build/voidlink_ncm_adapter.bin`
