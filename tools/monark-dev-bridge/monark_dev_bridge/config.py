"""Tiny TOML config loader/saver. Uses stdlib tomllib for read; manual write since the schema is fixed."""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

from monark_dev_bridge.aggregator import ROLES, Role


@dataclass
class DeviceCfg:
    address: str | None = None
    name: str | None = None

    def is_set(self) -> bool:
        return self.address is not None


@dataclass
class Config:
    port: int = 8765
    log_path: str = "bridge.log"
    devices: dict[Role, DeviceCfg] = field(
        default_factory=lambda: {r: DeviceCfg() for r in ROLES}
    )

    def device(self, role: Role) -> DeviceCfg:
        return self.devices[role]


def load(path: Path) -> Config:
    cfg = Config()
    if not path.exists():
        return cfg
    raw = tomllib.loads(path.read_text())
    server = raw.get("server", {})
    cfg.port = int(server.get("port", cfg.port))
    cfg.log_path = str(server.get("log_path", cfg.log_path))
    devices = raw.get("devices", {})
    for role in ROLES:
        d = devices.get(role, {}) or {}
        cfg.devices[role] = DeviceCfg(
            address=d.get("address"),
            name=d.get("name"),
        )
    return cfg


def save(cfg: Config, path: Path) -> None:
    """Write back as TOML. Schema is small and stable so manual emit is fine."""
    lines: list[str] = []
    lines.append("[server]")
    lines.append(f"port = {cfg.port}")
    lines.append(f'log_path = "{_escape(cfg.log_path)}"')
    lines.append("")
    for role in ROLES:
        d = cfg.devices[role]
        lines.append(f"[devices.{role}]")
        if d.address:
            lines.append(f'address = "{_escape(d.address)}"')
        else:
            lines.append("# address = \"AA:BB:CC:DD:EE:FF\"")
        if d.name:
            lines.append(f'name = "{_escape(d.name)}"')
        lines.append("")
    path.write_text("\n".join(lines))


def _escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def set_device(cfg: Config, role: Role, address: str, name: str | None = None) -> None:
    cfg.devices[role] = DeviceCfg(address=address, name=name)
