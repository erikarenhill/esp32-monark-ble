# Monark dev bridge — design

A macOS-side dev tool that connects to the user's real BLE sensors (cycling power pedals + HR strap) and to the ESP32 firmware under test (also CPS), then exposes a side-by-side comparison view used as the iteration feedback loop while developing the firmware.

The ESP32 itself is **not** modified — this is a Mac-side observer.

## Decisions

- **Comparison-only architecture.** Mac is the hub; firmware is unaware of the bridge.
- **All ingest is BLE.** Mac reads ESP32 over its production Cycling Power Service (`0x1818`/`0x2A63`) — same code path a real bike computer uses. This validates the actual broadcast output, not a side channel.
- **Python + bleak + FastAPI.** Single async process. uv-managed.
- **UX scope: live numbers + 60s rolling overlaid chart.** Plus a structured JSON endpoint and ndjson log file so the AI assistant can read state during iteration without a browser.
- **Scan-then-bind config.** No manual MAC entry. CLI `scan` and a Web UI scan panel both populate `bridge.toml`.

## Architecture

Single Python process, three concurrent components on one event loop:

1. **BLE client pool (`bleak`)** — three `RoleClient` instances (`real_power`, `hr`, `esp32`). Each connects, subscribes to its characteristic, parses notifications, pushes typed `Sample` objects to the aggregator. Auto-reconnect with exponential backoff (1s→10s).
2. **Aggregator** — owns rolling 60s ring buffers per role + `latest` per role + computes `diff_w = latest.real_power.power_w - latest.esp32.power_w`. Single source of truth.
3. **FastAPI server on `localhost:8765`** — serves `/` (dashboard), `GET /api/state` (snapshot JSON), `WS /api/stream` (push at ~10 Hz), `POST /api/scan`, `POST /api/bind {role,address}`.

Config in `bridge.toml`. Logs in `bridge.log` as ndjson (one Sample per line).

## Data flow

```
[real pedals] ─BLE notify─┐
[HR strap]    ─BLE notify─┼─► RoleClient ─► Sample ─► Aggregator ─┬─► /api/state
[ESP32]       ─BLE notify─┘                                       ├─► WS /api/stream → uPlot
                                                                  └─► bridge.log (ndjson)
```

## Components

- `ble/scanner.py` — `async scan(duration=10)`, filtered to peripherals advertising CPS or HR.
- `ble/client.py` — `RoleClient(role, address, on_sample)`. Reconnect loop, stale-sample marking.
- `ble/parsers.py` — pure `parse_cps(bytes, prev_state) -> (sample, state)` and `parse_hr(bytes) -> sample`. Stateful for cadence (delta crank revs / delta time).
- `aggregator.py` — `Aggregator.ingest()`, `.snapshot()`.
- `config.py` — TOML load/save (`tomllib` read; manual write since schema is tiny and stable).
- `server.py` — FastAPI routes + WS broadcast loop.
- `web/index.html` + `web/app.js` — single page, uPlot via CDN, no build step.
- `cli.py` — `typer` subcommands: `run`, `scan`, `bind <role> <address>`.

## Sample shape

```python
@dataclass
class Sample:
    role: Literal["real_power","hr","esp32"]
    ts: float                # epoch seconds
    power_w: float | None
    cadence_rpm: float | None
    hr_bpm: int | None
```

## Error handling

- Peripheral missing / disconnect → reconnect with exponential backoff. State `reconnecting` in `/api/state`.
- Malformed packet → log hex + drop sample, keep connection.
- macOS Bluetooth permission denied → one-line fix message, exit non-zero.
- Adapter off → explicit message, exit non-zero.
- Missing config → tool starts, dashboard opens to scan view.
- One role's failure must not block the others.

## Testing

- **Unit:** parser tests with captured byte fixtures (CPS power-only, CPS power+cadence, HR uint8/uint16, malformed).
- **Unit:** aggregator tests (ring-buffer, latest, diff, stale).
- **Manual smoke:** ~30s real-hardware run; assert `/api/state` shows 3 connected roles and `bridge.log` accumulates.

Out of scope v1: mocking bleak; cross-platform; perf tests.

## Tooling

`uv` for env, `pytest` + `pytest-asyncio` for tests, `ruff` for lint. macOS only.

## Layout

```
tools/monark-dev-bridge/
  pyproject.toml
  README.md
  bridge.toml.example
  monark_dev_bridge/
    __init__.py  __main__.py  cli.py  config.py  aggregator.py  server.py
    ble/{__init__.py, scanner.py, client.py, parsers.py}
    web/{index.html, app.js}
  tests/{test_parsers.py, test_aggregator.py, fixtures/}
```
