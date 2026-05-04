from monark_dev_bridge.aggregator import Aggregator, Sample


def test_latest_per_role():
    a = Aggregator()
    a.ingest(Sample("real_power", ts=1.0, power_w=200))
    a.ingest(Sample("real_power", ts=2.0, power_w=210))
    a.ingest(Sample("esp32", ts=2.0, power_w=205))
    assert a.latest("real_power").power_w == 210
    assert a.latest("esp32").power_w == 205
    assert a.latest("hr") is None


def test_diff_w():
    a = Aggregator()
    a.ingest(Sample("real_power", ts=1.0, power_w=300))
    a.ingest(Sample("esp32", ts=1.0, power_w=290))
    assert a.diff_w() == 10


def test_diff_w_none_when_missing():
    a = Aggregator()
    a.ingest(Sample("real_power", ts=1.0, power_w=300))
    assert a.diff_w() is None


def test_buffer_evicts_old_samples():
    a = Aggregator(window_seconds=10.0)
    for ts in range(0, 25):
        a.ingest(Sample("real_power", ts=float(ts), power_w=ts))
    buf = a.buffer("real_power")
    # Last sample ts=24, window=10 → cutoff=14, so ts >= 14 retained
    assert buf[0].ts >= 14
    assert buf[-1].ts == 24


def test_snapshot_marks_stale():
    a = Aggregator()
    a.ingest(Sample("real_power", ts=100.0, power_w=200))
    snap = a.snapshot(now=110.0)  # 10s later — past STALE_AFTER_SECONDS=5
    assert snap["latest"]["real_power"]["stale"] is True
    assert snap["latest"]["real_power"]["age_s"] == 10.0

    snap = a.snapshot(now=102.0)  # 2s — fresh
    assert snap["latest"]["real_power"]["stale"] is False


def test_snapshot_includes_all_roles_even_when_missing():
    a = Aggregator()
    snap = a.snapshot(now=0)
    assert set(snap["latest"].keys()) == {"real_power", "hr", "esp32"}
    assert all(v is None for v in snap["latest"].values())
