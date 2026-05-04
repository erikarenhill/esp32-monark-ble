"""In-memory store of the latest sample per role + a 60s rolling buffer for the chart."""

from __future__ import annotations

import time
from collections import deque
from dataclasses import asdict, dataclass
from typing import Literal

Role = Literal["real_power", "hr", "esp32"]
ROLES: tuple[Role, ...] = ("real_power", "hr", "esp32")

WINDOW_SECONDS = 60.0
STALE_AFTER_SECONDS = 5.0


@dataclass
class Sample:
    role: Role
    ts: float
    power_w: float | None = None
    cadence_rpm: float | None = None
    hr_bpm: int | None = None

    def to_dict(self) -> dict:
        return asdict(self)


class Aggregator:
    def __init__(self, window_seconds: float = WINDOW_SECONDS) -> None:
        self._window = window_seconds
        self._latest: dict[Role, Sample] = {}
        self._buffers: dict[Role, deque[Sample]] = {r: deque() for r in ROLES}

    def ingest(self, sample: Sample) -> None:
        self._latest[sample.role] = sample
        buf = self._buffers[sample.role]
        buf.append(sample)
        cutoff = sample.ts - self._window
        while buf and buf[0].ts < cutoff:
            buf.popleft()

    def latest(self, role: Role) -> Sample | None:
        return self._latest.get(role)

    def buffer(self, role: Role) -> list[Sample]:
        return list(self._buffers[role])

    def diff_w(self) -> float | None:
        real = self._latest.get("real_power")
        esp = self._latest.get("esp32")
        if real is None or esp is None or real.power_w is None or esp.power_w is None:
            return None
        return real.power_w - esp.power_w

    def snapshot(self, now: float | None = None) -> dict:
        """Compact JSON-friendly view used by /api/state."""
        now = now if now is not None else time.time()
        latest = {}
        for role in ROLES:
            s = self._latest.get(role)
            if s is None:
                latest[role] = None
                continue
            d = s.to_dict()
            d["age_s"] = round(now - s.ts, 3)
            d["stale"] = (now - s.ts) > STALE_AFTER_SECONDS
            latest[role] = d
        return {
            "latest": latest,
            "diff_w": self.diff_w(),
            "updated_at": now,
        }
