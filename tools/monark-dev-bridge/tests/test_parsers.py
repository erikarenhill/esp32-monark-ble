"""Unit tests for BLE parsers — fixtures hand-built per Bluetooth SIG spec."""

import struct

import pytest

from monark_dev_bridge.ble.parsers import (
    CRANK_TIME_RESOLUTION_HZ,
    CpsParseState,
    parse_cps,
    parse_hr,
)


# ---- CPS ----

def _cps_packet(flags: int, power: int, extra: bytes = b"") -> bytes:
    return struct.pack("<Hh", flags, power) + extra


def test_cps_power_only():
    pkt = _cps_packet(flags=0, power=275)
    r = parse_cps(pkt, CpsParseState())
    assert r.power_w == 275
    assert r.cadence_rpm is None


def test_cps_negative_power():
    pkt = _cps_packet(flags=0, power=-12)
    r = parse_cps(pkt, CpsParseState())
    assert r.power_w == -12


def test_cps_too_short_raises():
    with pytest.raises(ValueError):
        parse_cps(b"\x00\x00\x00", CpsParseState())


def test_cps_crank_first_sample_no_cadence():
    # flags = crank present (bit 5)
    pkt = _cps_packet(flags=0b100000, power=200, extra=struct.pack("<HH", 100, 0))
    r = parse_cps(pkt, CpsParseState())
    assert r.power_w == 200
    assert r.cadence_rpm is None  # need two samples


def test_cps_crank_second_sample_yields_cadence():
    state = CpsParseState()
    # First: 100 revs at t=0
    parse_cps(_cps_packet(0b100000, 200, struct.pack("<HH", 100, 0)), state)
    # Second: 101 revs at t=1024 (== exactly 1.0s later) → 1 rev/s = 60 rpm
    r = parse_cps(_cps_packet(0b100000, 210, struct.pack("<HH", 101, CRANK_TIME_RESOLUTION_HZ)), state)
    assert r.power_w == 210
    assert r.cadence_rpm == pytest.approx(60.0)


def test_cps_crank_handles_time_rollover():
    state = CpsParseState()
    # Seed near the 16-bit time rollover boundary
    parse_cps(_cps_packet(0b100000, 200, struct.pack("<HH", 100, 65535)), state)
    # 1 rev later, time wrapped: 65535 → 1023 means delta = 1024 ticks = 1.0s
    new_time = (65535 + CRANK_TIME_RESOLUTION_HZ) % (1 << 16)
    r = parse_cps(_cps_packet(0b100000, 200, struct.pack("<HH", 101, new_time)), state)
    assert r.cadence_rpm == pytest.approx(60.0)


def test_cps_no_crank_event_holds_last_cadence():
    state = CpsParseState()
    parse_cps(_cps_packet(0b100000, 200, struct.pack("<HH", 100, 0)), state)
    parse_cps(_cps_packet(0b100000, 200, struct.pack("<HH", 101, CRANK_TIME_RESOLUTION_HZ)), state)
    # Same crank counters — no new crank event since last notify
    r = parse_cps(_cps_packet(0b100000, 200, struct.pack("<HH", 101, CRANK_TIME_RESOLUTION_HZ)), state)
    assert r.cadence_rpm == pytest.approx(60.0)  # held


def test_cps_pedal_balance_offset_skipped():
    # bits 0 (balance) and 5 (crank) set
    pkt = _cps_packet(
        flags=0b100001,
        power=180,
        extra=b"\x32" + struct.pack("<HH", 50, 0),  # 1 byte balance, then crank
    )
    r = parse_cps(pkt, CpsParseState())
    assert r.power_w == 180
    assert r.cadence_rpm is None  # first crank sample


# ---- HR ----

def test_hr_uint8():
    r = parse_hr(bytes([0x00, 72]))
    assert r.hr_bpm == 72


def test_hr_uint16():
    r = parse_hr(bytes([0x01]) + struct.pack("<H", 320))
    assert r.hr_bpm == 320


def test_hr_with_extras_ignored():
    # flags: uint8 hr + RR present + energy present (we only care about hr)
    pkt = bytes([0b00011000, 145]) + struct.pack("<H", 1234) + struct.pack("<H", 800)
    r = parse_hr(pkt)
    assert r.hr_bpm == 145


def test_hr_too_short_raises():
    with pytest.raises(ValueError):
        parse_hr(b"\x00")
