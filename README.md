# UCTRONICS Pi-Rack Pro Display – AlmaLinux/RHEL Optimized Fork

## Overview

This is a customized and optimized fork of the [UCTRONICS SKU_RM0004](https://github.com/UCTRONICS/SKU_RM0004) repository designed for **AlmaLinux 10 / RHEL** environments running on Raspberry Pi hardware (Pi 4 and similar).

It addresses hardware, naming, and metric calculation issues encountered when deploying the Pi-Rack Pro display system outside of Raspberry Pi OS, and on systems where:

- Network interfaces are **not** named `eth0` or `wlan0` (e.g., `end0`).
- Root filesystem devices are **not** `/dev/sda` (e.g., `/dev/root`, NVMe, LVM, mmcblk).
- The original code’s `get_sd_memory()` return values were inverted vs. what the display UI expects.
- GPIO overlay conflicts with `gpio-shutdown` caused immediate safe shutdowns.

---

## Changes & Fixes

### 1. **Custom Network Interface Support**
- Added a new `CUSTOM_ADDRESS` constant and `CUSTOM_IFNAME` macro in `rpiInfo.h`.
- The network display code now supports:
  - `ETH0_ADDRESS` → shows IP of `eth0`.
  - `WLAN0_ADDRESS` → shows IP of `wlan0`.
  - `CUSTOM_ADDRESS` → shows IP of any interface name (set via `CUSTOM_IFNAME`).
- Default `CUSTOM_IFNAME` in this fork is `"end0"`.

**Why:** AlmaLinux/RHEL on Pi often uses `predictable network interface names` like `end0`, breaking the original hardcoded eth0/wlan0 lookups.

---

### 2. **Robust IP Address Lookup**
- Replaced brittle shell parsing of `/sbin/ifconfig` with `getifaddrs()` for direct IPv4 address retrieval.
- This avoids dependencies on deprecated tools and ensures compatibility with minimal server installs.

---

### 3. **Root Filesystem Disk Usage Fix**
- Original `get_hard_disk_memory()` ran:
  ```sh
  df -l | grep /dev/sda | awk '{print $2}' ...
  ```
  …which fails on `/dev/root`, NVMe, or LVM devices.
- Now uses `statvfs("/")` to measure the mounted root filesystem regardless of device name.
- No reliance on `df` output or device naming conventions.

---

### 4. **Corrected SD/Root Usage Math**
- Original `get_sd_memory()` labeled the second output param `freesize` but actually returned **used space** (GB).
- Our first rewrite returned **free space**, which caused the display to show ~90% usage on all systems.
- Now restored the original semantic (return **used MB**), but kept the code clean and accurate.

---

### 5. **GPIO4 Safe Shutdown Conflict Resolution**
- Original instructions enabled:
  ```ini
  dtoverlay=gpio-shutdown,gpio_pin=4,active_low=1,gpio_pull=up
  ```
  …while the `display` binary also manipulates GPIO4.
- On AlmaLinux/RHEL, this resulted in an **instant safe shutdown** as soon as `display` ran.
- **Fix:** Disable the overlay in `/boot/config.txt` and let the `display` app handle the button in userspace.

---

### 6. **Temperature Reading**
- Reads from `/sys/class/thermal/thermal_zone0/temp`.
- Converts to °C or °F depending on `TEMPERATURE_TYPE`.
- Clamped to 0–255 for OLED-friendly display.

---

### 7. **CPU Load Bucketing**
- Uses `/proc/loadavg` (1-minute average) normalized by CPU core count.
- Scaled to a 0–255 range for graphical display.

---

## AlmaLinux / RHEL Optimizations

- **Interface Naming:** Supports predictable names like `end0` out of the box.
- **Disk Metrics:** Works with `/dev/root` (mmcblk), NVMe, LVM without changes.
- **No `ifconfig` dependency:** Compatible with minimal `dnf`-based server installs.
- **No `/dev/sda` assumption:** Portable across SD, SSD, USB, or network-root Pis.
- **glibc API usage:** Uses `getifaddrs()` and `statvfs()` — no parsing external command output.
- **Safe GPIO handling:** Removes conflicting overlays that trigger systemd-logind shutdown events.

---

## Build & Install

### 1. Clone Your Fork
```bash
git clone https://github.com/<yourusername>/SKU_RM0004.git
cd SKU_RM0004
```

### 2. Build
```bash
make clean && make
```

### 3. Install
```bash
install -m 0755 display /usr/bin/uctronics-display
```
*(Or change the Makefile target to `uctronics-display` if you want that name compiled in.)*

### 4. Run
```bash
uctronics-display
```

---

## Boot Service Setup

To start `uctronics-display` automatically at boot:

```bash
cat >/etc/systemd/system/uctronics-display.service <<'EOF'
[Unit]
Description=UCTRONICS OLED display service
After=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/uctronics-display
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now uctronics-display
```

---

## Compatibility Notes

- Tested on AlmaLinux 10 (aarch64) with Pi 4 hardware.
- Works on RHEL clones and Raspberry Pi OS (but Pi OS users generally don’t need the `/dev/root` and `end0` changes).
- If using the GPIO button for safe shutdown:
  - Let `uctronics-display` handle it in userspace.
  - Avoid `gpio-shutdown` overlay on the same pin (GPIO4) to prevent immediate shutdown events.

---

## License
This fork retains the original license from the UCTRONICS SKU_RM0004 repository.
