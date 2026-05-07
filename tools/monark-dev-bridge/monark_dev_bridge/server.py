"""FastAPI app: dashboard + JSON state + WS stream + scan/bind endpoints."""

from __future__ import annotations

import asyncio
import json
import logging
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from monark_dev_bridge import config as cfgmod
from monark_dev_bridge import tcx as tcxmod
from monark_dev_bridge.aggregator import ROLES, Aggregator, Role, Sample
from monark_dev_bridge.ble.client import RoleClient
from monark_dev_bridge.ble.scanner import scan as ble_scan

log = logging.getLogger(__name__)
WEB_DIR = Path(__file__).parent / "web"
STREAM_HZ = 10.0


class Bridge:
    """Owns the aggregator, the per-role clients, and the live broadcast pump."""

    def __init__(self, cfg_path: Path) -> None:
        self.cfg_path = cfg_path
        self.cfg = cfgmod.load(cfg_path)
        self.aggregator = Aggregator()
        self.clients: dict[Role, RoleClient] = {}
        self._websockets: set[WebSocket] = set()
        self._log_fp = None
        self._broadcast_task: asyncio.Task | None = None
        self._scan_lock = asyncio.Lock()
        self._scan_results: list[dict] = []
        # Lightweight chat log so the assistant can post status notes the user
        # can read on the dashboard while pedaling. In-memory ring buffer.
        self._notes: list[dict] = []
        self._notes_max = 100

    # ---- lifecycle ----

    async def start(self) -> None:
        log_path = Path(self.cfg.log_path)
        self._log_fp = log_path.open("a", buffering=1)  # line-buffered
        self._sync_clients()
        self._broadcast_task = asyncio.create_task(self._broadcast_loop(), name="broadcast")

    async def stop(self) -> None:
        if self._broadcast_task:
            self._broadcast_task.cancel()
        await asyncio.gather(*(c.stop() for c in self.clients.values()), return_exceptions=True)
        if self._log_fp:
            self._log_fp.close()

    # ---- state ----

    def status(self) -> dict[str, Any]:
        return {
            "connections": {role: c.status() for role, c in self.clients.items()},
            **self.aggregator.snapshot(),
        }

    # ---- ingest ----

    def _on_sample(self, sample: Sample) -> None:
        self.aggregator.ingest(sample)
        if self._log_fp:
            self._log_fp.write(json.dumps(sample.to_dict()) + "\n")

    # ---- broadcast loop ----

    async def _broadcast_loop(self) -> None:
        period = 1.0 / STREAM_HZ
        while True:
            try:
                await asyncio.sleep(period)
                if not self._websockets:
                    continue
                payload = json.dumps(self.status())
                dead: list[WebSocket] = []
                for ws in list(self._websockets):
                    try:
                        await ws.send_text(payload)
                    except Exception:
                        dead.append(ws)
                for ws in dead:
                    self._websockets.discard(ws)
            except asyncio.CancelledError:
                break
            except Exception:
                log.exception("broadcast loop error")

    # ---- websockets ----

    async def attach_ws(self, ws: WebSocket) -> None:
        await ws.accept()
        self._websockets.add(ws)
        try:
            await ws.send_text(json.dumps(self.status()))
            while True:
                # We don't expect any inbound messages; drain to detect close.
                await ws.receive_text()
        except WebSocketDisconnect:
            pass
        finally:
            self._websockets.discard(ws)

    # ---- scan / bind ----

    async def scan_and_remember(self, duration: float = 10.0) -> list[dict]:
        async with self._scan_lock:
            results = await ble_scan(duration=duration, only_relevant=True)
            self._scan_results = [d.to_dict() for d in results]
            return self._scan_results

    def last_scan(self) -> list[dict]:
        return self._scan_results

    async def bind(self, role: Role, address: str, name: str | None = None) -> None:
        if role not in ROLES:
            raise HTTPException(400, f"unknown role: {role}")
        cfgmod.set_device(self.cfg, role, address, name)
        cfgmod.save(self.cfg, self.cfg_path)
        # Restart just this role's client.
        old = self.clients.get(role)
        if old:
            await old.stop()
        client = RoleClient(role, address, on_sample=self._on_sample)
        client.start()
        self.clients[role] = client

    # ---- notes (assistant → user chat) ----

    def append_note(self, text: str, kind: str = "info") -> dict:
        note = {"ts": time.time(), "text": text, "kind": kind}
        self._notes.append(note)
        if len(self._notes) > self._notes_max:
            self._notes = self._notes[-self._notes_max:]
        return note

    def list_notes(self, since_ts: float = 0.0) -> list[dict]:
        if since_ts <= 0:
            return list(self._notes)
        return [n for n in self._notes if n["ts"] > since_ts]

    def _sync_clients(self) -> None:
        for role in ROLES:
            d = self.cfg.device(role)
            if not d.is_set():
                continue
            client = RoleClient(role, d.address, on_sample=self._on_sample)
            client.start()
            self.clients[role] = client


