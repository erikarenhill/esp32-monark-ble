"""BLE discovery — scan for nearby peripherals advertising CPS or HR."""

from __future__ import annotations

from dataclasses import dataclass

from bleak import BleakScanner

CPS_SERVICE_UUID = "00001818-0000-1000-8000-00805f9b34fb"
HR_SERVICE_UUID = "0000180d-0000-1000-8000-00805f9b34fb"
RELEVANT_SERVICE_UUIDS = {CPS_SERVICE_UUID, HR_SERVICE_UUID}


@dataclass
class DiscoveredDevice:
    address: str
    name: str
    rssi: int | None
    services: list[str]

    def offers_cps(self) -> bool:
        return CPS_SERVICE_UUID in self.services

    def offers_hr(self) -> bool:
        return HR_SERVICE_UUID in self.services

    def to_dict(self) -> dict:
        return {
            "address": self.address,
            "name": self.name,
            "rssi": self.rssi,
            "services": self.services,
            "offers_cps": self.offers_cps(),
            "offers_hr": self.offers_hr(),
        }


async def scan(duration: float = 10.0, only_relevant: bool = True) -> list[DiscoveredDevice]:
    """Run a passive scan for `duration` seconds.

    By default, returns only peripherals advertising CPS or HR. Set `only_relevant=False`
    to return everything (useful when sensors aren't advertising the service in their
    advertising data — only in the GATT table).
    """
    devices_with_adv = await BleakScanner.discover(
        timeout=duration, return_adv=True
    )
    out: list[DiscoveredDevice] = []
    for address, (device, adv) in devices_with_adv.items():
        services = [u.lower() for u in (adv.service_uuids or [])]
        d = DiscoveredDevice(
            address=address,
            name=(device.name or adv.local_name or "(unknown)"),
            rssi=adv.rssi,
            services=services,
        )
        if only_relevant and not (d.offers_cps() or d.offers_hr()):
            continue
        out.append(d)
    out.sort(key=lambda d: (d.rssi if d.rssi is not None else -999), reverse=True)
    return out
