# VoidLink T-Dongle USB Adapter

VoidLink is a separate public firmware/install project for turning a LilyGO T-Dongle S3 into a USB network adapter for deck work. It does not modify the existing NightGrid Cyberdeck, T-Deck Cyberdeck, or T-Dongle control-unit folders.

The first firmware target is an ESP32-S3 USB NCM adapter. When flashed, the dongle enumerates on Linux/Windows as a USB Ethernet-style interface instead of only a serial device. The initial adapter mode bridges the host USB network side to a configured Wi-Fi STA side, which gives the laptop a real virtual adapter path for deck control workflows.

## Status

- Project name: `VoidLink T-Dongle USB Adapter`
- Firmware: `firmware/voidlink-ncm-adapter`
- Public install page: GitHub Pages `docs/`
- Deck install pack: `deck/voidlink.pack.json`
- Marketplace index: `deck/marketplace-index.json`

This is intentionally a new repo/folder. Existing dongle firmware is left alone until a user explicitly flashes a physical dongle with this new build.

## Install From The Deck

Add this external marketplace/index URL to the deck:

```text
https://its-ze.github.io/tdongle-usb-adapter/deck/marketplace-index.json
```

The deck pack points to the ESP Web Tools installer:

```text
https://its-ze.github.io/tdongle-usb-adapter/
```

Flashing overwrites the firmware on the plugged-in dongle. Back up the current dongle first if you want to keep the older control-unit build.

## Build Locally

ESP-IDF is required for local firmware builds.

```powershell
cd "F:\Dropbox\Dev Ops\T-Dongle USB Adapter\firmware\voidlink-ncm-adapter"
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

Set the Wi-Fi SSID/password under `VoidLink Adapter` in `menuconfig`. Then package the web installer files:

```powershell
cd "F:\Dropbox\Dev Ops\T-Dongle USB Adapter"
python .\tools\package_site.py --build-dir .\firmware\voidlink-ncm-adapter\build --site-dir .\site --version 0.1.0
```

## Linux Host Notes

After flashing, plug the T-Dongle into the laptop. Linux should expose a new USB network interface. On Fedora/OpenSUSE-style NetworkManager systems, check it with:

```bash
nmcli device status
ip link
```

If the upstream Wi-Fi connection in the dongle is configured and associated, the host interface should receive network service through the USB NCM link.

## Source Basis

The firmware scaffold follows Espressif's supported ESP32-S3 TinyUSB device stack and the official `tusb_ncm` USB Network Control Model example. The local files are kept small and project-specific so the adapter can evolve into deck pairing/control without touching the older builds.