def build_app(cfg_path: Path) -> FastAPI:
    bridge = Bridge(cfg_path)

    @asynccontextmanager
    async def lifespan(_: FastAPI):
        await bridge.start()
        try:
            yield
        finally:
            await bridge.stop()

    app = FastAPI(lifespan=lifespan, title="monark-dev-bridge")
    app.state.bridge = bridge

    @app.get("/", response_class=HTMLResponse)
    async def index() -> HTMLResponse:
        return HTMLResponse((WEB_DIR / "index.html").read_text())

    app.mount("/web", StaticFiles(directory=WEB_DIR), name="web")

    @app.get("/api/state")
    async def state() -> JSONResponse:
        return JSONResponse(bridge.status())

    @app.get("/api/scan")
    async def scan_get() -> JSONResponse:
        return JSONResponse({"results": bridge.last_scan()})

    @app.post("/api/scan")
    async def scan_post(duration: float = 10.0) -> JSONResponse:
        results = await bridge.scan_and_remember(duration=duration)
        return JSONResponse({"results": results})

    @app.post("/api/bind")
    async def bind(payload: dict) -> JSONResponse:
        role = payload.get("role")
        address = payload.get("address")
        name = payload.get("name")
        if role not in ROLES or not address:
            raise HTTPException(400, "role and address required")
        await bridge.bind(role, address, name)
        return JSONResponse({"ok": True, "role": role, "address": address})

    @app.websocket("/api/stream")
    async def stream(ws: WebSocket) -> None:
        await bridge.attach_ws(ws)

    # ---- notes / chat (assistant posts, dashboard polls) ------------------

    @app.get("/api/notes")
    async def notes_get(since: float = 0.0) -> JSONResponse:
        return JSONResponse({"notes": bridge.list_notes(since_ts=since)})

    @app.post("/api/notes")
    async def notes_post(payload: dict) -> JSONResponse:
        text = (payload.get("text") or "").strip()
        if not text:
            raise HTTPException(400, "text required")
        kind = payload.get("kind", "info")
        if kind not in ("info", "warn", "ok", "wait"):
            kind = "info"
        note = bridge.append_note(text, kind=kind)
        return JSONResponse({"ok": True, "note": note})

    # ---- session export (Garmin Connect TCX) -------------------------------

    def _build_tcx_for(which: str) -> str:
        log_path = Path(bridge.cfg.log_path)
        all_samples = tcxmod.read_samples(log_path)
        if which == "real":
            power = [s for s in all_samples if s.get("role") == "real_power"]
            hr = [s for s in all_samples if s.get("role") == "hr"]
            # 5 s rolling mean on Assioma power so it visually matches the ESP32
            # firmware's smoothed output. Without this, the Assioma trace is
            # jagged next to the ESP32 trace purely from sample-rate / smoothing
            # differences, not real disagreement.
            power = tcxmod.smooth_power(power, window_s=5.0)
            merged = tcxmod.merge_power_with_hr(power, hr)
            return tcxmod.build_tcx(merged, activity_name="Monark — real pedals + HR (5s smoothed)")
        if which == "esp32":
            esp = [s for s in all_samples if s.get("role") == "esp32"]
            return tcxmod.build_tcx(esp, activity_name="Monark — ESP32 firmware")
        raise HTTPException(404, f"unknown export target: {which}")

    @app.get("/api/session/real.tcx")
    async def session_real_tcx():
        from fastapi.responses import Response
        body = _build_tcx_for("real")
        ts = time.strftime("%Y%m%d-%H%M%S")
        return Response(
            content=body, media_type="application/vnd.garmin.tcx+xml",
            headers={"Content-Disposition": f'attachment; filename="monark-real-{ts}.tcx"'},
        )

    @app.get("/api/session/esp32.tcx")
    async def session_esp32_tcx():
        from fastapi.responses import Response
        body = _build_tcx_for("esp32")
        ts = time.strftime("%Y%m%d-%H%M%S")
        return Response(
            content=body, media_type="application/vnd.garmin.tcx+xml",
            headers={"Content-Disposition": f'attachment; filename="monark-esp32-{ts}.tcx"'},
        )

    # ---- cycle-constant tuning ---------------------------------------------
    # Hold steady for N seconds; we average power from both real pedals and the
    # ESP32 over that window and return the multiplier needed to make the firmware
    # match reality:  new_constant = current_constant * (real_avg / esp32_avg)

    @app.post("/api/calibrate/cycle-constant")
    async def calibrate_cycle_constant(payload: dict):
        from fastapi.responses import JSONResponse as JR
        duration_s = float(payload.get("duration_s", 30))
        current = float(payload.get("current_constant", 1.05))
        if duration_s < 5 or duration_s > 300:
            raise HTTPException(400, "duration_s must be 5..300")

        start = time.time()
        end = start + duration_s
        real_sum = 0.0
        real_n = 0
        esp_sum = 0.0
        esp_n = 0
        # Snapshot the current "latest" tick periodically. The aggregator updates
        # on every BLE notify (~1 Hz per role), so 5 Hz polling here is plenty.
        seen_real_ts: set[float] = set()
        seen_esp_ts: set[float] = set()
        while time.time() < end:
            r = bridge.aggregator.latest("real_power")
            e = bridge.aggregator.latest("esp32")
            if r and r.power_w is not None and r.ts not in seen_real_ts:
                seen_real_ts.add(r.ts)
                real_sum += r.power_w
                real_n += 1
            if e and e.power_w is not None and e.ts not in seen_esp_ts:
                seen_esp_ts.add(e.ts)
                esp_sum += e.power_w
                esp_n += 1
            await asyncio.sleep(0.2)

        if real_n < 3 or esp_n < 3:
            return JR({
                "ok": False,
                "error": "not enough samples — make sure both real_power and esp32 are connected and pedaling steadily",
                "real_samples": real_n,
                "esp32_samples": esp_n,
            }, status_code=400)

        real_avg = real_sum / real_n
        esp_avg = esp_sum / esp_n
        if esp_avg <= 0:
            return JR({"ok": False, "error": "ESP32 average power is 0; pedal harder?"}, status_code=400)

        ratio = real_avg / esp_avg
        suggested = current * ratio
        return JR({
            "ok": True,
            "duration_s": duration_s,
            "current_constant": current,
            "real_avg_w": round(real_avg, 1),
            "esp32_avg_w": round(esp_avg, 1),
            "ratio_real_over_esp32": round(ratio, 4),
            "suggested_constant": round(suggested, 3),
            "real_samples": real_n,
            "esp32_samples": esp_n,
            "note": (
                "Plug 'suggested_constant' into Cycle Constant on the device's "
                "calibration page (http://192.168.4.1) and save. No reboot needed."
            ),
        })

    return app
