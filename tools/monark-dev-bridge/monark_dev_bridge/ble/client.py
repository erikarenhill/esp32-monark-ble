"""Per-role BLE client: connects, subscribes, parses, and pushes Samples to a callback.

Auto-reconnects with exponential backoff (1s → 10s cap) on disconnect/connect failure.
One BLE error in one role must not affect the others.
"""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Awaitable, Callable, Literal

from bleak import BleakClient

from monark_dev_bridge.aggregator import Sample
from monark_dev_bridge.ble.parsers import (
    CpsParseState,
    parse_cps,
    parse_hr,
)

log = logging.getLogger(__name__)

CPS_MEAS_CHAR = "00002a63-0000-1000-8000-00805f9b34fb"
HR_MEAS_CHAR = "00002a37-0000-1000-8000-00805f9b34fb"

Role = Literal["real_power", "hr", "esp32"]
SampleSink = Callable[[Sample], Awaitable[None] | None]

BACKOFF_INITIAL = 1.0
BACKOFF_CAP = 10.0


class RoleClient:
    """Owns the BLE connection for one logical role.

    `state` (`disconnected` / `connecting` / `connected` / `reconnecting`) and `last_error`
    are exposed for the dashboard's status row.
    """

    def __init__(self, role: Role, address: str, on_sample: SampleSink) -> None:
        self.role = role
        self.address = address
        self.on_sample = on_sample
        self.state: str = "disconnected"
        self.last_error: str | None = None
        self.connected_at: float | None = None

        self._task: asyncio.Task | None = None
        self._stop = asyncio.Event()
        # CPS parser keeps cadence delta state across notifications.
        self._cps_state = CpsParseState()

    # ---- lifecycle ----

    def start(self) -> None:
        if self._task is not None:
            return
        self._stop.clear()
        self._task = asyncio.create_task(self._run(), name=f"role-{self.role}")

    async def stop(self) -> None:
        self._stop.set()
        if self._task is not None:
            self._task.cancel()
            try:
                await self._task
            except (asyncio.CancelledError, Exception):
                pass
            self._task = None
        self.state = "disconnected"

    def status(self) -> dict:
        return {
            "role": self.role,
            "address": self.address,
            "state": self.state,
            "last_error": self.last_error,
            "connected_at": self.connected_at,
        }

    # ---- internals ----

    async def _run(self) -> None:
        backoff = BACKOFF_INITIAL
        while not self._stop.is_set():
            try:
                self.state = "connecting"
                async with BleakClient(self.address) as client:
                    self.state = "connected"
                    self.connected_at = time.time()
                    self.last_error = None
                    backoff = BACKOFF_INITIAL
                    log.info("[%s] connected to %s", self.role, self.address)
                    await self._subscribe(client)
                    # Keep the connection open until we lose it or are told to stop.
                    while client.is_connected and not self._stop.is_set():
                        await asyncio.sleep(0.5)
            except asyncio.CancelledError:
                break
            except Exception as exc:  # bleak raises a wide range; we treat all as recoverable
                self.last_error = f"{type(exc).__name__}: {exc}"
                log.warning("[%s] %s — reconnecting in %.1fs", self.role, self.last_error, backoff)
            self.state = "reconnecting"
            self.connected_at = None
            try:
                await asyncio.wait_for(self._stop.wait(), timeout=backoff)
                break  # stop event fired during sleep
            except asyncio.TimeoutError:
                pass
            backoff = min(backoff * 2, BACKOFF_CAP)

    async def _subscribe(self, client: BleakClient) -> None:
        if self.role in ("real_power", "esp32"):
            await client.start_notify(CPS_MEAS_CHAR, self._cps_handler)
        elif self.role == "hr":
            await client.start_notify(HR_MEAS_CHAR, self._hr_handler)
        else:
            raise ValueError(f"unknown role {self.role}")

    def _cps_handler(self, _char, data: bytearray) -> None:
        try:
            r = parse_cps(bytes(data), self._cps_state)
        except Exception as exc:
            log.error("[%s] CPS parse failed (%s): %s", self.role, exc, data.hex())
            return
        s = Sample(
            role=self.role, ts=time.time(),
            power_w=float(r.power_w), cadence_rpm=r.cadence_rpm,
        )
        self._dispatch(s)

    def _hr_handler(self, _char, data: bytearray) -> None:
        try:
            r = parse_hr(bytes(data))
        except Exception as exc:
            log.error("[%s] HR parse failed (%s): %s", self.role, exc, data.hex())
            return
        s = Sample(role=self.role, ts=time.time(), hr_bpm=r.hr_bpm)
        self._dispatch(s)

    def _dispatch(self, sample: Sample) -> None:
        result = self.on_sample(sample)
        if asyncio.iscoroutine(result):
            asyncio.create_task(result)
