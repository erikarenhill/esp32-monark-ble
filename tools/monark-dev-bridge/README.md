# monark-dev-bridge

A macOS dev tool that connects to your real BLE cycling power pedals + HR strap **and**
the ESP32 firmware under test (also CPS), and shows them side-by-side. Use it as a
real-time feedback loop while iterating on the firmware.

The ESP32 is treated like any other BLE peripheral — the bridge sniffs the same
Cycling Power notifications a bike computer would receive, so you're verifying the
production output path, not a side channel.

## Setup

```bash
cd tools/monark-dev-bridge
uv sync                  # creates .venv and installs deps
uv run pytest            # ~12 unit tests, no hardware needed
```

First run on macOS will prompt for **Bluetooth permission** — grant it to your terminal
(System Settings → Privacy & Security → Bluetooth).

## Discover and bind devices

```bash
uv run monark-dev-bridge scan
# Pick from the list, then:
uv run monark-dev-bridge bind real_power AA:BB:CC:DD:EE:FF
uv run monark-dev-bridge bind hr         AA:BB:CC:DD:EE:FF
uv run monark-dev-bridge bind esp32      AA:BB:CC:DD:EE:FF
```

(Or use the **Scan** button in the dashboard.)

## Run it

```bash
uv run monark-dev-bridge run
# → http://localhost:8765
```

Dashboard shows: real-pedal power, ESP32 power, Δ in watts, 60s overlaid chart, HR,
and per-role connection status.

## Reading state without a browser (assistant feedback loop)

```bash
curl -s localhost:8765/api/state | jq
tail -f bridge.log | jq           # ndjson — one Sample per line
```

The bridge keeps the BLE pipeline going across reflashes — when the ESP32 reboots after
a `pio run -t upload`, the `esp32` role just reconnects automatically.

## Layout

- `monark_dev_bridge/ble/parsers.py` — pure CPS / HR notification parsers (unit-tested)
- `monark_dev_bridge/ble/scanner.py` — `BleakScanner.discover` wrapper, filtered to CPS/HR
- `monark_dev_bridge/ble/client.py`  — per-role connect + subscribe + reconnect backoff
- `monark_dev_bridge/aggregator.py`  — latest sample per role, 60s ring buffer, diff_w
- `monark_dev_bridge/server.py`      — FastAPI app: `/`, `/api/state`, `/api/scan`, `/api/bind`, `WS /api/stream`
- `monark_dev_bridge/cli.py`         — `run` / `scan` / `bind` subcommands
- `monark_dev_bridge/web/`           — single-page dashboard (uPlot, no build step)

## Troubleshooting

- **"BLE permission denied"** — grant your terminal Bluetooth access in System Settings.
- **ESP32 doesn't show up in scan** — make sure it's advertising (LED behavior depends
  on firmware) and not already connected to another device. macOS Bluetooth can only
  hand out one connection per peripheral at a time.
- **Two CPS devices, only one binds** — that's fine; the bridge tracks them by address,
  not by service. Bind one to `real_power` and the other to `esp32`.
