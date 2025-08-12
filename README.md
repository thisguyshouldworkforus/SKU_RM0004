# UCTRONICS Display for AlmaLinux / RHEL

## Overview
This project is a fork of the UCTRONICS display utility for Raspberry Pi, fully refactored and optimized for **AlmaLinux 10 / RHEL 9+** environments.

The display utility uses the **ST7735** controller via I2C to display system information such as hostname, IP address, CPU load, RAM usage, temperature, and disk usage.

The source code has been heavily modified for stability, clarity, and better integration with server environments.

---

## Key Changes in This Fork

### 1. Hostname & IP Display Behavior
- The first line of the display now shows:
  - **`hostname ip`** when IP display is enabled.
  - **`HOSTNAME` (uppercase)** when IP display is disabled.
- Removed the `"IP:"` prefix to prevent line wrapping on smaller displays.
- Hostname/IP formatting is handled by `get_ip_address_new()` in `rpiInfo.c`.

### 2. Disk Usage Calculation
- Changed disk usage source to read from **`/dev/root`** instead of hardcoded `/dev/mmcblk0` or `/` paths.
- Correctly calculates total and used disk space for any filesystem mounted as root.

### 3. Codebase Improvements
- Entire **`st7735.c`** rewritten for clean C89 compatibility.
- Memory safety improvements: buffer bounds checking, `malloc()`/`free()` handling for IP strings.
- Removed redundant variables and fixed potential truncation bugs.
- Clearer separation of drawing functions and data retrieval logic.

### 4. Optimizations for AlmaLinux / RHEL
- Uses `/dev/i2c-1` by default (can be changed in source).
- Fully compatible with **systemd** service environments.
- No Raspberry Pi–specific dependencies — works on any SBC with the ST7735 over I2C.

---

## Installation

### Build from Source
```bash
# Clone your forked repository
git clone https://github.com/YOURUSERNAME/uctronics-display.git
cd uctronics-display

# Build
make clean && make

# Install binary
sudo install -m 0755 display /usr/bin/uctronics-display
```

### Running Manually
```bash
uctronics-display
```

### Running as a Service
Create a systemd service file at `/etc/systemd/system/uctronics-display.service`:
```ini
[Unit]
Description=UCTRONICS Display Service
After=multi-user.target

[Service]
ExecStart=/usr/bin/uctronics-display
Restart=always
User=root

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now uctronics-display
```

---

## Display Layout

Example when IP display is enabled:
```
ansible 10.0.0.15
=================
CPU: 12%
```

Example when IP display is disabled:
```
ANSIBLE
=================
CPU: 12%
```

---

## File Changes Summary

### Modified Files
- `hardware/rpiInfo/rpiInfo.c`
  - Updated `get_ip_address_new()` to return formatted string: `hostname ip` or `HOSTNAME`.
  - Added logic for uppercase hostname only when IP display is disabled.
- `hardware/st7735/st7735.c`
  - Removed `"IP:"` prefix entirely.
  - Now calls `get_ip_address_new()` directly for first-line display.
  - Memory safety fixes and code cleanup.

---

## License
This project remains under the same license as the original UCTRONICS source. Please check `LICENSE` for details.
