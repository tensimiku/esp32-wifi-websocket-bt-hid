# ESP32-C3 BLE HID Bridge

A secure, composite BLE HID device (Keyboard + Mouse) for ESP32-C3, controlled via a password-protected **WebSocket Secure (WSS)** web interface.

> Open `https://<ESP32_IP>` on any browser (PC or mobile), type or drag on the virtual trackpad, and the ESP32 injects HID events over Bluetooth to a paired device.

## Features

| Feature | Details |
|---|---|
| **Composite HID** | Keyboard (Report ID 1) + Mouse (Report ID 2) in a single descriptor |
| **WSS Transport** | Persistent WebSocket over TLS — sub-20ms input latency |
| **Virtual Trackpad** | Desktop `mousemove` + mobile `touchmove` with sub-pixel accumulation |
| **Modifier Keys** | Ctrl, Shift, Alt, GUI toggles and combo keys (Ctrl+C, Alt+Tab, etc.) |
| **Language Keys** | Dedicated R-Alt (한/영) and R-Ctrl (한자) tap buttons |
| **Pairing Control** | "Force BLE Pairing Mode" button to restart advertisement |
| **Credential Security** | Wi-Fi & HTTP passwords in Git-ignored `secrets.h` |

## Prerequisites

- **Hardware**: ESP32-C3 development board
- **Toolchain**: [ESP-IDF v6.0](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/)
- **TLS Certificates**: Self-signed PEM files (`cacert.pem`, `prvtkey.pem`) in `main/`

## Quick Start

### 1. Clone & Configure Secrets

```bash
git clone <repo-url>
cd esp_hid_device
cp main/secrets.h.example main/secrets.h
```

Edit `main/secrets.h` with your credentials:
```c
#define WIFI_SSID     "YourSSID"
#define WIFI_PASS     "YourWiFiPassword"
#define HTTP_PASSWORD "YourHTTPPassword"
```

### 2. Generate TLS Certificates (if not present)

```bash
openssl req -x509 -newkey rsa:2048 -keyout main/prvtkey.pem -out main/cacert.pem \
  -days 3650 -nodes -subj '/CN=ESP32'
```

### 3. Build & Flash

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p COM7 flash monitor    # adjust COM port as needed
```

### 4. Connect

1. The serial monitor will print the assigned IP address (e.g., `192.168.0.18`).
2. Open `https://<IP>` in a browser. Accept the self-signed certificate warning.
3. Pair the ESP32 via Bluetooth on the target device (appears as **"ESP BLE HID Combo"**).
4. Enter the HTTP password, then type or use the trackpad.

## Project Structure

```
esp_hid_device/
├── main/
│   ├── esp_hid_device_main.c   # Main application (Wi-Fi, WSS, HID logic)
│   ├── esp_hid_gap.c/h         # BLE GAP configuration
│   ├── CMakeLists.txt           # Build config (embedded certs)
│   ├── secrets.h                # Credentials (git-ignored)
│   ├── secrets.h.example        # Template for secrets.h
│   ├── cacert.pem               # TLS server certificate
│   └── prvtkey.pem              # TLS private key
├── sdkconfig                    # ESP-IDF configuration
└── README.md
```

## Key Configuration (sdkconfig)

| Option | Value | Purpose |
|---|---|---|
| `CONFIG_HTTPD_WS_SUPPORT` | `y` | Enable WebSocket in the HTTP server |
| `CONFIG_BT_BLE_ENABLED` | `y` | Enable BLE |

## WebSocket Protocol

All messages use the format: `password|type|data`

| Type | Format | Example |
|---|---|---|
| Keyboard | `pw\|K\|modifier\|char` | `mypass\|K\|1\|c` (Ctrl+C) |
| Tap Key | `pw\|T\|modifier` | `mypass\|T\|64` (R-Alt tap) |
| Mouse | `pw\|M\|buttons,dx,dy,wheel` | `mypass\|M\|1,5,-3,0` |

## Troubleshooting

| Symptom | Solution |
|---|---|
| Mouse not moving (X:0 Y:0 in logs) | Hard-refresh browser (`Ctrl+Shift+R`) to clear cached JS |
| Mouse coordinates OK but no movement on host | Unpair & re-pair Bluetooth on the host device |
| `ESP_ERR_MBEDTLS_SSL_SETUP_FAILED` | Ensure WSS is used instead of HTTPS POST; reduce connection frequency |
| TLS handshake errors (`-0x7780`) | Normal for initial browser cert negotiation; accept the self-signed cert |
| Trackpad text selection during drag | Already fixed with `user-select: none` CSS |

## Architecture Notes

- **Single Composite Report Map**: Keyboard and Mouse descriptors are merged into one `compositeReportMap[]` array. This is required for Android compatibility — Android ignores secondary GATT Report Map characteristics.
- **Sub-pixel Accumulation**: The trackpad JS uses `lx += dx` (not `lx = e.clientX`) to prevent High-DPI sub-pixel coordinate decay.
- **Modifier Bitmask**: Standard USB HID modifier bits (L-Ctrl=0x01, L-Shift=0x02, L-Alt=0x04, L-GUI=0x08, R-Ctrl=0x10, R-Shift=0x20, R-Alt=0x40).

## License

Based on Espressif Systems example code. See [SPDX license header](main/esp_hid_device_main.c) for details.
