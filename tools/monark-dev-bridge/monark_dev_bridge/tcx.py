"""Build Garmin TCX XML from recorded Sample dicts.

TCX is XML with a Track of Trackpoints. For indoor cycling we emit per-second points
with Time, optional HeartRateBpm, Cadence, and the ActivityExtension Watts element.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable
from xml.sax.saxutils import escape

TCX_NS = "http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2"
EXT_NS = "http://www.garmin.com/xmlschemas/ActivityExtension/v2"


def _iso(ts: float) -> str:
    return (
        datetime.fromtimestamp(ts, tz=timezone.utc)
        .strftime("%Y-%m-%dT%H:%M:%S.") + f"{int((ts % 1) * 1000):03d}Z"
    )


def read_samples(log_path: Path) -> list[dict]:
    """Load all sample lines from a bridge ndjson log."""
    if not log_path.exists():
        return []
    out: list[dict] = []
    with log_path.open("r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return out


def smooth_power(samples: list[dict], window_s: float = 3.0) -> list[dict]:
    """Apply a centred rolling-mean over `window_s` seconds to the `power_w` field.

    The Assioma pedals report instantaneous 1 Hz power which is jagged compared
    to the ESP32 output (the firmware already rolls 3 s internally before sending
    via BLE). When overlaying for visual comparison, applying the same window
    on the reference side cancels the jitter that's *only* due to the difference
    in smoothing strategy, not real measurement disagreement.

    Returns a new list with the same shape; non-power fields are untouched.
    """
    if not samples or window_s <= 0:
        return list(samples)
    half = window_s / 2.0
    src = sorted(samples, key=lambda s: s["ts"])
    out: list[dict] = []
    n = len(src)
    lo = 0
    hi = 0
    for i, s in enumerate(src):
        ts = s["ts"]
        while lo < n and src[lo]["ts"] < ts - half:
            lo += 1
        while hi < n and src[hi]["ts"] <= ts + half:
            hi += 1
        vals = [src[j].get("power_w") for j in range(lo, hi) if src[j].get("power_w") is not None]
        new = dict(s)
        if vals:
            new["power_w"] = sum(vals) / len(vals)
        out.append(new)
    return out


def merge_power_with_hr(power_samples: list[dict], hr_samples: list[dict]) -> list[dict]:
    """For each power sample, attach the most recent hr_bpm <= ts (within 10s)."""
    if not power_samples:
        return []
    hr_sorted = sorted(hr_samples, key=lambda s: s["ts"])
    out: list[dict] = []
    hr_idx = 0
    last_hr: int | None = None
    last_hr_ts: float | None = None
    for ps in sorted(power_samples, key=lambda s: s["ts"]):
        ts = ps["ts"]
        while hr_idx < len(hr_sorted) and hr_sorted[hr_idx]["ts"] <= ts:
            last_hr = hr_sorted[hr_idx].get("hr_bpm")
            last_hr_ts = hr_sorted[hr_idx]["ts"]
            hr_idx += 1
        merged = dict(ps)
        if last_hr is not None and last_hr_ts is not None and (ts - last_hr_ts) <= 10.0:
            merged["hr_bpm"] = last_hr
        out.append(merged)
    return out


def build_tcx(samples: list[dict], activity_name: str = "Monark indoor ride") -> str:
    """Render samples (each with ts, power_w, cadence_rpm, optional hr_bpm) as TCX."""
    if not samples:
        # Empty TCX still parses; produce a minimal valid doc.
        now = datetime.now(tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        return _empty_tcx(now, activity_name)

    samples = sorted(samples, key=lambda s: s["ts"])
    start_ts = samples[0]["ts"]
    end_ts = samples[-1]["ts"]
    total_sec = max(1.0, end_ts - start_ts)

    points: list[str] = []
    for s in samples:
        bits: list[str] = [f"<Time>{_iso(s['ts'])}</Time>"]
        hr = s.get("hr_bpm")
        if hr is not None:
            bits.append(f"<HeartRateBpm><Value>{int(round(hr))}</Value></HeartRateBpm>")
        cad = s.get("cadence_rpm")
        if cad is not None:
            cad_int = max(0, min(254, int(round(cad))))
            bits.append(f"<Cadence>{cad_int}</Cadence>")
        pwr = s.get("power_w")
        if pwr is not None:
            bits.append(
                "<Extensions>"
                f'<TPX xmlns="{EXT_NS}"><Watts>{int(round(max(0, pwr)))}</Watts></TPX>'
                "</Extensions>"
            )
        points.append("<Trackpoint>" + "".join(bits) + "</Trackpoint>")

    return f"""<?xml version="1.0" encoding="UTF-8"?>
<TrainingCenterDatabase xmlns="{TCX_NS}">
  <Activities>
    <Activity Sport="Biking">
      <Id>{_iso(start_ts)}</Id>
      <Lap StartTime="{_iso(start_ts)}">
        <TotalTimeSeconds>{total_sec:.1f}</TotalTimeSeconds>
        <DistanceMeters>0</DistanceMeters>
        <MaximumSpeed>0</MaximumSpeed>
        <Calories>0</Calories>
        <Intensity>Active</Intensity>
        <TriggerMethod>Manual</TriggerMethod>
        <Track>{''.join(points)}</Track>
      </Lap>
      <Notes>{escape(activity_name)}</Notes>
    </Activity>
  </Activities>
</TrainingCenterDatabase>
"""


def _empty_tcx(iso_now: str, activity_name: str) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<TrainingCenterDatabase xmlns="{TCX_NS}">
  <Activities>
    <Activity Sport="Biking">
      <Id>{iso_now}</Id>
      <Lap StartTime="{iso_now}">
        <TotalTimeSeconds>0</TotalTimeSeconds>
        <DistanceMeters>0</DistanceMeters>
        <MaximumSpeed>0</MaximumSpeed>
        <Calories>0</Calories>
        <Intensity>Active</Intensity>
        <TriggerMethod>Manual</TriggerMethod>
        <Track></Track>
      </Lap>
      <Notes>{escape(activity_name)} (no samples)</Notes>
    </Activity>
  </Activities>
</TrainingCenterDatabase>
"""
