"""CLI entrypoints. `run` starts the server; `scan` lists peripherals; `bind` writes config."""

from __future__ import annotations

import asyncio
import logging
from pathlib import Path

import typer
import uvicorn

from monark_dev_bridge import config as cfgmod
from monark_dev_bridge.aggregator import ROLES
from monark_dev_bridge.ble.scanner import scan as ble_scan
from monark_dev_bridge.server import build_app

app = typer.Typer(help="Mac-side BLE bridge for ESP32 firmware iteration.", no_args_is_help=True)

DEFAULT_CFG = Path("bridge.toml")


def _setup_logging(verbose: bool) -> None:
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s — %(message)s",
        datefmt="%H:%M:%S",
    )


@app.command()
def run(
    config: Path = typer.Option(DEFAULT_CFG, "--config", "-c", help="Path to bridge.toml"),
    host: str = typer.Option("127.0.0.1", "--host"),
    port: int = typer.Option(0, "--port", help="Override config port. 0 = use config."),
    verbose: bool = typer.Option(False, "--verbose", "-v"),
):
    """Start the bridge: BLE clients + dashboard at http://localhost:<port>/"""
    _setup_logging(verbose)
    cfg = cfgmod.load(config)
    final_port = port or cfg.port
    fastapi_app = build_app(config)
    uvicorn.run(fastapi_app, host=host, port=final_port, log_level="warning")


@app.command()
def scan(
    duration: float = typer.Option(10.0, "--duration", "-d"),
    all_devices: bool = typer.Option(False, "--all", help="Include devices not advertising CPS/HR"),
):
    """List nearby BLE peripherals (filtered to CPS/HR by default)."""
    _setup_logging(False)
    results = asyncio.run(ble_scan(duration=duration, only_relevant=not all_devices))
    if not results:
        typer.echo("(no devices found — try --all, move closer, or wake the sensor)")
        raise typer.Exit(0)
    typer.echo(f"{'#':>2}  {'NAME':<28}  {'ADDRESS':<40}  {'RSSI':>5}  SERVICES")
    for i, d in enumerate(results, 1):
        svc = []
        if d.offers_cps(): svc.append("CPS")
        if d.offers_hr(): svc.append("HR")
        typer.echo(f"{i:>2}  {d.name[:28]:<28}  {d.address:<40}  {d.rssi or '?':>5}  {','.join(svc) or '-'}")


@app.command()
def bind(
    role: str = typer.Argument(..., help=f"One of: {', '.join(ROLES)}"),
    address: str = typer.Argument(..., help="BLE address from `scan`"),
    name: str = typer.Option("", "--name"),
    config: Path = typer.Option(DEFAULT_CFG, "--config", "-c"),
):
    """Write a role→address binding to bridge.toml."""
    if role not in ROLES:
        raise typer.BadParameter(f"role must be one of: {', '.join(ROLES)}")
    cfg = cfgmod.load(config)
    cfgmod.set_device(cfg, role, address, name or None)
    cfgmod.save(cfg, config)
    typer.echo(f"bound {role} → {address} (config: {config})")


if __name__ == "__main__":
    app()
