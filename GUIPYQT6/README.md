# Weather Station GUI (PyQt6)

GUI desktop app to communicate with your STM32 weather station via:
- **Bluetooth HC-06 (Serial COM)**
- **WiFi (TCP socket)**

## Features
- Connect/disconnect Bluetooth COM port
- Connect/disconnect WiFi TCP endpoint
- Send text commands (`1`, `2`, `READ`, etc.)
- Receive logs/telemetry in real time
- Auto-parse telemetry format: `RAW=... mV=... LUX=...`
- Quick buttons for PB15 LED ON/OFF

## 1) Install dependencies

```bash
pip install -r requirements.txt
```

## 2) Run

```bash
python main.py
```

## 3) Typical Bluetooth setup
1. Pair HC-06 in Windows
2. Open app -> Bluetooth tab
3. Refresh Ports -> choose `Standard Serial over Bluetooth link (COMx)`
4. Baud: `9600`
5. Connect
6. Send command `1` (LED ON) and `2` (LED OFF)

## 4) Supported telemetry format
The app auto-detects and updates sensor cards when receiving line:

```text
RAW=1234 mV=995 LUX=456
```

## 5) Notes
- Use CRLF line ending (app sends `\r\n` automatically)
- If you switch firmware baudrate, update the GUI baud field accordingly
- WiFi tab is for TCP endpoint testing (ESP-01S or other module/server)
