"""FastAPI app: dashboard + JSON state + WS stream + scan/bind endpoints."""

from __future__ import annotations

import asyncio
import json
import logging
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from monark_dev_bridge import config as cfgmod
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

    return app
