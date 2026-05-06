"""Parsers for Bluetooth SIG Cycling Power Measurement (0x2A63) and Heart Rate Measurement (0x2A37)."""

from __future__ import annotations

import struct
from dataclasses import dataclass


# ---- CPS ----------------------------------------------------------------

CPS_FLAG_PEDAL_BALANCE = 1 << 0
CPS_FLAG_PEDAL_BALANCE_REF = 1 << 1
CPS_FLAG_ACCUM_TORQUE = 1 << 2
CPS_FLAG_ACCUM_TORQUE_SRC = 1 << 3
CPS_FLAG_WHEEL_REV = 1 << 4
CPS_FLAG_CRANK_REV = 1 << 5

# Crank event time is in units of 1/1024 s and rolls over at 65536.
CRANK_TIME_RESOLUTION_HZ = 1024
CRANK_TIME_ROLLOVER = 1 << 16
CRANK_REV_ROLLOVER = 1 << 16


@dataclass
class CpsParseState:
    """Persisted across notifications so we can compute cadence from deltas."""
    last_crank_revs: int | None = None
    last_crank_time: int | None = None
    last_cadence_rpm: float | None = None


@dataclass
class CpsReading:
    power_w: int  # signed instantaneous power (W)
    cadence_rpm: float | None  # None until we have two crank samples to diff


def parse_cps(data: bytes, state: CpsParseState) -> CpsReading:
    """Parse a CPS Cycling Power Measurement notification.

    Mutates `state` to track crank counters across calls so cadence can be derived.
    Raises struct.error / ValueError on malformed input — caller should catch.
    """
    if len(data) < 4:
        raise ValueError(f"CPS payload too short: {len(data)} bytes")

    flags = struct.unpack_from("<H", data, 0)[0]
    power_w = struct.unpack_from("<h", data, 2)[0]

    offset = 4
    if flags & CPS_FLAG_PEDAL_BALANCE:
        offset += 1
    if flags & CPS_FLAG_ACCUM_TORQUE:
        offset += 2
    if flags & CPS_FLAG_WHEEL_REV:
        offset += 6  # uint32 cumulative revs + uint16 last wheel event time

    cadence_rpm: float | None = state.last_cadence_rpm
    if flags & CPS_FLAG_CRANK_REV:
        if len(data) < offset + 4:
            raise ValueError("CPS payload missing crank revolution data")
        crank_revs, crank_time = struct.unpack_from("<HH", data, offset)
        cadence_rpm = _compute_cadence(crank_revs, crank_time, state)
        state.last_crank_revs = crank_revs
        state.last_crank_time = crank_time
        state.last_cadence_rpm = cadence_rpm

    return CpsReading(power_w=power_w, cadence_rpm=cadence_rpm)


def _compute_cadence(
    crank_revs: int, crank_time: int, state: CpsParseState
) -> float | None:
    if state.last_crank_revs is None or state.last_crank_time is None:
        return None  # need a baseline

    d_revs = (crank_revs - state.last_crank_revs) % CRANK_REV_ROLLOVER
    d_time = (crank_time - state.last_crank_time) % CRANK_TIME_ROLLOVER

    # Counter reset detection: a real CPS rev counter rolls forward by 1 per
    # crank, so deltas wider than half the modular space are almost certainly
    # the peripheral rebooting (e.g. ESP32 reflashed mid-session) — *not* a
    # 32000-rev sprint. Drop the sample, re-baseline, hold last cadence.
    if d_revs > CRANK_REV_ROLLOVER // 2:
        return state.last_cadence_rpm

    if d_time == 0:
        # No crank event since last sample — pedaling stopped or notification rate > sensor rate.
        # Hold last cadence for one tick; aggregator's stale logic decays it eventually.
        return state.last_cadence_rpm
    if d_revs == 0:
        return 0.0
    seconds = d_time / CRANK_TIME_RESOLUTION_HZ
    return (d_revs / seconds) * 60.0


# ---- HR -----------------------------------------------------------------

HR_FLAG_VALUE_FORMAT_UINT16 = 1 << 0


@dataclass
class HrReading:
    hr_bpm: int


def parse_hr(data: bytes) -> HrReading:
    """Parse a Heart Rate Measurement notification."""
    if len(data) < 2:
        raise ValueError(f"HR payload too short: {len(data)} bytes")
    flags = data[0]
    if flags & HR_FLAG_VALUE_FORMAT_UINT16:
        if len(data) < 3:
            raise ValueError("HR payload missing uint16 value")
        hr = struct.unpack_from("<H", data, 1)[0]
    else:
        hr = data[1]
    return HrReading(hr_bpm=hr)
