# VoidLink T-Dongle USB Adapter

VoidLink is a separate public firmware/install project for turning a LilyGO T-Dongle S3 into a USB network adapter for deck work. It does not depend on the older T-Dongle control-unit firmware path.

The firmware target is an ESP32-S3 USB NCM adapter with a dongle-hosted T-Deck pairing web UI. When flashed, the dongle enumerates on Linux/Windows as a USB Ethernet-style interface instead of only a serial device, gives the computer an address over DHCP, and serves the pairing page at `http://192.168.4.1/`.

VoidLink is the separate USB network-card build. Use this build when you want the computer to reach the T-Dongle through the USB network adapter and open the pairing/control page locally.

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

Then package the web installer files:

```powershell
cd "F:\Dropbox\Dev Ops\T-Dongle USB Adapter"
python .\tools\package_site.py --build-dir .\firmware\voidlink-ncm-adapter\build --site-dir .\site --version 0.2.1
```

## Linux Host Notes

After flashing, plug the T-Dongle into the laptop. Linux should expose a new USB network interface. On Fedora/OpenSUSE-style NetworkManager systems, check it with:

```bash
nmcli device status
ip link
```

The host interface should receive an address from the dongle. Open:

```text
http://192.168.4.1/
```

Expected status endpoint:

```text
http://192.168.4.1/api/status
```

The dongle page is the setup surface. It includes:

- `Enable T-Deck support`
- `Select T-Deck USB` through the browser Web Serial chooser
- `Begin Pair`
- `Confirm`
- `Reset Pairing`

## Source Basis

The firmware uses the ESP32-S3 TinyUSB device stack plus the `usb-netif` component so the dongle can run DHCP and HTTP directly over the USB network link. The local files are kept small and project-specific so the adapter can evolve into T-Deck pairing/control without touching older builds.
