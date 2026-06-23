#!/usr/bin/env python3
"""Package VoidLink firmware output into a GitHub Pages install site."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def copytree_contents(src: Path, dst: Path) -> None:
    dst.mkdir(parents=True, exist_ok=True)
    for item in src.iterdir():
        target = dst / item.name
        if item.is_dir():
            if target.exists():
                shutil.rmtree(target)
            shutil.copytree(item, target)
        else:
            shutil.copy2(item, target)


def build_payload(build_dir: Path, site_dir: Path, version: str) -> tuple[dict, dict]:
    firmware_dir = site_dir / "firmware" / f"voidlink-ncm-adapter-{version}"
    firmware_dir.mkdir(parents=True, exist_ok=True)

    sources = {
        "bootloader.bin": build_dir / "bootloader" / "bootloader.bin",
        "partitions.bin": build_dir / "partition_table" / "partition-table.bin",
        "firmware.bin": build_dir / "voidlink_ncm_adapter.bin",
    }

    for name, src in sources.items():
        if not src.exists():
            raise FileNotFoundError(f"Missing firmware artifact: {src}")
        shutil.copy2(src, firmware_dir / name)

    offsets = [
        {"offset": 0, "path": f"firmware/voidlink-ncm-adapter-{version}/bootloader.bin"},
        {"offset": 32768, "path": f"firmware/voidlink-ncm-adapter-{version}/partitions.bin"},
        {"offset": 65536, "path": f"firmware/voidlink-ncm-adapter-{version}/firmware.bin"},
    ]

    files = []
    for name in sources:
        target = firmware_dir / name
        files.append({
            "name": name,
            "length": target.stat().st_size,
            "sha256": sha256_file(target),
        })

    manifest = {
        "name": "VoidLink T-Dongle USB Adapter",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3", "parts": offsets}],
    }

    build = {
        "name": "voidlink-ncm-adapter",
        "version": version,
        "target": "LilyGO T-Dongle S3 / ESP32-S3 native USB",
        "transport": "USB NCM",
        "status": "packaged",
        "generated": datetime.now(timezone.utc).isoformat(),
        "warning": "Flashing overwrites the selected dongle firmware.",
        "offsets": offsets,
        "files": files,
    }

    return manifest, build


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--site-dir", type=Path, required=True)
    parser.add_argument("--version", default="0.2.0")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    site_dir = args.site_dir.resolve()
    docs_dir = ROOT / "docs"
    deck_dir = ROOT / "deck"

    if site_dir.exists():
        shutil.rmtree(site_dir)
    copytree_contents(docs_dir, site_dir)
    copytree_contents(deck_dir, site_dir / "deck")

    manifest, build = build_payload(build_dir, site_dir, args.version)
    (site_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (site_dir / "build.json").write_text(json.dumps(build, indent=2) + "\n", encoding="utf-8")

    print(f"Packaged VoidLink site at {site_dir}")


if __name__ == "__main__":
    main()
